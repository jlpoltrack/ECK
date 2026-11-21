#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Topre Realforce R3S US TKL (R3SD): matrix, layout and EEPROM map. Hardware: docs/r3s-hardware.md. */

#define KEYBOARD_NAME "ECK Realforce R3S" /* tools find it by "R3S"; HHKB tools look for "HHKB" */
#define USB_PRODUCT_ID 0x4011             /* test ID beside the HHKB's 0x4010 (docs/deferred.md) */

/* --- Matrix Geometry: 16 mux channels (columns) x 6 of the 8 drive lines (rows) --- */
#define MATRIX_ROWS 6
#define MATRIX_COLS 16
// Keep numeric for the VIA JSON generator, which reads these defines directly.
#define NUM_ROWS 6
#define NUM_COLS 16
#define NUM_KEYS 87
#if MATRIX_ROWS != NUM_ROWS || MATRIX_COLS != NUM_COLS
#error "matrix dimensions must match VIA dimensions"
#endif

// {ROW, COL} of each key in physical order (stock layout table 0x0801D510, docs/r3s-hardware.md)
// clang-format off
#define LAYOUT { \
    {0, 4},  {0, 7},  {0, 5},  {0, 3},  {0, 0},  {0, 1},  {0, 2},  {0, 12}, {0, 14}, {0, 15}, {0, 13}, {0, 11}, {1, 8},  {0, 8},  {0, 9},  {0, 10}, \
    {1, 4},  {1, 6},  {1, 7},  {1, 5},  {1, 3},  {1, 0},  {1, 1},  {1, 2},  {1, 12}, {1, 14}, {1, 15}, {1, 13}, {1, 11}, {2, 8},  {2, 9},  {1, 9},  {1, 10}, \
    {2, 4},  {2, 6},  {2, 7},  {2, 5},  {2, 3},  {2, 0},  {2, 1},  {2, 2},  {2, 12}, {2, 14}, {2, 15}, {2, 13}, {2, 11}, {3, 8},  {3, 9},  {3, 10}, {2, 10}, \
    {3, 4},  {3, 6},  {3, 7},  {3, 5},  {3, 3},  {3, 0},  {3, 1},  {3, 2},  {3, 12}, {3, 14}, {3, 15}, {3, 13}, {4, 8}, \
    {4, 4},  {4, 6},  {4, 7},  {4, 5},  {4, 3},  {4, 0},  {4, 1},  {4, 2},  {4, 12}, {4, 14}, {4, 15}, {4, 11}, {4, 10}, \
    {5, 4},  {5, 6},  {5, 7},  {5, 0},  {5, 14}, {5, 15}, {5, 13}, {5, 11}, {5, 10}, {5, 9},  {4, 9} \
}
#define LAYOUT_DEFAULT LAYOUT
// clang-format on

/* No factory rest levels: measure each key's rest at start-up. Measured 2026-11-17 (87 keys): rest
 * 691-1146 (median 895), full press 2585-3403, swing 1744-2404 (median 2175). */
#define BOARD_BOOT_REST 1
#define BOARD_BOOT_REST_MAX 1400   /* higher at start-up: key held, keep the default */
#define BOARD_DEFAULT_REST 900
#define BOARD_TYPICAL_TRAVEL 2100U /* below the median swing, so the weakest key still reaches 83% */
#define BOARD_SCAN_RATE 3 /* 500 Hz: a full scan takes ~1.6 ms (2026-11-17) */
#define BOARD_HOST_LEDS 1         /* Num, Caps and Scroll Lock indicators */
#define BOARD_NOTIFY_LED 1        /* the Simple Mode LED (LED 1): confirmation blinks */
#define BOARD_NOTIFY_SOLO 1       /* ...alone and in their own colour (board_leds_notify) */
#define ROM_DFU_KEEP_IOP (1U << 1) /* GPIOB (IOPRSTR/IOPENR bit 1): the update LED stays lit */
/* No recovery switch: Esc and Caps Lock held while plugging in start ST's DFU loader (r3s_matrix.c);
 * two keys, as a BIOS boot menu is entered by holding Esc alone. {col, row} each. */
#define BOARD_RECOVERY_KEYS {{4, 0}, {4, 3}}
#define BOARD_RECOVERY_MIN 2000   /* start-up reading of a held key: rest 691-1146, a full press 2585+ */
#define BOARD_LED_SETTINGS 1      /* indicator colour and brightness in VIA (IDs 34, 35) */
#define BOARD_LED_COLOUR_DEFAULT 1 /* red (bit 0 red, 1 green, 2 blue; owner's choice) */
#define BOARD_LED_LEVEL_DEFAULT 4  /* 20 %: each LED lit 5 % per level of the time, 25 % at most */

/* --- EEPROM map (data EEPROM 0x08080000-0x080817FF) ---
 * Topre's data stays untouched: 0x000-0x05F (model ID), 0x500-0xBFF, 0x17C0-0x17FF (Bank 2 tag at
 * 0x17E0). No factory sensor data: no EEPROM_FACTORY_* addresses. */
#define EEPROM_SETTINGS_ADDR         0x08080100UL /* ECK settings record (ec_settings.h) */
#define EEPROM_SETTINGS_SIZE         408          /* its size, fixed: later fields keep their offsets */
#define EEPROM_VIAL_ADDR             0x08080300UL /* Vial entries record (via/vial.c) */
#define EEPROM_VIAL_END              0x08080500UL /* up to Topre's data */
#define EEPROM_KEYMAP_SLOTS_ADDR     0x08080C00UL /* two VIA keymap + macro slots (hhkb_keymap_storage.c) */
#define EEPROM_KEYMAP_SLOTS_SIZE     0xBC0U       /* up to Topre's tag block */
#define EEPROM_TOPRE_TAG_ADDR        0x080817E0UL /* Topre's Bank 2 tag (tools/r3s/r3s_backup.py) */

#define BOARD_VIAL_UID {0x45, 0x43, 0x4B, 0x52, 0x33, 0x53, 0x00, 0x01} /* Vial keyboard ID: "ECKR3S" 1 */

#endif // BOARD_CONFIG_H
