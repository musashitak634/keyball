/*
Copyright 2023 @takashicompany

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H
#include "quantum.h"

/////////////////////////////
/// miniZoneの実装 ここから ///
////////////////////////////

enum custom_keycodes {
    KC_MY_BTN1 = SAFE_RANGE,
    KC_MY_BTN2,
    KC_MY_BTN3,
    KC_MY_SCR,
    KC_TO_CLICKABLE_INC,
    KC_TO_CLICKABLE_DEC,
    KC_SCROLL_DIR_V,
    KC_SCROLL_DIR_H,
};


enum click_state {
    NONE = 0,
    WAITING,    // マウスレイヤーが有効になるのを待つ。 Wait for mouse layer to activate.
    CLICKABLE,  // マウスレイヤー有効になりクリック入力が取れる。 Mouse layer is enabled to take click input.
    CLICKING,   // クリック中。 Clicking.
    SCROLLING   // スクロール中。 Scrolling.
};

typedef union {
  uint32_t raw;
  struct {
    // int16_t to_clickable_time; // // この秒数(千分の一秒)、WAITING状態ならクリックレイヤーが有効になる。  For this number of seconds (milliseconds), if in WAITING state, the click layer is activated.
    int16_t to_clickable_movement;
    bool mouse_scroll_v_reverse;
    bool mouse_scroll_h_reverse;
  };
} user_config_t;

user_config_t user_config;

enum click_state state;     // 現在のクリック入力受付の状態 Current click input reception status
uint16_t click_timer;       // タイマー。状態に応じて時間で判定する。 Timer. Time to determine the state of the system.

// uint16_t to_clickable_time = 50;   // この秒数(千分の一秒)、WAITING状態ならクリックレイヤーが有効になる。  For this number of seconds (milliseconds), if in WAITING state, the click layer is activated.
uint16_t to_reset_time = 1000; // この秒数(千分の一秒)、CLICKABLE状態ならクリックレイヤーが無効になる。 For this number of seconds (milliseconds), the click layer is disabled if in CLICKABLE state.

const uint16_t click_layer = 6;   // マウス入力が可能になった際に有効になるレイヤー。Layers enabled when mouse input is enabled

int16_t scroll_v_mouse_interval_counter;   // 垂直スクロールの入力をカウントする。　Counting Vertical Scroll Inputs
int16_t scroll_h_mouse_interval_counter;   // 水平スクロールの入力をカウントする。  Counts horizontal scrolling inputs.

int16_t scroll_v_threshold = 50;    // この閾値を超える度に垂直スクロールが実行される。 Vertical scrolling is performed each time this threshold is exceeded.
int16_t scroll_h_threshold = 50;    // この閾値を超える度に水平スクロールが実行される。 Each time this threshold is exceeded, horizontal scrolling is performed.

int16_t after_click_lock_movement = 0;      // クリック入力後の移動量を測定する変数。 Variable that measures the amount of movement after a click input.

int16_t mouse_record_threshold = 30;    // ポインターの動きを一時的に記録するフレーム数。 Number of frames in which the pointer movement is temporarily recorded.
int16_t mouse_move_count_ratio = 5;     // ポインターの動きを再生する際の移動フレームの係数。 The coefficient of the moving frame when replaying the pointer movement.

const uint16_t ignore_disable_mouse_layer_keys[] = { KC_LGUI, KC_LCTL, KC_LALT, KC_LSFT, KC_RGUI, KC_RCTL, KC_RALT, KC_RSFT };   // この配列で指定されたキーはマウスレイヤー中に押下してもマウスレイヤーを解除しない

int16_t mouse_movement;

void eeconfig_init_user(void) {
    user_config.raw = 0;
    user_config.to_clickable_movement = 50;
    user_config.mouse_scroll_v_reverse = false;
    user_config.mouse_scroll_h_reverse = false;
    eeconfig_update_user(user_config.raw);
}

void keyboard_post_init_user(void) {
    user_config.raw = eeconfig_read_user();
}

// クリック用のレイヤーを有効にする。　Enable layers for clicks
void enable_click_layer(void) {
    layer_on(click_layer);
    click_timer = timer_read();
    state = CLICKABLE;
#ifdef OLED_ENABLE
    oled_invert(true);  // Invert the OLED display when in click layer
#endif
}

// クリック用のレイヤーを無効にする。 Disable layers for clicks.
void disable_click_layer(void) {
    state = NONE;
    layer_off(click_layer);
    scroll_v_mouse_interval_counter = 0;
    scroll_h_mouse_interval_counter = 0;
#ifdef OLED_ENABLE
    oled_invert(false);  // Revert the OLED display when not in click layer
#endif
}

// 自前の絶対数を返す関数。 Functions that return absolute numbers.
int16_t my_abs(int16_t num) {
    if (num < 0) {
        num = -num;
    }

    return num;
}

// 自前の符号を返す関数。 Function to return the sign.
int16_t mmouse_move_y_sign(int16_t num) {
    if (num < 0) {
        return -1;
    }

    return 1;
}

// 現在クリックが可能な状態か。 Is it currently clickable?
bool is_clickable_mode(void) {
    return state == CLICKABLE || state == CLICKING || state == SCROLLING;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    
    switch (keycode) {
        case KC_MY_BTN1:
        case KC_MY_BTN2:
        case KC_MY_BTN3:
        {
            report_mouse_t currentReport = pointing_device_get_report();

            // どこのビットを対象にするか。 Which bits are to be targeted?
            uint8_t btn = 1 << (keycode - KC_MY_BTN1);
            
            if (record->event.pressed) {
                // ビットORは演算子の左辺と右辺の同じ位置にあるビットを比較して、両方のビットのどちらかが「1」の場合に「1」にします。
                // Bit OR compares bits in the same position on the left and right sides of the operator and sets them to "1" if either of both bits is "1".
                currentReport.buttons |= btn;
                state = CLICKING;
                after_click_lock_movement = 30;
            } else {
                // ビットANDは演算子の左辺と右辺の同じ位置にあるビットを比較して、両方のビットが共に「1」の場合だけ「1」にします。
                // Bit AND compares the bits in the same position on the left and right sides of the operator and sets them to "1" only if both bits are "1" together.
                currentReport.buttons &= ~btn;
            }

            enable_click_layer();

            pointing_device_set_report(currentReport);
            pointing_device_send();
            return false;
        }

        case KC_MY_SCR:
            if (record->event.pressed) {
                state = SCROLLING;
            } else {
                enable_click_layer();   // スクロールキーを離した時に再度クリックレイヤーを有効にする。 Enable click layer again when the scroll key is released.
            }
         return false;
        
        case KC_TO_CLICKABLE_INC:
            if (record->event.pressed) {
                user_config.to_clickable_movement += 5; // user_config.to_clickable_time += 10;
                eeconfig_update_user(user_config.raw);
            }
            return false;

        case KC_TO_CLICKABLE_DEC:
            if (record->event.pressed) {

                user_config.to_clickable_movement -= 5; // user_config.to_clickable_time -= 10;

                if (user_config.to_clickable_movement < 5)
                {
                    user_config.to_clickable_movement = 5;
                }

                // if (user_config.to_clickable_time < 10) {
                //     user_config.to_clickable_time = 10;
                // }

                eeconfig_update_user(user_config.raw);
            }
            return false;
        
        case KC_SCROLL_DIR_V:
            if (record->event.pressed) {
                user_config.mouse_scroll_v_reverse = !user_config.mouse_scroll_v_reverse;
                eeconfig_update_user(user_config.raw);
            }
            return false;
        
        case KC_SCROLL_DIR_H:
            if (record->event.pressed) {
                user_config.mouse_scroll_h_reverse = !user_config.mouse_scroll_h_reverse;
                eeconfig_update_user(user_config.raw);
            }
            return false;
    }

    return true;
}

void pointing_device_task(void) {
    report_mouse_t mouse_report = pointing_device_get_report();
    mouse_movement += my_abs(mouse_report.x) + my_abs(mouse_report.y);

    if (after_click_lock_movement) {
        after_click_lock_movement -= 1;
    }

    if (state == CLICKABLE) {
        if (mouse_report.buttons) {
            click_timer = timer_read();
            state = CLICKING;
            after_click_lock_movement = 30;
        } else if (timer_elapsed(click_timer) > to_reset_time) {
            disable_click_layer();
        }
    } else if (state == WAITING) {
        if (mouse_movement > user_config.to_clickable_movement) {
            enable_click_layer();
        }
    } else if (state == NONE) {
        bool hit = false;

        for (uint8_t i = 0; i < sizeof(ignore_disable_mouse_layer_keys)/sizeof(uint16_t); i++) {
            if (keymap_key_to_keycode(layer_switch_get_layer(keymap_key_to_keycode_user_record->event.key.row, keymap_key_to_keycode_user_record->event.key.col), keymap_key_to_keycode_user_record->event.key) == ignore_disable_mouse_layer_keys[i]) {
                hit = true;
            }
        }

        if (!hit) {
            state = WAITING;
        }
    }

    pointing_device_set_report(mouse_report);
    pointing_device_send();
}

#ifdef OLED_ENABLE
static void render_logo(void) {
    static const char PROGMEM qmk_logo[] = {
        0x00, 0xC0, 0xE0, 0xF8, 0xFC, 0xFC, 0xFE, 0xFE, 0xFE, 0xFC, 0xFC, 0xF8, 0xE0, 0xC0, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0x00, 0xF8, 0xFE, 0xFF, 0xFF, 0xF8, 0xF0, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0x0F, 0x1F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x07, 0x00, 0x00,
        0xF0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x07,
        0x1F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x07, 0x00, 0x00, 0xFF, 0xFF, 0xFF
    };

    oled_write_raw_P(qmk_logo, sizeof(qmk_logo));
}

void oled_task_user(void) {
    if (is_clickable_mode()) {
        render_logo();
    } else {
        render_logo();
    }
}
#endif

///////////////////////////
/// miniZoneの実装 ここまで ///
///////////////////////////

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (index == 0) { /* 左のエンコーダの処理。 Handling the left encoder */
        if (clockwise) {
            tap_code(KC_VOLU); // 時計回りのとき、音量アップ。 Volume up when clockwise.
        } else {
            tap_code(KC_VOLD); // 反時計回りのとき、音量ダウン。 Volume down when counterclockwise.
        }
    } else if (index == 1) { /* 右のエンコーダの処理。 Handling the right encoder */
        if (clockwise) {
            tap_code(KC_PGDN); // 時計回りのとき、ページダウン。 Page down when clockwise.
        } else {
            tap_code(KC_PGUP); // 反時計回りのとき、ページアップ。 Page up when counterclockwise.
        }
    }
    return true;
}

void matrix_init_user(void) {
    // ユーザ初期化のための関数。 Initialization function for the user.
    user_config.raw = eeconfig_read_user();
    state = NONE;
    scroll_v_mouse_interval_counter = 0;
    scroll_h_mouse_interval_counter = 0;
    mouse_movement = 0;
    click_timer = timer_read();
}

void matrix_scan_user(void) {
    // ユーザスキャンのための関数。 Scanning function for the user.
    if (state == WAITING) {
        enable_click_layer();
    }
}

void keyboard_post_init_user(void) {
    // ユーザポスト初期化のための関数。 Post-initialization function for the user.
    user_config.raw = eeconfig_read_user();
}
void keyboard_post_init_user(void) {
    // ユーザポスト初期化のための関数。 Post-initialization function for the user.
    user_config.raw = eeconfig_read_user();
}

#ifdef OLED_ENABLE
static void render_logo(void) {
    static const char PROGMEM qmk_logo[] = {
        0x00, 0xC0, 0xE0, 0xF8, 0xFC, 0xFC, 0xFE, 0xFE, 0xFE, 0xFC, 0xFC, 0xF8, 0xE0, 0xC0, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0x00, 0xF8, 0xFE, 0xFF, 0xFF, 0xF8, 0xF0, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0x0F, 0x1F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x07, 0x00, 0x00,
        0xF0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x07,
        0x1F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x07, 0x00, 0x00, 0xFF, 0xFF, 0xFF
    };

    oled_write_raw_P(qmk_logo, sizeof(qmk_logo));
}

void oled_task_user(void) {
    if (is_clickable_mode()) {
        render_logo();
    } else {
        render_logo();
    }
}
#endif

///////////////////////////
/// miniZoneの実装 ここまで ///
///////////////////////////

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (index == 0) { /* 左のエンコーダの処理。 Handling the left encoder */
        if (clockwise) {
            tap_code(KC_VOLU); // 時計回りのとき、音量アップ。 Volume up when clockwise.
        } else {
            tap_code(KC_VOLD); // 反時計回りのとき、音量ダウン。 Volume down when counterclockwise.
        }
    } else if (index == 1) { /* 右のエンコーダの処理。 Handling the right encoder */
        if (clockwise) {
            tap_code(KC_PGDN); // 時計回りのとき、ページダウン。 Page down when clockwise.
        } else {
            tap_code(KC_PGUP); // 反時計回りのとき、ページアップ。 Page up when counterclockwise.
        }
    }
    return true;
}

void matrix_init_user(void) {
    // ユーザ初期化のための関数。 Initialization function for the user.
    user_config.raw = eeconfig_read_user();
    state = NONE;
    scroll_v_mouse_interval_counter = 0;
    scroll_h_mouse_interval_counter = 0;
    mouse_movement = 0;
    click_timer = timer_read();
}

void matrix_scan_user(void) {
    // ユーザスキャンのための関数。 Scanning function for the user.
    if (state == WAITING) {
        enable_click_layer();
    }
}
