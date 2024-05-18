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
}

// クリック用のレイヤーを無効にする。 Disable layers for clicks.
void disable_click_layer(void) {
    state = NONE;
    layer_off(click_layer);
    scroll_v_mouse_interval_counter = 0;
    scroll_h_mouse_interval_counter = 0;
}

// 自前の絶対数を返す関数。 Functions that return absolute numbers.
int16_t my_abs(int16_t num) {
    if (num < 0) {
        num = -num;
    }

    return num;
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

         default:
            if  (record->event.pressed) {
                
                if (state == CLICKING || state == SCROLLING)
                {
                    enable_click_layer();
                    return false;
                }
                
                for (int i = 0; i < sizeof(ignore_disable_mouse_layer_keys) / sizeof(ignore_disable_mouse_layer_keys[0]); i++) {
                    if (keycode == ignore_disable_mouse_layer_keys[i]) {
                        return true;
                    }
                }
                
                disable_click_layer();
            }
            break;
    }
    return true;
}

// クリックレイヤーをリセットするかどうかの判定。 Determining whether to reset the click layer
void determine_click_layer_reset(void) {
    if (state == CLICKABLE) {
        if (timer_elapsed(click_timer) >= to_reset_time) {
            disable_click_layer();
        }
    }
}

bool should_enable_click_layer(void) {
    return my_abs(mouse_movement) >= user_config.to_clickable_movement;  // return timer_elapsed(click_timer) >= user_config.to_clickable_time;
}

void update_click_layer_state(int16_t x, int16_t y) {
    mouse_movement += my_abs(x) + my_abs(y);

    if (state == WAITING && should_enable_click_layer()) {
        enable_click_layer();
    }
}

void update_scroll_intervals(int16_t x, int16_t y) {
    scroll_v_mouse_interval_counter += y;
    scroll_h_mouse_interval_counter += x;

    if (my_abs(scroll_v_mouse_interval_counter) >= scroll_v_threshold) {
        uint8_t scroll = scroll_v_mouse_interval_counter / scroll_v_threshold;
        report_mouse_t currentReport = pointing_device_get_report();

        if (user_config.mouse_scroll_v_reverse) {
            currentReport.v = -scroll;
        } else {
            currentReport.v = scroll;
        }

        pointing_device_set_report(currentReport);
        pointing_device_send();

        scroll_v_mouse_interval_counter = 0;
    }

    if (my_abs(scroll_h_mouse_interval_counter) >= scroll_h_threshold) {
        uint8_t scroll = scroll_h_mouse_interval_counter / scroll_h_threshold;
        report_mouse_t currentReport = pointing_device_get_report();

        if (user_config.mouse_scroll_h_reverse) {
            currentReport.h = -scroll;
        } else {
            currentReport.h = scroll;
        }

        pointing_device_set_report(currentReport);
        pointing_device_send();

        scroll_h_mouse_interval_counter = 0;
    }
}

void pointing_device_task(void) {
    report_mouse_t report = pointing_device_get_report();
    int16_t x = report.x;
    int16_t y = report.y;

    if (is_clickable_mode()) {
        determine_click_layer_reset();
    } else {
        update_click_layer_state(x, y);
    }

    if (state == SCROLLING) {
        update_scroll_intervals(x, y);

        report.x = 0;
        report.y = 0;
    }

    pointing_device_set_report(report);
    pointing_device_send();
}

///////////////
/// OLEDの設定 ///
///////////////
#ifdef OLED_ENABLE
static void render_layer_status(void) {
    oled_write_P(PSTR("Layer: "), false);

    switch (get_highest_layer(layer_state)) {
        case 0:
            oled_write_ln_P(PSTR("Default"), false);
            break;
        case 1:
            oled_write_ln_P(PSTR("Raise"), false);
            break;
        case 2:
            oled_write_ln_P(PSTR("Lower"), false);
            break;
        case 3:
            oled_write_ln_P(PSTR("Adjust"), false);
            break;
        case 4:
            oled_write_ln_P(PSTR("Nav"), false);
            break;
        case 5:
            oled_write_ln_P(PSTR("Mouse"), false);
            break;
        case 6:
            oled_write_ln_P(PSTR("Click"), false);
            break;
        default:
            oled_write_ln_P(PSTR("Unknown"), false);
    }
}

static void render_mod_status(uint8_t modifiers) {
    oled_write_P(PSTR("Mods: "), false);
    oled_write_P(PSTR(" "), false);

    oled_write_P(modifiers & MOD_MASK_SHIFT ? PSTR("S") : PSTR("-"), false);
    oled_write_P(modifiers & MOD_MASK_CTRL ? PSTR("C") : PSTR("-"), false);
    oled_write_P(modifiers & MOD_MASK_ALT ? PSTR("A") : PSTR("-"), false);
    oled_write_P(modifiers & MOD_MASK_GUI ? PSTR("G") : PSTR("-"), false);
}

bool oled_task_user(void) {
    oled_clear();

    render_layer_status();
    render_mod_status(get_mods());

    return false;
}
#endif



const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    LAYOUT_universal(
        KC_ESC, LT(4, KC_Q), KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_BSPC,
        KC_LCTL, KC_A, KC_S, LT(3, KC_D), KC_F, KC_G, KC_H, KC_J, LT(3, KC_K), KC_L, KC_ENT, KC_ENT,
        KC_LSFT, LSFT_T(KC_Z), LGUI_T(KC_X), KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, LCTL_T(KC_DOT), KC_BSPC, KC_BSPC, 
        KC_LSFT, KC_LCTL, KC_LGUI, LALT_T(KC_LNG2), LSFT_T(KC_TAB), LT(2, KC_SPC), LT(1, KC_LNG1), LT(1, KC_LNG1), KC_RGUI, KC_RCTL
    ),
    
    LAYOUT_universal(
        KC_TRNS, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_TRNS,
        KC_TRNS, LCTL_T(KC_EQL), KC_LBRC, KC_SLSH, KC_MINS, KC_INT1, KC_SCLN, KC_QUOT, KC_RBRC, KC_NUHS, KC_INT3, KC_TRNS,
        KC_TRNS, LSFT_T(KC_PLUS), KC_LCBR, KC_QUES, KC_UNDS, LSFT(KC_INT1), KC_COLN, KC_DQUO, KC_RCBR, LSFT(KC_NUHS), LSFT(KC_INT3),  KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ),
    
    LAYOUT_universal(
        KC_TRNS, KC_EXLM, KC_AT, KC_HASH, KC_DLR, KC_PERC, KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, LGUI(KC_INT3),  KC_TRNS,
        KC_TRNS, KC_PLUS, KC_LCBR, KC_QUES, KC_UNDS, LSFT(KC_INT1), KC_COLN, KC_DQUO, KC_RCBR, LSFT(KC_NUHS), LSFT(KC_INT3),  KC_TRNS,
        KC_TRNS, KC_LSFT, KC_LGUI, KC_LALT, KC_LNG2, KC_LSFT, KC_SPC, KC_LNG1, KC_TRNS, KC_TRNS, KC_DEL,  KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ),
    
    LAYOUT_universal(
        KC_TRNS, KC_ESC, KC_TAB, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_UP, KC_NO, KC_NO, KC_TRNS,
        KC_TRNS, KC_LCTL, KC_TRNS, KC_QUES, KC_EXLM, KC_NO, KC_NO, KC_LEFT, KC_DOWN, KC_RGHT, KC_NO, KC_TRNS,
        KC_TRNS, KC_LSFT, KC_LGUI, KC_LALT, KC_LNG2, KC_TRNS, KC_NO, KC_LNG1, KC_NO, KC_NO, KC_DEL, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ), 
    
    LAYOUT_universal(
        KC_TRNS, KC_NO, KC_TAB, KC_NO, KC_NO, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_TRNS,
        KC_TRNS, KC_NO, KC_NO, KC_NO, KC_NO, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_TRNS,
        KC_TRNS, KC_LSFT, KC_NO, KC_NO, KC_NO, KC_TRNS, KC_TRNS, KC_TRNS, KC_NO, MO(5), MO(6), KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ),
    
    LAYOUT_universal(
        KC_TRNS, RGB_TOG, RGB_MOD, RGB_HUI, RGB_SAI, RGB_VAI, CPI_I100, CPI_D100, KC_TO_CLICKABLE_INC, KC_TO_CLICKABLE_DEC, KC_NO, KBC_SAVE,
        KC_TRNS, RGB_M_P, RGB_M_B, RGB_M_R, RGB_M_SW, RGB_M_SN, CPI_I1K, CPI_D1K, KC_NO, KC_NO, KC_NO, KBC_RST,
        KC_TRNS, RGB_M_K, RGB_M_X, RGB_M_G, KC_NO, KC_NO, QK_BOOT, KC_NO, KC_NO, KC_NO, KC_NO, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ), 

    LAYOUT_universal(
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_MY_BTN2, KC_MY_SCR, KC_MY_BTN1, KC_TRNS, KC_TRNS, KC_MY_BTN1, KC_MY_SCR, KC_MY_BTN3, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    )
  
};
