#ifndef CONFIG_H
#define CONFIG_H

/* Firmware settings shared by every board. The board's own (name, matrix, layout, EEPROM map)
 * come from board_config.h in stm32/boards/<board>/, which the build picks. */
#include "board_config.h"

#define VIA_V3 1
#define VIA_FIRMWARE_VERSION 2U
#define DYNAMIC_KEYMAP_MAX_LAYERS 4

/* Two slots of keymap + macros must fit EEPROM_KEYMAP_SLOTS_SIZE (hhkb_keymap_storage.c). */
#define DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE 512

#define TAPPING_TERM_DEFAULT 220
#define PERMISSIVE_HOLD 1
#define HOLD_TAP_TAP_PREFERRED 0
#define QUICK_TAP_TERM 150
// #define HHKB_SCAN_DIAGNOSTICS 1

#define QUEUE_DEF_SIZE 48
#define KEYBOARD_REPORT_QUEUE_LENGTH 16

#ifdef DEFINE_KEYMAP
#include "default_keymap.h"
#endif

#endif // CONFIG_H
