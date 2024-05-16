/*
 * This is the C configuration file for the keymap.
 *
 * Copyright 2022 @Yowkees
 * Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef RGBLIGHT_ENABLE
    // Uncomment the effect you wish to enable
    // #define RGBLIGHT_EFFECT_BREATHING
    // #define RGBLIGHT_EFFECT_RAINBOW_MOOD
    #define RGBLIGHT_EFFECT_RAINBOW_SWIRL
    // #define RGBLIGHT_EFFECT_SNAKE
    // #define RGBLIGHT_EFFECT_KNIGHT
    // #define RGBLIGHT_EFFECT_CHRISTMAS
    // #define RGBLIGHT_EFFECT_STATIC_GRADIENT
    // #define RGBLIGHT_EFFECT_RGB_TEST
    // #define RGBLIGHT_EFFECT_ALTERNATING
    // #define RGBLIGHT_EFFECT_TWINKLE
#endif

// Key code tapping settings
#define TAP_CODE_DELAY 5

// Dynamic keymap layer settings
#define DYNAMIC_KEYMAP_LAYER_COUNT 7

// Keyball settings
#define KEYBALL_CPI_DEFAULT 1100        // Default CPI (Counts Per Inch) for the PMW3360DM optical sensor
#define KEYBALL_SCROLL_DIV_DEFAULT 4    // Default scroll speed divisor
#define KEYBALL_SCROLLSNAP_ENABLE 1     // Enable scroll snapping

// Split keyboard settings
#define SPLIT_LAYER_STATE_ENABLE        // Send layer state to the secondary keyboard

// Modifier key settings
#define KEYBOARD_MOD_PACKET_DELAY 50    // Delay to prevent modifier key dropouts in RDP (https://github.com/qmk/qmk_firmware/pull/19405)

// RGB light default settings
#define RGBLIGHT_DEFAULT_HUE 0          // Default backlight hue
#define RGBLIGHT_DEFAULT_SAT 0          // Default backlight saturation
#define RGBLIGHT_DEFAULT_VAL 70         // Default backlight brightness

// Tapping term setting
#define TAPPING_TERM 145
