/* Vial's features at run time: tap dance, combos, key overrides, (alt) repeat key, Caps Word,
 * Layer Lock, Grave Escape and QMK's magic swaps. Entries come from vial_data (via/vial.c).
 * Keys a feature presses go through keyboard_virtual_press(), which returns the key as pressed;
 * the feature keeps it and releases exactly that, so edits or state changes can't strand a key. */
#include "ec_settings.h"
#include "hold_tap.h"
#include "keyboard.h"
#include "keys.h"
#include "layers.h"
#include "task.h"
#include "usb.h"
#include "via.h"
#include "vial.h"
#include <string.h>

#define CAPS_WORD_IDLE_MS 5000U
// Virtual positions (columns of KEYBOARD_VIRTUAL_ROW) for keys the features press
enum {
    SLOT_TD,
    SLOT_COMBO = SLOT_TD + VIAL_TAP_DANCE_ENTRIES,
    SLOT_KO = SLOT_COMBO + VIAL_COMBO_ENTRIES,
    SLOT_REPEAT,
    SLOT_ALT_REPEAT,
};
_Static_assert(SLOT_ALT_REPEAT < VIAL_SLOT_MACRO, "virtual slots overlap");

static bool s_combos_on = true, s_ko_on = true;

static uint32_t press_via(uint8_t slot, uint16_t via_key) {
    return via_key ? keyboard_virtual_press(slot, from_via_keycode(via_key)) : 0;
}
static void release(uint8_t slot, uint32_t *key) {
    uint32_t k = *key;
    *key = 0; // first: a release that re-enters must not release it again
    if (k) keyboard_virtual_release(slot, k);
}

/* ---------------- Tap dance (outcomes as vial-qmk's) ---------------- */
static struct {
    uint8_t count;
    bool pressed, finished, interrupted;
    uint32_t down; // key held by the outcome, released with the tap-dance key
    TickType_t deadline;
} tds[VIAL_TAP_DANCE_ENTRIES];

static void td_down(uint8_t i, uint16_t kc) { tds[i].down = press_via((uint8_t)(SLOT_TD + i), kc); }
static void td_tap(uint8_t i, uint16_t kc) {
    uint32_t k = press_via((uint8_t)(SLOT_TD + i), kc);
    release((uint8_t)(SLOT_TD + i), &k);
}

static void td_reset(uint8_t i) {
    release((uint8_t)(SLOT_TD + i), &tds[i].down);
    memset(&tds[i], 0, sizeof(tds[i]));
}

static void td_finish(uint8_t i) {
    vial_td_t e = vial_data.td[i];
    tds[i].finished = true; // before any press: those presses interrupt other dances, not this one
    uint8_t n = tds[i].count;
    bool held = tds[i].pressed && !tds[i].interrupted;
    if (n == 1 && !held) {
        td_down(i, e.on_tap);
    } else if (n == 1) {
        td_down(i, e.on_hold ? e.on_hold : e.on_tap);
    } else if (n == 2 && tds[i].interrupted) {
        td_tap(i, e.on_tap);
        td_down(i, e.on_tap);
    } else if (n == 2 && held) {
        if (e.on_tap_hold) td_down(i, e.on_tap_hold);
        else if (e.on_tap) { td_tap(i, e.on_tap); td_down(i, e.on_hold ? e.on_hold : e.on_tap); }
        else td_down(i, e.on_hold);
    } else if (n == 2) {
        if (e.on_double_tap) td_down(i, e.on_double_tap);
        else { td_tap(i, e.on_tap); td_down(i, e.on_tap); }
    } // 3 or more: the taps were sent as they came
    if (!tds[i].pressed) td_reset(i);
}

static void td_event(uint8_t i, bool pressed, TickType_t now) {
    if (pressed) {
        if (tds[i].finished) td_reset(i);
        uint8_t n = ++tds[i].count;
        tds[i].pressed = true;
        tds[i].deadline = now + pdMS_TO_TICKS(vial_data.td[i].tapping_term ? vial_data.td[i].tapping_term
                                                                             : hold_tap_config_value(0));
        uint16_t tap = vial_data.td[i].on_tap;
        if (n == 3) { td_tap(i, tap); td_tap(i, tap); td_tap(i, tap); }
        else if (n > 3) td_tap(i, tap);
        return;
    }
    if (!tds[i].count) return;
    tds[i].pressed = false;
    if (tds[i].finished) { td_reset(i); return; }
    // vial-qmk finishes early when no later outcome is possible
    vial_td_t e = vial_data.td[i];
    if ((tds[i].count == 1 && e.on_tap && e.on_hold && !e.on_double_tap && !e.on_tap_hold) ||
        (tds[i].count == 2 && e.on_double_tap))
        td_finish(i);
}

// Another key went down: pending dances finish as interrupted (QMK: taps, not holds)
static void td_interrupt(int except) {
    for (uint8_t i = 0; i < VIAL_TAP_DANCE_ENTRIES; i++) {
        if ((int)i == except || !tds[i].count || tds[i].finished) continue;
        tds[i].interrupted = true;
        td_finish(i);
    }
}

/* ---------------- Combos ---------------- */
static struct { uint8_t row, col; uint16_t kc; } cb_buf[4];
static uint8_t cb_n;
static TickType_t cb_deadline;
// A fired combo: its key positions, which are still down, and the output as pressed
static struct { uint8_t row[4], col[4], n, down, idx; uint32_t out; } cb_active[2];

static uint8_t combo_len(const vial_combo_t *c) {
    uint8_t n = 0;
    for (unsigned i = 0; i < 4; i++) n += c->input[i] != 0;
    return n;
}
static bool combo_valid(const vial_combo_t *c) { return c->output && combo_len(c) >= 2; }
static bool combo_has(const vial_combo_t *c, uint16_t kc) {
    for (unsigned i = 0; i < 4; i++) if (c->input[i] && c->input[i] == kc) return true;
    return false;
}
// Every buffered key is an input of combo i
static bool combo_covers(unsigned i) {
    const vial_combo_t *c = &vial_data.combo[i];
    if (!combo_valid(c)) return false;
    for (unsigned k = 0; k < cb_n; k++) if (!combo_has(c, cb_buf[k].kc)) return false;
    return true;
}

// Every input of combo i is buffered (two keys with the same code don't count twice)
static bool combo_complete(unsigned i) {
    const vial_combo_t *c = &vial_data.combo[i];
    if (combo_len(c) != cb_n) return false;
    for (unsigned j = 0; j < 4; j++) {
        if (!c->input[j]) continue;
        bool found = false;
        for (unsigned k = 0; k < cb_n && !found; k++) found = cb_buf[k].kc == c->input[j];
        if (!found) return false;
    }
    return true;
}

static void combo_flush(void) {
    uint8_t n = cb_n;
    cb_n = 0;
    for (unsigned k = 0; k < n; k++) keyboard_handle_press(cb_buf[k].row, cb_buf[k].col);
}

static void combo_fire(unsigned idx) {
    for (unsigned a = 0; a < 2; a++) {
        if (cb_active[a].n) continue;
        cb_active[a].n = cb_n;
        cb_active[a].down = (uint8_t)((1U << cb_n) - 1U);
        cb_active[a].idx = (uint8_t)idx;
        for (unsigned k = 0; k < cb_n; k++) { cb_active[a].row[k] = cb_buf[k].row; cb_active[a].col[k] = cb_buf[k].col; }
        cb_n = 0;
        cb_active[a].out = press_via((uint8_t)(SLOT_COMBO + idx), vial_data.combo[idx].output);
        return;
    }
    combo_flush(); // both trackers busy: plain keys
}

// Fire a complete combo unless a longer one could still match (then wait, unless finishing)
static bool combo_try(bool finishing) {
    int complete = -1;
    bool longer = false;
    for (unsigned i = 0; i < VIAL_COMBO_ENTRIES; i++) {
        if (!combo_covers(i)) continue;
        if (combo_complete(i)) complete = (int)i;
        else longer = true;
    }
    if (complete >= 0 && (!longer || finishing)) { combo_fire((unsigned)complete); return true; }
    return false;
}

// End the wait: fire what is complete, else send the buffered keys as typed
static void combo_resolve(void) {
    if (cb_n && !combo_try(true)) combo_flush();
}

bool vial_combo_on_press(uint8_t row, uint8_t col, TickType_t now) {
    if (!s_combos_on) return false;
    uint16_t kc = to_via_keycode(layers_lookup_key(row, col));
    bool any = false;
    for (unsigned i = 0; i < VIAL_COMBO_ENTRIES && !any; i++)
        any = combo_valid(&vial_data.combo[i]) && combo_has(&vial_data.combo[i], kc);
    if (!any) {
        combo_resolve();
        return false;
    }
    if (cb_n == 4) combo_resolve();
    cb_buf[cb_n].row = row; cb_buf[cb_n].col = col; cb_buf[cb_n].kc = kc;
    cb_n++;
    bool candidate = false;
    for (unsigned i = 0; i < VIAL_COMBO_ENTRIES && !candidate; i++) candidate = combo_covers(i);
    if (!candidate) { // the new key breaks the combo: settle the others, start again from it
        cb_n--;
        combo_resolve();
        cb_buf[0].row = row; cb_buf[0].col = col; cb_buf[0].kc = kc;
        cb_n = 1;
    }
    if (cb_n == 1) cb_deadline = now + pdMS_TO_TICKS(vial_data.combo_term);
    combo_try(false);
    return true;
}

bool vial_combo_on_release(uint8_t row, uint8_t col) {
    combo_resolve(); // any release ends the wait, so earlier presses keep their order
    for (unsigned a = 0; a < 2; a++) {
        for (unsigned k = 0; k < cb_active[a].n; k++) {
            if (cb_active[a].row[k] != row || cb_active[a].col[k] != col || !(cb_active[a].down & (1U << k))) continue;
            cb_active[a].down &= (uint8_t)~(1U << k);
            release((uint8_t)(SLOT_COMBO + cb_active[a].idx), &cb_active[a].out); // first key up releases it
            if (!cb_active[a].down) cb_active[a].n = 0;
            return true;
        }
    }
    return false;
}

/* ---------------- Key overrides ---------------- */
static struct { bool active, keep; uint8_t row, col; uint32_t sent; } ko;

static uint8_t four_mods(uint8_t m) { return (uint8_t)((m | (m >> 4)) & 0x0F); }

/* QMK: a trigger mod set on one hand needs that hand; set on both, either hand will do.
 * With one_mod, any one of them is enough. */
static bool ko_mods_ok(uint8_t need, uint8_t have, bool one_mod) {
    if (one_mod) return !need || (need & have);
    for (unsigned t = 0; t < 4; t++) {
        uint8_t l = (uint8_t)(1U << t), r = (uint8_t)(1U << (t + 4));
        if ((need & l) && (need & r)) { if (!(have & (l | r))) return false; }
        else if ((need & (l | r)) && !(have & need & (l | r))) return false;
    }
    return true;
}

static void ko_end(void) {
    if (!ko.active) return;
    ko.active = false;
    release(SLOT_KO, &ko.sent);
    usb_suppress_mods(0);
}

static bool ko_press(uint8_t row, uint8_t col, uint32_t key) {
    uint16_t kc = to_via_keycode(key);
    uint8_t mods = usb_get_real_mods();
    for (uint8_t i = 0; i < VIAL_KEY_OVERRIDE_ENTRIES; i++) {
        vial_ko_t e = vial_data.ko[i];
        if (!(e.options & KO_ENABLED) || !e.trigger || e.trigger != kc) continue;
        if (!(e.layers & layers_get_state())) continue;
        if (!ko_mods_ok(e.trigger_mods, mods, e.options & KO_ONE_MOD) || (mods & e.negative_mods)) continue;
        ko_end();
        usb_suppress_mods(e.suppressed_mods & mods);
        uint32_t sent = press_via(SLOT_KO, e.replacement);
        ko = (__typeof__(ko)){.active = true, .keep = (e.options & KO_KEEP_ON_OTHER_KEY) != 0,
                              .row = row, .col = col, .sent = sent};
        return true;
    }
    return false;
}

/* ---------------- Repeat and alt repeat ---------------- */
static uint32_t s_last_key;
static uint8_t s_last_mods;
static uint32_t s_repeat_down[2]; // per key (Repeat, Alt Repeat): what it pressed

// The key a repeat sends: a mod-tap's tap key, a modified key's key with its mods added
static uint32_t base_key(uint32_t k, uint8_t *mods) {
    uint32_t kind = k & 0xFF000000;
    if (kind == KIND_HT) return k & 0xFF;
    if (kind == KIND_MK) { *mods |= (uint8_t)(k >> 8); return k & 0xFF; }
    return k;
}

// An entry keycode as vial-qmk normalises it: modified key -> key + mods; mod/layer-tap -> tap key
static uint8_t unpack_mods5(uint8_t m) { return (m & 0x10) ? (uint8_t)(m << 4) : m; }
static uint16_t ar_normalize(uint16_t kc, uint8_t *mods) {
    if (kc >= 0x0100 && kc <= 0x1FFF) { *mods |= unpack_mods5((uint8_t)((kc >> 8) & 0x1F)); return kc & 0xFF; }
    if (kc >= 0x2000 && kc <= 0x4FFF) return kc & 0xFF; // mod-tap, layer-tap
    return kc;
}
static bool ar_mods_match(uint8_t mods, uint8_t required, uint8_t allowed, uint8_t options) {
    allowed |= required;
    if (options & AR_ANY_HAND) { mods = four_mods(mods); required = four_mods(required); allowed = four_mods(allowed); }
    return (mods & required) == required && !(mods & ~allowed);
}
static uint8_t popcount8(uint8_t m) { uint8_t n = 0; for (; m; m &= (uint8_t)(m - 1)) n++; return n; }

/* vial-qmk's alt repeat lookup: the match with the most required mods wins, else a default
 * entry; returns the alt key (0 if none) and adds its own mods to *add_mods */
static uint16_t alt_of(uint16_t kc, uint8_t mods, uint8_t *add_mods) {
    uint16_t alt = 0;
    uint8_t alt_mods = 0;
    int best = -1;
    for (unsigned i = 0; i < VIAL_ALT_REPEAT_KEY_ENTRIES; i++) {
        vial_ar_t e = vial_data.ar[i];
        if (!(e.options & AR_ENABLED)) continue;
        uint8_t req = 0, alt_req = 0;
        uint16_t key = ar_normalize(e.keycode, &req), alt_key = ar_normalize(e.alt_keycode, &alt_req);
        if (key == kc && ar_mods_match(mods, req, e.allowed_mods, e.options) && popcount8(req) > best) {
            best = popcount8(req); alt = alt_key; alt_mods = alt_req;
        }
        if ((e.options & AR_BIDIRECTIONAL) && alt_key == kc && ar_mods_match(mods, alt_req, e.allowed_mods, e.options) &&
            popcount8(alt_req) > best) {
            best = popcount8(alt_req); alt = key; alt_mods = req;
        }
        if ((e.options & AR_DEFAULT) && best < 0 && !alt && ar_mods_match(mods, 0, e.allowed_mods, e.options)) {
            alt = alt_key; alt_mods = alt_req;
        }
    }
    *add_mods |= alt_mods;
    return alt;
}

static void repeat_key(bool alt, bool pressed) {
    uint8_t slot = alt ? SLOT_ALT_REPEAT : SLOT_REPEAT;
    release(slot, &s_repeat_down[alt]);
    if (!pressed) return;
    uint8_t mods = s_last_mods;
    uint32_t k = base_key(s_last_key, &mods);
    if (alt) {
        uint8_t add = 0;
        uint16_t a = alt_of(to_via_keycode(k), mods, &add);
        k = a ? base_key(from_via_keycode(a), &mods) : 0;
        mods |= add;
    }
    // Repeat keys never repeat themselves (an entry pointing at one would recurse)
    if (!k || k == K_REPEAT || k == K_ALT_REPEAT) return;
    if (mods && k <= HID_KEY_EXSEL) k = MK(mods, k); // basic keys only: macro keys must stay macros
    s_repeat_down[alt] = keyboard_virtual_press(slot, k);
}

/* ---------------- Caps Word, Grave Escape, magic swaps ---------------- */
static bool s_caps_word;
static TickType_t s_caps_word_at;

// QMK magic flags (setting 21) applied to basic keycodes at press time
static uint32_t magic_map(uint32_t k) {
    uint32_t f = vial_data.magic;
    if (!f || k > 0xFF) return k;
    if ((f & (1U << 0)) && k == HID_KEY_CAPS_LOCK) return HID_KEY_CONTROL_LEFT;
    if ((f & (1U << 0)) && k == HID_KEY_CONTROL_LEFT) return HID_KEY_CAPS_LOCK;
    if ((f & (1U << 1)) && k == HID_KEY_CAPS_LOCK) return HID_KEY_CONTROL_LEFT;
    if ((f & (1U << 2)) && (k == HID_KEY_ALT_LEFT || k == HID_KEY_GUI_LEFT)) k ^= HID_KEY_ALT_LEFT ^ HID_KEY_GUI_LEFT;
    else if ((f & (1U << 3)) && (k == HID_KEY_ALT_RIGHT || k == HID_KEY_GUI_RIGHT)) k ^= HID_KEY_ALT_RIGHT ^ HID_KEY_GUI_RIGHT;
    else if ((f & (1U << 8)) && (k == HID_KEY_CONTROL_LEFT || k == HID_KEY_GUI_LEFT)) k ^= HID_KEY_CONTROL_LEFT ^ HID_KEY_GUI_LEFT;
    else if ((f & (1U << 9)) && (k == HID_KEY_CONTROL_RIGHT || k == HID_KEY_GUI_RIGHT)) k ^= HID_KEY_CONTROL_RIGHT ^ HID_KEY_GUI_RIGHT;
    if ((f & (1U << 4)) && (k == HID_KEY_GUI_LEFT || k == HID_KEY_GUI_RIGHT)) return K_NULL;
    if ((f & (1U << 5)) && (k == HID_KEY_ESCAPE || k == HID_KEY_GRAVE)) return k ^ HID_KEY_ESCAPE ^ HID_KEY_GRAVE;
    if ((f & (1U << 6)) && (k == HID_KEY_BACKSLASH || k == HID_KEY_BACKSPACE)) return k ^ HID_KEY_BACKSLASH ^ HID_KEY_BACKSPACE;
    return k;
}

/* Grave Escape (QMK): ` when Shift or GUI is held, but Esc when any held modifier has its
 * override bit (0 Alt, 1 Ctrl, 2 GUI, 3 Shift) */
static uint32_t grave_escape(void) {
    uint8_t m = four_mods(usb_get_real_mods()), off = vial_data.grave_esc_override;
    bool overridden = ((m & MOD_LALT) && (off & 1)) || ((m & MOD_LCTRL) && (off & 2)) ||
                      ((m & MOD_LGUI) && (off & 4)) || ((m & MOD_LSHIFT) && (off & 8));
    return (m & (MOD_LSHIFT | MOD_LGUI)) && !overridden ? HID_KEY_GRAVE : HID_KEY_ESCAPE;
}

// Caps Word on one basic key: true to shift it; ends the word on keys that aren't part of one
static bool caps_word_shift(uint32_t k) {
    if (!s_caps_word || k > 0xFF || (k >= 0xE0 && k <= 0xE7)) return false;
    s_caps_word_at = xTaskGetTickCount();
    if ((k >= HID_KEY_A && k <= HID_KEY_Z) || k == HID_KEY_MINUS) return true;
    if (!((k >= HID_KEY_1 && k <= HID_KEY_0) || k == HID_KEY_BACKSPACE || k == HID_KEY_DELETE || k == K_NULL))
        s_caps_word = false;
    return false;
}

uint32_t vial_grave_escape(void) { return grave_escape(); }

uint8_t vial_caps_word_mods(uint8_t kc) { return caps_word_shift(kc) ? MOD_LSHIFT : 0; }

static bool is_modifier_or_layer(uint32_t k) {
    uint32_t kind = k & 0xFF000000;
    return (k >= 0xE0 && k <= 0xE7) || (k >= L_0 && k <= L_15) || kind == KIND_MO || kind == KIND_TG ||
           kind == KIND_OS || k == K_REPEAT || k == K_ALT_REPEAT || k == 0 || k == K_NULL;
}

static int td_index(uint32_t key) {
    return (key >= K_TD(0) && key < K_TD(VIAL_TAP_DANCE_ENTRIES)) ? (int)(key - K_TD(0)) : -1;
}

/* Every press, real or virtual, after the layer lookup; returns the key to record and act on */
uint32_t vial_features_on_press(uint8_t row, uint8_t col, uint32_t key) {
    uint16_t layers = layers_get_state();
    bool from_td = row == KEYBOARD_VIRTUAL_ROW && col < SLOT_COMBO; // a dance's own output
    td_interrupt(from_td ? (int)col : td_index(key));
    if (row != KEYBOARD_VIRTUAL_ROW && layers_get_state() != layers)
        key = layers_lookup_key(row, col); // a dance's outcome changed the layer
    if (ko.active && !ko.keep) ko_end();
    if (row != KEYBOARD_VIRTUAL_ROW) key = magic_map(key); // features' keys go out unswapped, as in vial-qmk
    if (key == K_GRAVE_ESC) key = grave_escape();
    if (caps_word_shift(key)) key = MK(MOD_LSHIFT, key);
    bool repeat = row == KEYBOARD_VIRTUAL_ROW && (col == SLOT_REPEAT || col == SLOT_ALT_REPEAT);
    if (!repeat && !is_modifier_or_layer(key)) {
        s_last_key = key;
        s_last_mods = usb_get_real_mods() & (uint8_t)~usb_get_suppressed_mods(); // as the host saw them
    }
    return key;
}

static bool system_base_layer(uint8_t l) { return l == 0 || (l == 2 && hhkb_get_system()); }

bool vial_features_process(uint8_t row, uint8_t col, uint32_t key, bool pressed) {
    int td = td_index(key);
    if (td >= 0) {
        td_event((uint8_t)td, pressed, xTaskGetTickCount());
        return true;
    }
    switch (key) {
    case K_REPEAT: repeat_key(false, pressed); return true;
    case K_ALT_REPEAT: repeat_key(true, pressed); return true;
    case K_CAPS_WORD:
        if (pressed) { s_caps_word = !s_caps_word; s_caps_word_at = xTaskGetTickCount(); }
        return true;
    case K_LAYER_LOCK: // never the base layer of the selected system
        if (pressed && !system_base_layer(layers_get_active())) layers_lock_toggle(layers_get_active());
        return true;
    case K_COMBO_ON: case K_COMBO_OFF: case K_COMBO_TOGGLE:
        if (pressed) s_combos_on = key == K_COMBO_ON || (key == K_COMBO_TOGGLE && !s_combos_on);
        return true;
    case K_KO_ON: case K_KO_OFF: case K_KO_TOGGLE:
        if (pressed) s_ko_on = key == K_KO_ON || (key == K_KO_TOGGLE && !s_ko_on);
        if (!s_ko_on) ko_end();
        return true;
    default: break;
    }
    if (row == KEYBOARD_VIRTUAL_ROW || !s_ko_on) return false; // features' own keys are never overridden
    if (pressed) return ko_press(row, col, key);
    if (ko.active && ko.row == row && ko.col == col) { ko_end(); return true; }
    return false;
}

static bool td_pending(void) {
    for (unsigned i = 0; i < VIAL_TAP_DANCE_ENTRIES; i++) if (tds[i].count && !tds[i].finished) return true;
    return false;
}

bool vial_features_has_active(void) {
    return cb_n || td_pending() || s_caps_word;
}

static void earliest(TickType_t *next, TickType_t deadline, TickType_t now) {
    if (deadline - now < *next) *next = deadline - now;
}

TickType_t vial_features_check_timeouts(TickType_t now) {
    TickType_t next = portMAX_DELAY;
    if (cb_n) {
        if ((int32_t)(now - cb_deadline) >= 0) combo_resolve();
        else earliest(&next, cb_deadline, now);
    }
    for (uint8_t i = 0; i < VIAL_TAP_DANCE_ENTRIES; i++) {
        if (!tds[i].count || tds[i].finished) continue;
        if ((int32_t)(now - tds[i].deadline) >= 0) td_finish(i);
        else earliest(&next, tds[i].deadline, now);
    }
    if (s_caps_word) {
        TickType_t end = s_caps_word_at + pdMS_TO_TICKS(CAPS_WORD_IDLE_MS);
        if ((int32_t)(now - end) >= 0) s_caps_word = false;
        else earliest(&next, end, now);
    }
    return next;
}

void vial_features_init(void) {
    memset(tds, 0, sizeof(tds));
    memset(&ko, 0, sizeof(ko));
    memset(cb_active, 0, sizeof(cb_active));
    memset(s_repeat_down, 0, sizeof(s_repeat_down));
    cb_n = 0;
    s_caps_word = false;
}
