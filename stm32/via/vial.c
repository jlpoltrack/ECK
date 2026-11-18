/* Vial protocol v6: keyboard definition, QMK settings and dynamic entries (tap dance, combos,
 * key overrides, alt repeat keys). Always unlocked, like vial-qmk's VIAL_INSECURE builds.
 * Written from the protocol, not vial-qmk's code. */
#include "vial.h"
#include "board_config.h"
#include "ec_settings.h"
#include "eeprom.h"
#include "hold_tap.h"
#include "task.h"
#include "vial_definition.h"
#include <string.h>

#define VIAL_PROTOCOL 6U

static const uint8_t keyboard_uid[8] = BOARD_VIAL_UID;

vial_data_t vial_data;

enum {
    GET_KEYBOARD_ID, GET_SIZE, GET_DEF, GET_ENCODER, SET_ENCODER, GET_UNLOCK_STATUS, UNLOCK_START,
    UNLOCK_POLL, LOCK, QMK_SETTINGS_QUERY, QMK_SETTINGS_GET, QMK_SETTINGS_SET, QMK_SETTINGS_RESET,
    DYNAMIC_ENTRY_OP,
};
enum { DYN_COUNT, DYN_TD_GET, DYN_TD_SET, DYN_COMBO_GET, DYN_COMBO_SET, DYN_KO_GET, DYN_KO_SET,
       DYN_AR_GET, DYN_AR_SET };

/* ---- EEPROM record at the board's EEPROM_VIAL_ADDR: commit, tag, hash, payload ---- */
#define REC_ADDR EEPROM_VIAL_ADDR
#define REC_TAG 0x4C414956UL    // 'VIAL'
#define REC_COMMIT 0x54494d43UL // 'CMIT'
#define REC_WORDS (sizeof(vial_data_t) / 4U)
_Static_assert(sizeof(vial_data_t) % 4U == 0, "record is written in words");
_Static_assert(REC_ADDR + (3U + REC_WORDS) * 4U <= EEPROM_VIAL_END, "record overruns its EEPROM area");
_Static_assert(REC_ADDR >= EEPROM_SETTINGS_ADDR + EEPROM_SETTINGS_SIZE || REC_ADDR + (3U + REC_WORDS) * 4U <= EEPROM_SETTINGS_ADDR,
               "record overlaps the settings record");

static unsigned rec_stage;
static bool rec_writing, rec_dirty;

#define REC ((volatile uint32_t *)REC_ADDR)

// Hash of the payload as stored, so it always matches what was written
static uint32_t stored_hash(void) {
    uint32_t h = 2166136261U;
    for (unsigned i = 0; i < REC_WORDS; i++)
        for (unsigned b = 0; b < 4; b++) { h ^= (REC[3 + i] >> (8 * b)) & 0xFFU; h *= 16777619U; }
    return h;
}

static void defaults(void) {
    memset(&vial_data, 0, sizeof(vial_data));
    vial_data.combo_term = 50;
    vial_data.osk_timeout = 1500;
}

static void load(void) {
    defaults();
    if (REC[0] != REC_COMMIT || REC[1] != REC_TAG || REC[2] != stored_hash()) return;
    for (unsigned i = 0; i < REC_WORDS; i++) {
        uint32_t w = REC[3 + i];
        memcpy((uint8_t *)&vial_data + 4 * i, &w, 4);
    }
}

static void save(void) { rec_dirty = true; }

/* Copies straight from vial_data, a word per call (no snapshot): an edit mid-save sets
 * rec_dirty again, and the hash is taken from the stored words, so the record stays valid. */
void vial_storage_poll(void) {
    if (!rec_writing) {
        if (!rec_dirty) return;
        rec_dirty = false;
        rec_stage = 0;
        rec_writing = true;
    }
    unsigned at;
    uint32_t value;
    if (rec_stage == 0) { at = 0; value = 0; }
    else if (rec_stage == 1) { at = 1; value = REC_TAG; }
    else if (rec_stage < 2 + REC_WORDS) { at = rec_stage + 1; memcpy(&value, (uint8_t *)&vial_data + 4 * (rec_stage - 2), 4); }
    else if (rec_stage == 2 + REC_WORDS) { at = 2; value = stored_hash(); }
    else { at = 0; value = REC_COMMIT; }
    if (!eeprom_write_word(&REC[at], value)) { rec_writing = false; return; } // give up, as the keymap writer does
    if (++rec_stage == 4 + REC_WORDS) rec_writing = false;
}

/* ---- QMK settings: tapping values live in ECK's settings record, the rest in ours ---- */
// ECK's "permissive hold" holds when another key goes down: QMK's hold on other key press (23)
static const uint16_t qsids[] = {1, 2, 6, 7, 21, 23, 24, 25};

static int qs_get(uint16_t id, uint8_t *out) {
    uint32_t v;
    uint8_t size = 2;
    switch (id) {
    case 1: v = vial_data.grave_esc_override; size = 1; break;
    case 2: v = vial_data.combo_term; break;
    case 6: v = vial_data.osk_timeout; break;
    case 7: v = hold_tap_config_value(0); break;
    case 21: v = vial_data.magic; size = 4; break;
    case 23: v = hold_tap_config_value(1); size = 1; break;
    case 24: v = (vial_data.tapping & VIAL_TAPPING_RETRO) != 0; size = 1; break;
    case 25: v = hold_tap_config_value(3); break;
    default: return -1;
    }
    memcpy(out, &v, size); // little-endian, as the app expects
    return 0;
}

static int qs_set(uint16_t id, const uint8_t *in) {
    uint16_t v16 = (uint16_t)(in[0] | (in[1] << 8));
    switch (id) {
    case 1: vial_data.grave_esc_override = in[0]; break;
    case 2: vial_data.combo_term = v16; break;
    case 6: vial_data.osk_timeout = v16; break;
    case 7: // ECK keeps 50-500 ms in 10 ms steps
        if (v16 < 50 || v16 > 500) return -1;
        hhkb_set_hold_tap_setting(0, (uint8_t)(v16 / 10U));
        return 0;
    case 21: memcpy(&vial_data.magic, in, 4); break;
    case 23: hhkb_set_hold_tap_setting(1, in[0] & 1U); return 0;
    case 24: vial_data.tapping = (uint8_t)((vial_data.tapping & ~VIAL_TAPPING_RETRO) | (in[0] & 1U)); break;
    case 25:
        if (v16 > 500) return -1;
        hhkb_set_hold_tap_setting(3, (uint8_t)(v16 / 10U));
        return 0;
    default: return -1;
    }
    save();
    return 0;
}

static void qs_reset(void) {
    vial_data.magic = 0;
    vial_data.combo_term = 50;
    vial_data.osk_timeout = 1500;
    vial_data.grave_esc_override = 0;
    vial_data.tapping = 0;
    hhkb_set_hold_tap_setting(0, 22);
    hhkb_set_hold_tap_setting(1, 1);
    hhkb_set_hold_tap_setting(3, 15);
    save();
}

/* ---- Dynamic entries: one table drives get/set for all four kinds ---- */
// kc_words: leading 16-bit fields that are keycodes (checked by the firewall)
static const struct { void *base; uint8_t size, count, kc_words; } tables[] = {
    {vial_data.td, sizeof(vial_td_t), VIAL_TAP_DANCE_ENTRIES, 4},
    {vial_data.combo, sizeof(vial_combo_t), VIAL_COMBO_ENTRIES, 5},
    {vial_data.ko, sizeof(vial_ko_t), VIAL_KEY_OVERRIDE_ENTRIES, 2},
    {vial_data.ar, sizeof(vial_ar_t), VIAL_ALT_REPEAT_KEY_ENTRIES, 2},
};

static void dynamic_entry(uint8_t *msg) {
    uint8_t op = msg[2], idx = msg[3];
    if (op == DYN_COUNT) {
        memset(msg, 0, 32);
        msg[0] = VIAL_TAP_DANCE_ENTRIES;
        msg[1] = VIAL_COMBO_ENTRIES;
        msg[2] = VIAL_KEY_OVERRIDE_ENTRIES;
        msg[3] = VIAL_ALT_REPEAT_KEY_ENTRIES;
        msg[31] = 0x03; // Caps Word, Layer Lock
        return;
    }
    if (op > DYN_AR_SET) return;
    unsigned t = (op - 1U) / 2U;
    if (idx >= tables[t].count) { msg[0] = 1; return; }
    uint8_t *entry = (uint8_t *)tables[t].base + (size_t)idx * tables[t].size;
    if (op & 1U) { // get
        msg[0] = 0;
        memcpy(&msg[1], entry, tables[t].size);
        return;
    }
    taskENTER_CRITICAL(); // the keyboard task reads entries
    memcpy(entry, &msg[4], tables[t].size);
    taskEXIT_CRITICAL();
    msg[0] = 0;
    save();
}

void vial_process(uint8_t *msg) {
    switch (msg[1]) {
    case GET_KEYBOARD_ID:
        memset(msg, 0, 32);
        msg[0] = VIAL_PROTOCOL; // 32-bit little-endian
        memcpy(&msg[4], keyboard_uid, sizeof(keyboard_uid));
        break;
    case GET_SIZE: {
        uint32_t size = sizeof(vial_definition);
        memcpy(msg, &size, 4);
        break;
    }
    case GET_DEF: {
        uint32_t start = (uint32_t)(msg[2] | (msg[3] << 8)) * 32U;
        if (start >= sizeof(vial_definition)) break;
        uint32_t n = sizeof(vial_definition) - start;
        memcpy(msg, &vial_definition[start], n > 32 ? 32 : n);
        break;
    }
    case GET_UNLOCK_STATUS: // always unlocked; no unlock keys
    case UNLOCK_POLL:
        memset(msg, 0xFF, 32);
        msg[0] = 1;
        msg[1] = 0;
        break;
    case QMK_SETTINGS_QUERY: {
        uint16_t after = (uint16_t)(msg[2] | (msg[3] << 8));
        memset(msg, 0xFF, 32);
        unsigned n = 0;
        for (unsigned i = 0; i < sizeof(qsids) / sizeof(qsids[0]) && n < 16; i++)
            if (qsids[i] > after) { msg[2 * n] = (uint8_t)qsids[i]; msg[2 * n + 1] = 0; n++; }
        break;
    }
    case QMK_SETTINGS_GET:
        msg[0] = (uint8_t)qs_get((uint16_t)(msg[2] | (msg[3] << 8)), &msg[1]);
        break;
    case QMK_SETTINGS_SET:
        msg[0] = (uint8_t)qs_set((uint16_t)(msg[2] | (msg[3] << 8)), &msg[4]);
        break;
    case QMK_SETTINGS_RESET:
        qs_reset();
        break;
    case DYNAMIC_ENTRY_OP:
        dynamic_entry(msg);
        break;
    default: // no encoders
        break;
    }
}

void vial_init(void) {
    load();
    vial_features_init();
}
