/*
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

#include "quantum.h"
#ifdef SPLIT_KEYBOARD
#    include "transactions.h"
#endif

#include "keyball.h"
#include "drivers/pmw3360/pmw3360.h"

#include <string.h>

const uint8_t CPI_DEFAULT    = KEYBALL_CPI_DEFAULT / 100;
const uint8_t CPI_MAX        = pmw3360_MAXCPI + 1;
const uint8_t SCROLL_DIV_MAX = 7;

const uint16_t AML_TIMEOUT_MIN = 100;
const uint16_t AML_TIMEOUT_MAX = 1000;
const uint16_t AML_TIMEOUT_QU  = 50;   // Quantization Unit

static const char BL = '\xB0'; // Blank indicator character
static const char LFSTR_ON[] PROGMEM = "\xB2\xB3";
static const char LFSTR_OFF[] PROGMEM = "\xB4\xB5";

keyball_t keyball = {
    .this_have_ball = false,
    .that_enable    = false,
    .that_have_ball = false,

    .this_motion = {0},
    .that_motion = {0},

    .cpi_value   = 0,
    .cpi_changed = false,

    .scroll_mode = false,
    .scroll_div  = 0,

    .pressing_keys = { BL, BL, BL, BL, BL, BL, 0 },
};

//////////////////////////////////////////////////////////////////////////////
// Hook points

__attribute__((weak)) void keyball_on_adjust_layout(keyball_adjust_t v) {}

//////////////////////////////////////////////////////////////////////////////
// Static utilities

// add16 adds two int16_t with clipping.
static int16_t add16(int16_t a, int16_t b) {
    int16_t r = a + b;
    if (a >= 0 && b >= 0 && r < 0) {
        r = 32767;
    } else if (a < 0 && b < 0 && r >= 0) {
        r = -32768;
    }
    return r;
}

// divmod16 divides *v by div, returns the quotient, and assigns the remainder
// to *v.
static int16_t divmod16(int16_t *v, int16_t div) {
    int16_t r = *v / div;
    *v -= r * div;
    return r;
}

// clip2int8 clips an integer fit into int8_t.
static inline int8_t clip2int8(int16_t v) {
    return (v) < -127 ? -127 : (v) > 127 ? 127 : (int8_t)v;
}

#ifdef OLED_ENABLE
static const char *format_4d(int8_t d) {
    static char buf[5] = {0}; // max width (4) + NUL (1)
    char        lead   = ' ';
    if (d < 0) {
        d    = -d;
        lead = '-';
    }
    buf[3] = (d % 10) + '0';
    d /= 10;
    if (d == 0) {
        buf[2] = lead;
        lead   = ' ';
    } else {
        buf[2] = (d % 10) + '0';
        d /= 10;
    }
    if (d == 0) {
        buf[1] = lead;
        lead   = ' ';
    } else {
        buf[1] = (d % 10) + '0';
        d /= 10;
    }
    buf[0] = lead;
    return buf;
}

static char to_1x(uint8_t x) {
    x &= 0x0f;
    return x < 10 ? x + '0' : x + 'a' - 10;
}
#endif

static void add_cpi(int8_t delta) {
    int16_t v = keyball_get_cpi() + delta;
    keyball_set_cpi(v < 1 ? 1 : v);
}

static void add_scroll_div(int8_t delta) {
    int8_t v = keyball_get_scroll_div() + delta;
    keyball_set_scroll_div(v < 1 ? 1 : v);
}

//////////////////////////////////////////////////////////////////////////////
// Speed multiplier
static float speed_multiplier = 1.0f;

void set_speed_multiplier(float multiplier) {
    speed_multiplier = multiplier;
}

float get_speed_multiplier(void) {
    return speed_multiplier;
}

//////////////////////////////////////////////////////////////////////////////
// Pointing device driver

#if KEYBALL_MODEL == 46
void keyboard_pre_init_kb(void) {
    keyball.this_have_ball = pmw3360_init();
    keyboard_pre_init_user();
}
#endif

void pointing_device_driver_init(void) {
#if KEYBALL_MODEL != 46
    keyball.this_have_ball = pmw3360_init();
#endif
    if (keyball.this_have_ball) {
#if defined(KEYBALL_PMW3360_UPLOAD_SROM_ID)
#    if KEYBALL_PMW3360_UPLOAD_SROM_ID == 0x04
        pmw3360_srom_upload(pmw3360_srom_0x04);
#    elif KEYBALL_PMW3360_UPLOAD_SROM_ID == 0x81
        pmw3360_srom_upload(pmw3360_srom_0x81);
#    else
#        error Invalid value for KEYBALL_PMW3360_UPLOAD_SROM_ID. Please choose 0x04 or 0x81 or disable it.
#    endif
#endif
        pmw3360_cpi_set(CPI_DEFAULT - 1);
    }
}

uint16_t pointing_device_driver_get_cpi(void) {
    return keyball_get_cpi();
}

void pointing_device_driver_set_cpi(uint16_t cpi) {
    keyball_set_cpi(cpi);
}

__attribute__((weak)) void keyball_on_apply_motion_to_mouse_move(keyball_motion_t *m, report_mouse_t *r, bool is_left) {
#if KEYBALL_MODEL == 61 || KEYBALL_MODEL == 39 || KEYBALL_MODEL == 147 || KEYBALL_MODEL == 44
    r->x = clip2int8(m->y * speed_multiplier);
    r->y = clip2int8(m->x * speed_multiplier);
    if (is_left) {
        r->x = -r->x;
        r->y = -r->y;
    }
#elif KEYBALL_MODEL == 46
    r->x = clip2int8(m->x * speed_multiplier);
    r->y = -clip2int8(m->y * speed_multiplier);
#else
#    error("unknown Keyball model")
#endif
    // clear motion
    m->x = 0;
    m->y = 0;
}

__attribute__((weak)) void keyball_on_apply_motion_to_mouse_scroll(keyball_motion_t *m, report_mouse_t *r, bool is_left) {
    // consume motion of trackball.
    int16_t div = 1 << (keyball_get_scroll_div() - 1);
    int16_t x = divmod16(&m->x, div);
    int16_t y = divmod16(&m->y, div);

    // apply to mouse report.
#if KEYBALL_MODEL == 61 || KEYBALL_MODEL == 39 || KEYBALL_MODEL == 147 || KEYBALL_MODEL == 44
    r->h = clip2int8(y * speed_multiplier);
    r->v = -clip2int8(x * speed_multiplier);
    if (is_left) {
        r->h = -r->h;
        r->v = -r->v;
    }
#elif KEYBALL_MODEL == 46
    r->h = clip2int8(x * speed_multiplier);
    r->v = clip2int8(y * speed_multiplier);
#else
#    error("unknown Keyball model")
#endif

    // Scroll snapping
#if KEYBALL_SCROLLSNAP_ENABLE == 1
    // Old behavior up to 1.3.2)
    uint32_t now = timer_read32();
    if (r->h != 0 || r->v != 0) {
        keyball.scroll_snap_last = now;
    } else if (TIMER_DIFF_32(now, keyball.scroll_snap_last) >= KEYBALL_SCROLLSNAP_RESET_TIMER) {
        keyball.scroll_snap_tension_h = 0;
    }
    if (abs(keyball.scroll_snap_tension_h) < KEYBALL_SCROLLSNAP_TENSION_THRESHOLD) {
        keyball.scroll_snap_tension_h += y;
        r->h = 0;
    }
#elif KEYBALL_SCROLLSNAP_ENABLE == 2
    // ScrollSnap implementation by @EklipZ on 2021-12-17 (From QMK 0.16.1)
    static int16_t scroll_snap_tension_h = 0;
    static int16_t scroll_snap_tension_v = 0;
    // When the timer has expired, reset the tensions
    uint32_t now = timer_read32();
    if (TIMER_DIFF_32(now, keyball.scroll_snap_last) >= KEYBALL_SCROLLSNAP_RESET_TIMER) {
        scroll_snap_tension_h = 0;
        scroll_snap_tension_v = 0;
    }
    if (r->h != 0 || r->v != 0) {
        keyball.scroll_snap_last = now;
    }
    // When tension exceeds the threshold, apply scroll
    if (abs(scroll_snap_tension_h) < KEYBALL_SCROLLSNAP_TENSION_THRESHOLD) {
        scroll_snap_tension_h += x;
        r->h = 0;
    }
    if (abs(scroll_snap_tension_v) < KEYBALL_SCROLLSNAP_TENSION_THRESHOLD) {
        scroll_snap_tension_v += y;
        r->v = 0;
    }
#endif
}

static void motion_to_mouse(keyball_motion_t *m, report_mouse_t *r, bool is_left) {
    if (keyball.scroll_mode) {
        keyball_on_apply_motion_to_mouse_scroll(m, r, is_left);
    } else {
        keyball_on_apply_motion_to_mouse_move(m, r, is_left);
    }
}

void pointing_device_task(void) {
    if (!keyball.this_have_ball && !keyball.that_have_ball) return;

    report_mouse_t report = pointing_device_get_report();

    keyball_motion_t motion = keyball.this_motion;

#ifdef SPLIT_KEYBOARD
    if (keyball.that_have_ball) {
        keyball_motion_t another_motion = {0};
        if (keyball.that_enable) {
            transaction_rpc_recv(KEYBALL_RPC_ID_MOTION, sizeof(another_motion), (uint8_t *)&another_motion);
        }
        if (is_keyboard_master()) {
            if (another_motion.x != 0 || another_motion.y != 0) {
                another_motion.x = -another_motion.x;
                another_motion.y = -another_motion.y;
            }
        }
        motion.x = add16(motion.x, another_motion.x);
        motion.y = add16(motion.y, another_motion.y);
    }
#endif

    motion_to_mouse(&motion, &report, is_keyboard_left());

    pointing_device_set_report(report);

    keyball.this_motion.x = 0;
    keyball.this_motion.y = 0;
}

//////////////////////////////////////////////////////////////////////////////
// Keyball API

bool keyball_get_this_have_ball(void) {
    return keyball.this_have_ball;
}

bool keyball_get_that_have_ball(void) {
    return keyball.that_have_ball;
}

bool keyball_get_that_enable(void) {
    return keyball.that_enable;
}

void keyball_set_that_enable(bool enable) {
    keyball.that_enable = enable;
}

keyball_motion_t keyball_get_this_motion(void) {
    return keyball.this_motion;
}

keyball_motion_t keyball_get_that_motion(void) {
    return keyball.that_motion;
}

int16_t keyball_get_cpi(void) {
    return keyball.cpi_value;
}

void keyball_set_cpi(int16_t cpi) {
    keyball.cpi_value = cpi < CPI_MAX ? cpi : CPI_MAX - 1;
    pmw3360_cpi_set(keyball.cpi_value);
}

void keyball_cpi_cycle(void) {
    keyball_set_cpi((keyball_get_cpi() % CPI_MAX) + 1);
}

bool keyball_get_scroll_mode(void) {
    return keyball.scroll_mode;
}

void keyball_set_scroll_mode(bool scroll_mode) {
    keyball.scroll_mode = scroll_mode;
}

int8_t keyball_get_scroll_div(void) {
    return keyball.scroll_div + 1;
}

void keyball_set_scroll_div(int8_t scroll_div) {
    keyball.scroll_div = scroll_div - 1 < SCROLL_DIV_MAX ? scroll_div - 1 : SCROLL_DIV_MAX - 1;
}

void keyball_adjust(keyball_adjust_t v) {
    keyball_on_adjust_layout(v);
}

void keyball_set_pressing_key(uint8_t index, char v) {
    if (index < sizeof(keyball.pressing_keys) - 1) {
        keyball.pressing_keys[index] = v;
    }
}

void keyball_clear_pressing_keys(void) {
    memset(keyball.pressing_keys, BL, sizeof(keyball.pressing_keys) - 1);
}

const char *keyball_get_pressing_keys(void) {
    return keyball.pressing_keys;
}

#ifdef OLED_ENABLE
void keyball_oled_status(void) {
    // default DPI = 4-digit integer
    oled_write_ln("CPI:", false);
    oled_write_ln(format_4d(keyball_get_cpi()), false);
    // scroll div = 1x-7x
    oled_write_ln("SCR:", false);
    oled_write_char(to_1x(keyball_get_scroll_div()), false);
    // left hand mouse
    oled_write_ln("LFT:", false);
    oled_write_ln(keyball_get_that_enable() ? "ON" : "OFF", false);
    // left hand mouse
    oled_write_ln("SPD:", false);
    oled_write_ln(format_4d((int8_t)(speed_multiplier * 100)), false);
    // pressing keys = 6 chars
    oled_write_ln("KEYS:", false);
    oled_write_ln(keyball_get_pressing_keys(), false);
}
#endif
