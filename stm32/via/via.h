#ifndef VIA_H
#define VIA_H

#include "FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DYNAMIC_KEYMAP_MACRO_COUNT 16

/* Runtime keymap and VIA macro buffer; hhkb_keymap_storage.c keeps them in EEPROM. */
typedef struct {
    uint32_t keymap[DYNAMIC_KEYMAP_MAX_LAYERS][NUM_KEYS];
    uint8_t macro_buffer[DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE];
} via_storage_t;

extern via_storage_t via_storage;

#define dynamic_keymap (via_storage.keymap)
#define dynamic_macro_buffer (via_storage.macro_buffer)

uint16_t to_via_keycode(uint32_t key);
uint32_t from_via_keycode(uint16_t via_key);

/* Load the keymap: compiled defaults, then the EEPROM copy if there is one. */
void via_init(void);
/* Handle one 32-byte VIA request; response gets the 32-byte answer. */
void via_process_packet(uint8_t const *request, uint8_t *response);
#ifdef VIAL_ENABLE
// Board chord: switch between Vial (protocol 9) and VIA mode (12, usevia.app's menus)
void via_mode_toggle(void);
void via_mode_task(void); // USB task: the reconnect after a switch
#endif
/* USB task: save a changed keymap/macros after a short delay, a word per call. */
void via_eeprom_task(void);
void via_eeprom_save(void);

void via_macro_send(uint8_t id);
bool via_macro_has_active(void);
TickType_t via_macro_check_timeouts(TickType_t now);

/* hhkb_keymap_storage.c */
void via_keymap_load(void);
void via_keymap_save(void);
void via_keymap_poll(void);

/* hhkb_via_controls.c: custom menu values (channel 0); true when handled. */
bool hhkb_via_command(uint8_t const *request, uint8_t *response);

#ifdef __cplusplus
}
#endif

#endif // VIA_H
