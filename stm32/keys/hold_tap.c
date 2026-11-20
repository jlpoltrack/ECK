#include "hold_tap.h"
#include "config.h"
#include "keys.h"
#include "layers.h"
#include "task.h"
#include <string.h>
#ifdef VIAL_ENABLE
#include "usb.h"
#include "vial.h"
#endif

#ifndef TAPPING_TERM_DEFAULT
#define TAPPING_TERM_DEFAULT 200
#endif

#ifndef QUICK_TAP_TERM
#define QUICK_TAP_TERM 0
#endif

static HTTracker ht_trackers[MAX_HT_TRACKERS];
static HTLastRelease s_last_release;
static uint8_t s_active_ht_count = 0;

__attribute__((weak)) uint16_t hold_tap_config_value(uint8_t id) {
    switch (id) {
    case 0: return TAPPING_TERM_DEFAULT;
    case 1: return PERMISSIVE_HOLD_ENABLED;
    case 3: return QUICK_TAP_TERM;
    default: return 0;
    }
}

extern void keyboard_send_key(uint16_t keycode, bool pressed);
extern void keyboard_send_modifiers(uint8_t mod_mask, bool pressed);
extern void oneshot_send_lazy_mods(void);
extern void oneshot_on_tap_key(void);

#ifdef VIAL_ENABLE
// A tap, shifted while Caps Word is on
static void tap_key(uint8_t kc) {
    uint8_t mods = vial_caps_word_mods(kc);
    if (mods) {
        usb_process_key_with_modifiers(mods, kc, true);
        usb_process_key_with_modifiers(mods, kc, false);
        return;
    }
    keyboard_send_key(kc, true);
    keyboard_send_key(kc, false);
}
#endif

void hold_tap_init(void) {
    memset(ht_trackers, 0, sizeof(ht_trackers));
    memset(&s_last_release, 0, sizeof(s_last_release));
    s_active_ht_count = 0;
}

bool hold_tap_has_active(void) {
    return s_active_ht_count > 0;
}

/* Hold modifiers as a mask: keymap HT() may name one modifier by keycode (0xE0..0xE7); MT() holds a mask. */
static uint8_t ht_mod_mask(uint32_t keycode) {
    uint8_t target = (keycode >> 8) & 0xFF;
    if (!(keycode & HT_MODS_FLAG) && target >= 0xE0 && target <= 0xE7) return (uint8_t)(1U << (target - 0xE0));
    return target;
}

static void activate_hold(HTTracker *tracker) {
    if (tracker->state == HT_STATE_PRESSED && s_active_ht_count > 0) {
        s_active_ht_count--;
    }
    tracker->state = HT_STATE_HOLD;
    uint8_t target = (tracker->keycode >> 8) & 0xFF;
    if (HT_IS_LAYER(tracker->keycode))
        layers_on(target);
    else
        keyboard_send_modifiers(ht_mod_mask(tracker->keycode), true);
}

void hold_tap_permissive_resolve(uint8_t except_row, uint8_t except_col) {
#ifdef VIAL_ENABLE
    for (int i = 0; i < MAX_HT_TRACKERS; i++)
        if (ht_trackers[i].row != except_row || ht_trackers[i].col != except_col) ht_trackers[i].interrupted = true;
#endif
    if (!hold_tap_config_value(1)) return;
    for (int i = 0; i < MAX_HT_TRACKERS; i++) {
        if (ht_trackers[i].state == HT_STATE_PRESSED &&
            (ht_trackers[i].row != except_row || ht_trackers[i].col != except_col)) {
            activate_hold(&ht_trackers[i]);
        }
    }
}

TickType_t hold_tap_check_timeouts(TickType_t now) {
    if (s_active_ht_count == 0) {
        return portMAX_DELAY;
    }
    TickType_t min_remaining = portMAX_DELAY;
    for (int i = 0; i < MAX_HT_TRACKERS; i++) {
        if (ht_trackers[i].state == HT_STATE_PRESSED) {
            TickType_t elapsed = now - ht_trackers[i].press_time;
            if (elapsed >= ht_trackers[i].timeout_ticks) {
                activate_hold(&ht_trackers[i]);
            } else {
                TickType_t rem = ht_trackers[i].timeout_ticks - elapsed;
                if (rem < min_remaining)
                    min_remaining = rem;
            }
        }
    }
    return min_remaining;
}

bool hold_tap_process_event(uint8_t row, uint8_t col, uint32_t key, bool pressed) {
    if ((key & 0xFF000000) != KIND_HT)
        return false;

    TickType_t now = xTaskGetTickCount();

    if (pressed) {
        // Quick-Tap / Auto-repeat tap detection:
        // If the exact same Hold-Tap key was recently tapped and released within QUICK_TAP_TERM,
        // treat this press as a continuous tap hold (repeating the tap keycode).
        uint16_t quick_tap_ms = hold_tap_config_value(3);
        if (s_last_release.valid &&
            quick_tap_ms &&
            s_last_release.row == row &&
            s_last_release.col == col &&
            s_last_release.keycode == key &&
            (now - s_last_release.release_time) <= pdMS_TO_TICKS(quick_tap_ms)) {

            s_last_release.valid = false;
            for (int i = 0; i < MAX_HT_TRACKERS; i++) {
                if (ht_trackers[i].state == HT_STATE_IDLE) {
                    ht_trackers[i] = (HTTracker){.row = row,
                                                 .col = col,
                                                 .keycode = key,
                                                 .press_time = now,
                                                 .timeout_ticks = 0,
                                                 .state = HT_STATE_REPEAT};
                    uint8_t kc = key & 0xFF;
                    oneshot_send_lazy_mods();
                    keyboard_send_key(kc, true);
                    return true;
                }
            }
        }
        s_last_release.valid = false;

        for (int i = 0; i < MAX_HT_TRACKERS; i++) {
            if (ht_trackers[i].state == HT_STATE_IDLE) {
                uint32_t ms = (key >> 16) & 0x7F;
                ht_trackers[i] = (HTTracker){.row = row,
                                             .col = col,
                                             .keycode = key,
                                             .press_time = now,
                                             .timeout_ticks = pdMS_TO_TICKS(ms ? ms : hold_tap_config_value(0)),
                                             .state = HT_STATE_PRESSED};
                s_active_ht_count++;
                break;
            }
        }
    } else {
        for (int i = 0; i < MAX_HT_TRACKERS; i++) {
            if (ht_trackers[i].state != HT_STATE_IDLE && ht_trackers[i].row == row && ht_trackers[i].col == col) {
                uint8_t target = (ht_trackers[i].keycode >> 8) & 0xFF;
                uint8_t kc = ht_trackers[i].keycode & 0xFF;

                if (ht_trackers[i].state == HT_STATE_REPEAT) {
                    keyboard_send_key(kc, false);
                    oneshot_on_tap_key();
                    s_last_release.valid = false;
                } else if (ht_trackers[i].state == HT_STATE_PRESSED) {
                    if (s_active_ht_count > 0) {
                        s_active_ht_count--;
                    }
                    oneshot_send_lazy_mods();
#ifdef VIAL_ENABLE
                    tap_key(kc);
#else
                    keyboard_send_key(kc, true);
                    keyboard_send_key(kc, false);
#endif
                    oneshot_on_tap_key();

                    // Record tap release for potential quick-tap repeat
                    s_last_release.row = row;
                    s_last_release.col = col;
                    s_last_release.keycode = ht_trackers[i].keycode;
                    s_last_release.release_time = now;
                    s_last_release.valid = true;
                } else if (ht_trackers[i].state == HT_STATE_HOLD) {
                    if (HT_IS_LAYER(ht_trackers[i].keycode))
                        layers_off(target);
                    else
                        keyboard_send_modifiers(ht_mod_mask(ht_trackers[i].keycode), false);
                    s_last_release.valid = false;
#ifdef VIAL_ENABLE
                    if (!ht_trackers[i].interrupted && (vial_data.tapping & VIAL_TAPPING_RETRO)) tap_key(kc);
#endif
                }
                ht_trackers[i].state = HT_STATE_IDLE;
                break;
            }
        }
    }
    return true;
}
