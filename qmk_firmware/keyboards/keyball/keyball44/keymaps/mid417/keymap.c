/*
Copyright 2022 @Yowkees
Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)

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

// コード表
// 【KBC_RST: 0x5DA5】Keyball 設定のリセット
// 【KBC_SAVE: 0x5DA6】現在の Keyball 設定を EEPROM に保存します
// 【CPI_I100: 0x5DA7】CPI を 100 増加させます(最大:12000)
// 【CPI_D100: 0x5DA8】CPI を 100 減少させます(最小:100)
// 【CPI_I1K: 0x5DA9】CPI を 1000 増加させます(最大:12000)
// 【CPI_D1K: 0x5DAA】CPI を 1000 減少させます(最小:100)
// 【SCRL_TO: 0x5DAB】タップごとにスクロールモードの ON/OFF を切り替えます
// 【SCRL_MO: 0x5DAC】キーを押している間、スクロールモードになります
// 【SCRL_DVI: 0x5DAD】スクロール除数を１つ上げます(max D7 = 1/128)← 最もスクロール遅い
// 【SCRL_DVD: 0x5DAE】スクロール除数を１つ下げます(min D0 = 1/1)← 最もスクロール速い

////////////////////////////////////
///
/// 自動マウスレイヤーの実装 ここから
/// 参考にさせていただいたページ
/// https://zenn.dev/takashicompany/articles/69b87160cda4b9
/// https://twitter.com/d_kamiichi
///
////////////////////////////////////

// Custom keycodes
enum custom_keycodes {
    KC_MY_BTN1 = KEYBALL_SAFE_RANGE,
    KC_MY_BTN2,
    KC_MY_BTN3,
    KC_MY_SCR,
};

// Click states
enum click_state {
    NONE = 0,
    WAITING,
    CLICKABLE,
    CLICKING,
    SCROLLING,
};

// State variables
enum click_state state;
uint16_t click_timer;
uint16_t to_reset_time = 800;
const int16_t to_clickable_movement = 2;
const uint16_t click_layer = 6;
const uint16_t ignore_disable_mouse_layer_keys[] = {
    KC_LGUI, KC_LCTL, KC_LALT, KC_LSFT, KC_RGUI, KC_RCTL, KC_RALT, KC_RSFT
};

// Helper functions
void enable_click_layer(void) {
    layer_on(click_layer);
    click_timer = timer_read();
    state = CLICKABLE;
}

void disable_click_layer(void) {
    state = NONE;
    layer_off(click_layer);
}

int16_t my_abs(int16_t num) {
    return (num < 0) ? -num : num;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_MY_BTN1:
        case KC_MY_BTN2:
        case KC_MY_BTN3: {
            report_mouse_t currentReport = pointing_device_get_report();
            uint8_t btn = 1 << (keycode - KC_MY_BTN1);

            if (record->event.pressed) {
                currentReport.buttons |= btn;
                state = CLICKING;
            } else {
                currentReport.buttons &= ~btn;
                enable_click_layer();
            }

            pointing_device_set_report(currentReport);
            pointing_device_send();
            return false;
        }
        case KC_MY_SCR:
            if (record->event.pressed) {
                keyball_set_scroll_mode(true);
                state = SCROLLING;
            } else {
                keyball_set_scroll_mode(false);
                state = NONE;
                enable_click_layer();
            }
            return false;
        default:
            if (record->event.pressed) {
                for (int i = 0; i < sizeof(ignore_disable_mouse_layer_keys) / sizeof(ignore_disable_mouse_layer_keys[0]); i++) {
                    if (keycode == ignore_disable_mouse_layer_keys[i]) {
                        click_timer = timer_read();
                        return true;
                    }
                }
                disable_click_layer();
            }
            return true;
    }
}

void matrix_scan_user(void) {
    report_mouse_t currentReport = pointing_device_get_report();

    if (state == WAITING) {
        if (my_abs(currentReport.x) > to_clickable_movement || my_abs(currentReport.y) > to_clickable_movement) {
            enable_click_layer();
        }
    } else if (state == CLICKABLE) {
        if (timer_elapsed(click_timer) > to_reset_time) {
            disable_click_layer();
        }
    }
}

void pointing_device_init_user(void) {
    state = NONE;
}

void pointing_device_task_user(void) {
    report_mouse_t currentReport = pointing_device_get_report();

    if (state == CLICKING) {
        enable_click_layer();
    }

    pointing_device_set_report(currentReport);
    pointing_device_send();
}

// Define layer names
enum layers {
    _BASE,
    _LAYER1,
    _LAYER2,
    _LAYER3,
    _LAYER4,
    _LAYER5,
    _MOUSE
};

// Define the keymaps for each layer
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_ESC,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,          KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_TAB,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,          KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_ENT,
        KC_LSFT,   KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,          KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT,
        KC_LCTL,   KC_LGUI, KC_LALT, KC_SPC,  MO(_LAYER1),          MO(_LAYER2), KC_RALT, MO(_LAYER3), MO(_LAYER4), MO(_MOUSE)
    ),
    [_LAYER1] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    ),
    [_LAYER2] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    ),
    [_LAYER3] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    ),
    [_LAYER4] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    ),
    [_LAYER5] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    ),
    [_MOUSE] = LAYOUT(
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,          _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,                   _______, _______, _______, _______, _______
    )
};
