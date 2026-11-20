#ifndef EC_SETTINGS_H
#define EC_SETTINGS_H

#include "config.h"
#include "rapid_trigger.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_EEPROM_MAGIC          0x484B4345UL // "HKCE" (versioned with CRC16)
#define USER_EEPROM_LEGACY_MAGIC   0x484B4344UL // "HKCD"
#define USER_EEPROM_C_MAGIC        0x484B4343UL // "HKCC"
#define USER_EEPROM_PREV_MAGIC     0x484B4342UL // "HKCB"
#define USER_EEPROM_OLD_MAGIC      0x484B4341UL // "HKCA"
#define USER_EEPROM_ADDR           EEPROM_SETTINGS_ADDR
#define USER_EEPROM_VERSION        2 // 2: led_modes byte replaces actuation_pct

typedef struct __attribute__((aligned(4))) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  led_modes;                          // v2: LED 0 (PA8) low nibble, LED 1 (PA9) high nibble
    uint16_t crc16;
    uint16_t baseline[MATRIX_COLS][MATRIX_ROWS]; // HHKB: 120 bytes
    uint16_t peak[MATRIX_COLS][MATRIX_ROWS];     // HHKB: 120 bytes
    rt_cfg_t rt;                                 // 10 bytes
    uint8_t  scan_rate;                          // retired: written as the fixed USB rate
    uint8_t  tapping_term_10ms;                  // 5..50
    uint8_t  quick_tap_10ms;                     // 0..50
    uint8_t  flags;                              // bit 0: permissive_hold, bits 1-2: retired (0), bit 3: custom_cal, bit 4: debug, bit 5: system, bit 6..7: v1 led0
    uint16_t classic_release_pm;                 // 25..1000
} hhkb_eeprom_cal_t;
_Static_assert(sizeof(hhkb_eeprom_cal_t) == EEPROM_SETTINGS_SIZE, "settings record size is fixed per board (EEPROM map)");

// Calibration & Settings API
void hhkb_settings_init(void);
void hhkb_calibration_load(void);
void hhkb_calibration_save_to_eeprom(void);
void hhkb_calibration_start(void);
void hhkb_calibration_finish(void);
bool hhkb_calibration_enabled(void);
void hhkb_calibration_set_enabled(bool enabled);
bool hhkb_is_calibrating(void);
bool hhkb_settings_storage_writable(void);
void hhkb_settings_mark_dirty(void);
void hhkb_settings_task(void);
/* Erase ECK's settings record and restart with the defaults (keymap and Vial entries stay);
 * false if the record area isn't ECK's. */
bool hhkb_settings_reset_request(void);

// Getters & Setters for VIA
/* Rate the matrix runs at now: the Hybrid uses its Bluetooth rate while reports go over Bluetooth. */
uint8_t hhkb_active_scan_rate(void);
/* Hybrid on batteries over Bluetooth: the matrix may drop to its idle stage after a quiet spell. */
bool hhkb_scan_idle_allowed(void);
/* USB is the output and the host has suspended it: scan at the 50 Hz idle rate, without STOP. */
bool hhkb_usb_sleep_idle_allowed(void);
uint8_t hhkb_get_hold_tap_setting(uint8_t id);
void hhkb_set_hold_tap_setting(uint8_t id, uint8_t value);
uint16_t hold_tap_config_value(uint8_t id);
/* LED modes: 0 off, 1 heartbeat, 2 Caps Lock, 3 Fn layer, 4 System 2, 5 Bluetooth status (Hybrid). */
#define HHKB_LED_MODE_BLE 5
#ifdef HHKB_HYBRID
#define HHKB_LED_MODE_MAX HHKB_LED_MODE_BLE
#else
#define HHKB_LED_MODE_MAX 4
#endif
uint8_t hhkb_get_led_mode(uint8_t index);
void hhkb_set_led_mode(uint8_t index, uint8_t mode);
uint8_t hhkb_get_system(void);
void hhkb_set_system(uint8_t system);
uint8_t hhkb_rt_get(uint8_t id);
void hhkb_rt_set(uint8_t id, uint8_t value);

// Debug Console & Monitoring API
enum {
    DEBUG_FLAG_EC    = 1U << 0, // 0x01: EC matrix debug [EC]
    DEBUG_FLAG_TOPRE = 1U << 0, // Backward-compatible alias
    DEBUG_FLAG_VIA   = 1U << 1, // 0x02: VIA telemetry diag [VIA_DIAG]
    DEBUG_FLAG_RATE  = 1U << 2, // 0x04: Polling/scan rate monitor [RATE]
    DEBUG_FLAG_CAL   = 1U << 3, // 0x08: Calibration progress monitor [CAL]
};

bool hhkb_get_debug_flag(uint8_t flag);
void hhkb_set_debug_flag(uint8_t flag, bool enabled);
void hhkb_record_matrix_scan(void);
uint32_t hhkb_get_matrix_rate_hz(void);
void hhkb_calc_rates(uint32_t elapsed_ms);

void hhkb_debug_feed_sample(uint8_t col, uint8_t row, uint16_t raw, uint16_t norm, bool is_pressed);
void hhkb_get_debug_key_str(char *buf, size_t max_len);
void hhkb_get_debug_raw_str(char *buf, size_t max_len);
void hhkb_get_debug_travel_str(char *buf, size_t max_len);
void hhkb_get_cal_status_str(char *buf, size_t max_len);

// Direct matrix arrays for zero-call-overhead inner loop scan
extern uint16_t s_cal_peak[MATRIX_COLS][MATRIX_ROWS];
extern rt_key_t s_rt_keys[MATRIX_COLS][MATRIX_ROWS];

// Matrix accessors for scan module
rt_cfg_t hhkb_get_scan_cfg(void);
void hhkb_update_dynamic_peak(uint8_t col, uint8_t row, uint16_t val);
void hhkb_cal_accumulate_baseline(uint8_t col, uint8_t row, uint16_t val);
void hhkb_cal_record_peak(uint8_t col, uint8_t row, uint16_t val);
bool hhkb_cal_is_capturing_baseline(void);
void hhkb_cal_decrement_samples(void);
#ifdef BOARD_BOOT_REST
void ec_boot_rest(uint8_t col, uint8_t row, uint16_t val);
#endif

#ifdef __cplusplus
}
#endif

#endif // EC_SETTINGS_H
