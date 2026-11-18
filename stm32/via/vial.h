#ifndef VIAL_H
#define VIAL_H
/* Vial protocol (v6) and its dynamic features; built only with VIAL_ENABLE. Always unlocked. */

#include "FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIAL_TAP_DANCE_ENTRIES 8
#define VIAL_COMBO_ENTRIES 8
#define VIAL_KEY_OVERRIDE_ENTRIES 8
#define VIAL_ALT_REPEAT_KEY_ENTRIES 8

// QMK keycodes ECK keeps as-is internally (no other meaning there)
#define K_TD(n) (0x5700U | (n))
#define K_GRAVE_ESC 0x7C16U
#define K_COMBO_ON 0x7C50U
#define K_COMBO_OFF 0x7C51U
#define K_COMBO_TOGGLE 0x7C52U
#define K_KO_TOGGLE 0x7C5DU
#define K_KO_ON 0x7C5EU
#define K_KO_OFF 0x7C5FU
#define K_CAPS_WORD 0x7C73U
#define K_REPEAT 0x7C79U
#define K_ALT_REPEAT 0x7C7AU
#define K_LAYER_LOCK 0x7C7BU

/* Entries as Vial transfers them (little-endian, packed) */
typedef struct {
    uint16_t on_tap, on_hold, on_double_tap, on_tap_hold, tapping_term;
} vial_td_t;
typedef struct {
    uint16_t input[4];
    uint16_t output;
} vial_combo_t;
typedef struct {
    uint16_t trigger, replacement, layers;
    uint8_t trigger_mods, negative_mods, suppressed_mods, options;
} vial_ko_t;
typedef struct {
    uint16_t keycode, alt_keycode;
    uint8_t allowed_mods, options;
} vial_ar_t;

enum { KO_ONE_MOD = 1 << 3, KO_KEEP_ON_OTHER_KEY = 1 << 5, KO_ENABLED = 1 << 7 };
enum { AR_DEFAULT = 1 << 0, AR_BIDIRECTIONAL = 1 << 1, AR_ANY_HAND = 1 << 2, AR_ENABLED = 1 << 3 };
enum { VIAL_TAPPING_RETRO = 1 << 0 };

/* Everything Vial edits that ECK's other records don't hold; saved as one EEPROM record */
typedef struct {
    vial_td_t td[VIAL_TAP_DANCE_ENTRIES];
    vial_combo_t combo[VIAL_COMBO_ENTRIES];
    vial_ko_t ko[VIAL_KEY_OVERRIDE_ENTRIES];
    vial_ar_t ar[VIAL_ALT_REPEAT_KEY_ENTRIES];
    uint32_t magic;          // QMK "magic" swap flags (QMK setting 21)
    uint16_t combo_term;     // ms
    uint16_t osk_timeout;    // ms
    uint8_t grave_esc_override;
    uint8_t tapping;         // VIAL_TAPPING_*
    uint8_t reserved[2];
} vial_data_t;

extern vial_data_t vial_data;

void vial_init(void);
/* Handle a 0xFE-prefixed request in place (32 bytes) */
void vial_process(uint8_t *msg);
/* USB task: write a changed record, a word per call */
void vial_storage_poll(void);

/* keys/vial_features.c */
#define VIAL_SLOT_MACRO 0x20 // virtual position for macro keys above 0xFF
/* Caps Word for a key sent outside the press path (mod-tap taps): MOD_LSHIFT or 0 */
uint8_t vial_caps_word_mods(uint8_t kc);
/* Grave Escape's key for the modifiers held now */
uint32_t vial_grave_escape(void);
void vial_features_init(void);
uint32_t vial_features_on_press(uint8_t row, uint8_t col, uint32_t key);
bool vial_features_process(uint8_t row, uint8_t col, uint32_t key, bool pressed);
bool vial_combo_on_press(uint8_t row, uint8_t col, TickType_t now);
bool vial_combo_on_release(uint8_t row, uint8_t col);
bool vial_features_has_active(void);
TickType_t vial_features_check_timeouts(TickType_t now);

#ifdef __cplusplus
}
#endif

#endif // VIAL_H
