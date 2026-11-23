#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* HHKB Professional Classic, Type-S and Hybrid, ANSI or (HHKB_JIS) JIS: matrix, layout and EEPROM map. */

#ifdef HHKB_JIS
#ifdef HHKB_HYBRID
#define KEYBOARD_NAME "HHKB Professional Hybrid JIS"
#else
#define KEYBOARD_NAME "HHKB Professional Classic JIS"
#endif
#elif defined(HHKB_HYBRID)
#define KEYBOARD_NAME "HHKB Professional Hybrid"
#else
#define KEYBOARD_NAME "HHKB Professional Classic"
#endif

#ifdef HHKB_JIS
/* --- JIS matrix: 5 strobes x 15 columns, read from PFU's JIS images (docs/models.md#jis) --- */
#define MATRIX_ROWS 5
#define MATRIX_COLS 15
#define NUM_ROWS 5
#define NUM_COLS 15
#define NUM_KEYS 69
#if MATRIX_ROWS != NUM_ROWS || MATRIX_COLS != NUM_COLS
#error "HHKB matrix dimensions must match VIA dimensions"
#endif
#define HHKB_ROW_MASK 0x0C07U /* row strobes PB0, PB1, PB2, PB10, PB11 */
#define USB_PRODUCT_ID 0x4012 /* test ID beside the ANSI HHKB's 0x4010 (docs/deferred.md) */
#define USB_HID_COUNTRY_CODE 15 /* Japan (Katakana), as PFU's JIS firmware */
#define BOARD_SCAN_RATE 3 /* USB 500 Hz: 75 readings make a scan longer than 1 ms (docs/models.md#jis) */

// {ROW, COL} per physical key, rows top to bottom (PFU's JIS key table)
// clang-format off
#define LAYOUT { \
    {4, 0}, {4, 3}, {4, 2}, {4, 4}, {4, 5}, {4, 6}, {4, 7}, {4, 11}, {4, 8}, {4, 9}, {4, 10}, {4, 12}, {4, 14}, {4, 13}, {2, 13}, \
    {2, 0}, {2, 3}, {2, 2}, {2, 4}, {2, 5}, {2, 6}, {2, 7}, {2, 11}, {2, 8}, {2, 9}, {2, 10}, {2, 12}, {2, 14}, {0, 13}, \
    {0, 0}, {0, 3}, {0, 2}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 11}, {0, 8}, {0, 9}, {0, 10}, {0, 12}, {0, 14}, \
    {3, 0}, {3, 3}, {3, 2}, {3, 4}, {3, 5}, {3, 6}, {3, 7}, {3, 11}, {3, 8}, {3, 9}, {3, 10}, {3, 12}, {3, 14}, {3, 13}, \
    {4, 1}, {1, 1}, {1, 3}, {0, 1}, {3, 1}, {1, 6}, {1, 11}, {1, 8}, {1, 9}, {1, 10}, {1, 12}, {1, 14}, {1, 13} \
}
#define LAYOUT_DEFAULT LAYOUT
// clang-format on
#else
/* --- Matrix Geometry --- */
#define MATRIX_ROWS 4
#define MATRIX_COLS 15
// Keep numeric for the VIA JSON generator, which reads these defines directly.
#define NUM_ROWS 4
#define NUM_COLS 15
#define NUM_KEYS 60
#if MATRIX_ROWS != NUM_ROWS || MATRIX_COLS != NUM_COLS
#error "HHKB matrix dimensions must match VIA dimensions"
#endif
#define HHKB_ROW_MASK 0x0407U /* row strobes PB0, PB1, PB2, PB10 (PB11 is the JIS fifth) */

// {ROW, COL} coordinate mapping from physical key position to Topre matrix (Duncaen / QMK)
// clang-format off
#define LAYOUT { \
    {1, 0}, {1, 3}, {1, 1}, {1, 2}, {1, 4}, {1, 5}, {1, 6}, {1, 7}, {1, 11}, {1, 8}, {1, 9}, {1, 10}, {1, 12}, {1, 14}, {1, 13}, \
    {2, 0}, {2, 3}, {2, 1}, {2, 2}, {2, 4}, {2, 5}, {2, 7}, {2, 11}, {2, 8}, {2, 9}, {2, 10}, {2, 12}, {2, 14}, {2, 13}, \
    {3, 0}, {3, 3}, {3, 1}, {3, 2}, {0, 5}, {2, 6}, {3, 7}, {3, 11}, {3, 8}, {3, 9}, {3, 10}, {3, 12}, {3, 14}, \
    {0, 0}, {0, 1}, {0, 2}, {3, 4}, {3, 5}, {3, 6}, {0, 7}, {0, 11}, {0, 8}, {0, 9}, {0, 12}, {0, 14}, {3, 13}, \
            {0, 3}, {0, 4},                 {0, 6},                   {0, 10}, {0, 13} \
}
#define LAYOUT_DEFAULT LAYOUT
// clang-format on
#endif

/* Matrix pins (driven by registers in classic_matrix.c): rows PB0, PB1, PB2, PB10 (+ PB11 on JIS); column
 * demux U1 (cols 0-7, PC6), U2 (cols 8-14, PB15), address PB12-PB14; sensor power PC14,
 * op-amp enable PC7, discharge PC8, ADC1_IN1 on PA1. DIP switches SW1-SW6: PA3-PA7, PC4. */

/*
 * SW6 Operation Mode:
 * Option 1 (Emergency Recovery): Set HHKB_SW6_REBOOT_TO_BANK1 1
 *   - SW6 ON: EMERGENCY ONLY! Triggers failsafe recovery into Bank 1 by:
 *             1. Setting EEPROM status byte 0x08080348 = 0x01 (Bank 2 broken).
 *             2. Zeroing EEPROM CRC tag at 0x08080370 (invalidates Bank 2 checksum).
 *             3. Erasing the first Flash page (128 B) of Bank 2 at 0x08010000
 *                from a RAM-resident function with interrupts disabled,
 *                destroying SP and Reset vectors so Bank 1 bootloader
 *                cannot jump into Bank 2 regardless of its own validation logic.
 *             4. System reset via NVIC_SystemReset().
 *             The stock Bank 1 bootloader then stays in firmware update mode
 *             until Bank 2 is reflashed (PFU Keymap Tool or tools/hhkb_flasher.py).
 *             NOTE: In normal operation, SW6 MUST ALWAYS BE OFF!
 *   - SW6 OFF: Normal operation; ECK boots in Bank 2.
 *
 * Option 2 (Stock Power Saving): Comment out or set HHKB_SW6_REBOOT_TO_BANK1 0
 *   - SW6 OFF: Power Saving Enabled (no key wakeup from USB suspend).
 *   - SW6 ON:  Power Saving Disabled (immediate key wakeup).
 */
#ifdef HHKB_HYBRID
// Stock Hybrid meaning (PFU manual): SW6 OFF = power saving (auto power-off on batteries).
#define HHKB_SW6_REBOOT_TO_BANK1 0
#else
#define HHKB_SW6_REBOOT_TO_BANK1 1
#endif

/* --- EEPROM map (data EEPROM 0x08080000-0x080817FF) ---
 * 0x340-0x37F holds the stock boot loader's Bank 2 tags (dip_switch.h, boot/boot.c). */
#define EEPROM_KEYMAP_SLOTS_ADDR     0x08080600UL /* two VIA keymap + macro slots (hhkb_keymap_storage.c) */
#define EEPROM_KEYMAP_SLOTS_SIZE     0xa00U       /* up to the factory sensor data */
#ifndef HHKB_JIS /* JIS slots are new: no older formats to read */
#define EEPROM_KEYMAP_LEGACY_FORMATS 1            /* slots may hold ECK formats 1 and 2 */
#endif
#define EEPROM_FACTORY_BASELINE_ADDR 0x08081000UL /* PFU's rest level per key, u16 at 8*col+row */
#define EEPROM_FACTORY_PRESS_ADDR    0x08081100UL /* PFU's press threshold per key, u16 */
#define EEPROM_FACTORY_OFFSET_ADDR   0x08081200UL /* signed offset added to that threshold, s16 */
#define EEPROM_SETTINGS_ADDR         0x08081500UL /* ECK settings record (ec_settings.h) */
#ifdef HHKB_JIS /* 69-key calibration arrays: a longer settings record, the Vial record after it */
#define EEPROM_SETTINGS_SIZE         324          /* its size, fixed: later fields keep their offsets */
#define EEPROM_VIAL_ADDR             0x08081644UL /* Vial entries record (via/vial.c) */
#else
#define EEPROM_SETTINGS_SIZE         264          /* its size, fixed: later fields keep their offsets */
#define EEPROM_VIAL_ADDR             0x08081608UL /* Vial entries record (via/vial.c) */
#endif
#define EEPROM_VIAL_END              0x080817F8UL /* up to the Bluetooth settings word */
#define EEPROM_BLE_SETTINGS_ADDR     0x080817F8UL /* Hybrid Bluetooth settings word (hybrid_ble.c) */

#ifdef HHKB_JIS
#define BOARD_VIAL_UID {0x45, 0x43, 0x4B, 0x48, 0x48, 0x4B, 0x42, 0x02} /* Vial keyboard ID: "ECKHHKB" 2 (JIS) */
#else
#define BOARD_VIAL_UID {0x45, 0x43, 0x4B, 0x48, 0x48, 0x4B, 0x42, 0x01} /* Vial keyboard ID: "ECKHHKB" 1 */
#endif

/* Status LEDs PA8 and PA9: board/indicators_system.c owns both. */
#define BOARD_NOTIFY_LED 0 /* PA8 (blue on the Hybrid): confirmation blinks */

#endif // BOARD_CONFIG_H
