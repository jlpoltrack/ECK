#include "ec_settings.h"
#include "matrix_scan.h"
#include "settings_storage_guard.h"
#include "usb_descriptors.h"
#include "eeprom.h"
#include "stm32l0xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "fmt.h"
#ifdef HHKB_HYBRID
#include "hybrid.h"
#include "hybrid_ble.h"
#include "platform.h"
/* A key counts as pressed in calibration this far above rest (Hybrid: full press ~2500). */
#define CAL_MIN_SWING 600U
#else
#define CAL_MIN_SWING RT_MIN_RANGE
#endif

#ifndef BOARD_TYPICAL_TRAVEL
#define BOARD_TYPICAL_TRAVEL 2370U /* rest to full press without calibration (HHKB) */
#endif
#define TOPRE_TYPICAL_TRAVEL_COUNTS BOARD_TYPICAL_TRAVEL
#ifndef BOARD_SCAN_RATE
#define BOARD_SCAN_RATE 4 /* USB scan rate, fixed: 1000 Hz */
#endif
#define BLE_SCAN_RATE 2U /* Bluetooth, fixed: 250 Hz, above the 15-30 ms connection interval and kind to batteries */
#define CAL_BASELINE_SAMPLES 16U
#define HHKB_SETTINGS_SAVE_DELAY_MS 500U

/* Per-key bit masks. KEY_BIT and the two-word mask macros keep the HHKB's original code. */
#define KEY_INDEX(col, row) ((col) * MATRIX_ROWS + (row))
#define KEY_MASK_WORDS ((MATRIX_COLS * MATRIX_ROWS + 31) / 32)
#if MATRIX_ROWS == 4
#define KEY_BIT(col, row) (((col) << 2) | ((row) & 3))
#else
#define KEY_BIT(col, row) KEY_INDEX(col, row)
#endif
#if KEY_MASK_WORDS == 2
#define MASK_TEST(m, k) (((k) < 32) ? ((m)[0] & (1U << (k))) != 0 : ((m)[1] & (1U << ((k) - 32))) != 0)
#define MASK_SET(m, k) do { if ((k) < 32) (m)[0] |= (1U << (k)); else (m)[1] |= (1U << ((k) - 32)); } while (0)
#define MASK_CLEAR(m, k) do { if ((k) < 32) (m)[0] &= ~(1U << (k)); else (m)[1] &= ~(1U << ((k) - 32)); } while (0)
#define MASK_COUNT(m) (__builtin_popcount((m)[0]) + __builtin_popcount((m)[1]))
#else
#define MASK_TEST(m, k) (((m)[(k) / 32] & (1U << ((k) % 32))) != 0)
#define MASK_SET(m, k) ((m)[(k) / 32] |= (1U << ((k) % 32)))
#define MASK_CLEAR(m, k) ((m)[(k) / 32] &= ~(1U << ((k) % 32)))
static int mask_count(const uint32_t *m) {
    int n = 0;
    for (unsigned i = 0; i < KEY_MASK_WORDS; i++) n += __builtin_popcount(m[i]);
    return n;
}
#define MASK_COUNT(m) mask_count(m)
#endif
#define STR_(x) #x
#define STR(x) STR_(x)
#ifndef BOARD_DEFAULT_REST
#define BOARD_DEFAULT_REST 800 /* rest level assumed without factory or custom calibration */
#endif

static uint16_t s_cal_baseline[MATRIX_COLS][MATRIX_ROWS];
uint16_t s_cal_peak[MATRIX_COLS][MATRIX_ROWS];
rt_key_t s_rt_keys[MATRIX_COLS][MATRIX_ROWS];

static bool s_custom_calibration = false; /* fresh keyboard: PFU factory thresholds */
static bool s_calibration_mode = false;
static bool s_capture_baseline = false;
static uint8_t s_cal_baseline_samples = 0;
static uint16_t s_cal_baseline_accum[MATRIX_COLS][MATRIX_ROWS];
static uint32_t s_calibrated_mask[KEY_MASK_WORDS];
static uint32_t s_cal_down_mask[KEY_MASK_WORDS];
static uint16_t s_cal_press_start_peak[MATRIX_COLS][MATRIX_ROWS];
static uint16_t s_capture_base[MATRIX_COLS][MATRIX_ROWS];
static uint16_t s_capture_peak[MATRIX_COLS][MATRIX_ROWS];

static rt_cfg_t s_rt_cfg = {
    .rt_enabled = false,
    .act_pm = 750,
    .press_pm = 25,
    .release_pm = 25,
    .reset_pm = 50,
};
static uint16_t s_classic_release_pm = 100; // Default 10% travel hysteresis (0.4 mm)

static uint8_t s_tapping_term_10ms = 22; // 220 ms
static uint8_t s_quick_tap_10ms = 15;    // 150 ms
static uint8_t s_permissive_hold = 1;
#ifdef HHKB_HYBRID
static const uint8_t k_led_default[2] = {HHKB_LED_MODE_BLE, 0}; // stock: off except status patterns
#else
static const uint8_t k_led_default[2] = {2, 3};
#endif
static uint8_t s_led_mode[2] = {k_led_default[0], k_led_default[1]};
#ifdef BOARD_LED_SETTINGS
static uint8_t s_led_colour = BOARD_LED_COLOUR_DEFAULT, s_led_level = BOARD_LED_LEVEL_DEFAULT;
#endif
static uint8_t s_system = 0;

static volatile bool s_settings_dirty = false;
static volatile TickType_t s_settings_dirty_time = 0;

// Debug console & monitoring
static volatile uint8_t s_debug_flags = 0;
static volatile uint32_t s_matrix_scans_total = 0;
static volatile uint32_t s_matrix_scans_prev = 0;
static volatile uint32_t s_matrix_rate_hz = 0;
static volatile uint8_t s_dbg_col = 0;
static volatile uint8_t s_dbg_row = 0;
static volatile uint16_t s_dbg_raw = 0;
static volatile uint16_t s_dbg_norm = 0;
static volatile bool s_dbg_down = false;
static uint16_t s_last_reported_raw[MATRIX_COLS][MATRIX_ROWS];
static uint32_t s_last_reported_down_mask[KEY_MASK_WORDS];

static inline bool is_reported_down(uint8_t col, uint8_t row) {
    uint8_t idx = KEY_BIT(col, row);
    return (s_last_reported_down_mask[idx / 32] & (1U << (idx % 32))) != 0;
}

static inline void set_reported_down(uint8_t col, uint8_t row, bool down) {
    uint8_t idx = KEY_BIT(col, row);
    if (down) {
        s_last_reported_down_mask[idx / 32] |= (1U << (idx % 32));
    } else {
        s_last_reported_down_mask[idx / 32] &= ~(1U << (idx % 32));
    }
}

static uint16_t compute_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

static uint16_t get_factory_baseline(uint8_t col, uint8_t row) {
#ifndef EEPROM_FACTORY_BASELINE_ADDR /* no factory sensor data on this board */
    (void)col;
    (void)row;
    return 0;
#else
    const uint16_t *base = (const uint16_t *)EEPROM_FACTORY_BASELINE_ADDR;
    uint8_t idx = (col << 3) | (row & 7);
    uint16_t val = base[idx];
    return (val >= 100 && val <= 2000) ? val : 0;
#endif
}

static uint16_t get_default_baseline(uint8_t col, uint8_t row) {
    uint16_t f_base = get_factory_baseline(col, row);
    return (f_base != 0) ? f_base : BOARD_DEFAULT_REST;
}

static uint16_t get_default_peak(uint8_t col, uint8_t row) {
    uint16_t base = get_default_baseline(col, row);
    uint32_t peak = (uint32_t)base + TOPRE_TYPICAL_TRAVEL_COUNTS;
    return (peak <= 4095) ? (uint16_t)peak : 4000;
}

#define WORD_ALIGNED_SIZE(n) (((n) + 3U) & ~3U)
bool hhkb_settings_storage_writable(void) {
    return hhkb_settings_region_writable((volatile const uint8_t *)USER_EEPROM_ADDR,
        sizeof(hhkb_eeprom_cal_t), offsetof(hhkb_eeprom_cal_t, rt),
        WORD_ALIGNED_SIZE(offsetof(hhkb_eeprom_cal_t, scan_rate)),
        sizeof(hhkb_eeprom_cal_t));
}

void hhkb_settings_mark_dirty(void) {
    s_settings_dirty_time = xTaskGetTickCount();
    s_settings_dirty = true;
}

void hhkb_calibration_save_to_eeprom(void) {
    if (!hhkb_settings_storage_writable()) return;

    hhkb_eeprom_cal_t cal;
    cal.magic = USER_EEPROM_MAGIC;
    cal.version = USER_EEPROM_VERSION;
    cal.led_modes = (uint8_t)((s_led_mode[0] & 0x0F) | (s_led_mode[1] << 4));
    cal.rt = s_rt_cfg;
    cal.classic_release_pm = s_classic_release_pm;
#ifdef BOARD_LED_SETTINGS /* the retired scan-rate byte holds the indicator style; bit 7 marks it */
    cal.scan_rate = (uint8_t)(0x80U | (s_led_level << 3) | s_led_colour);
#else
    cal.scan_rate = BOARD_SCAN_RATE; /* no longer a setting; the field stays for the record layout */
#endif
    cal.tapping_term_10ms = s_tapping_term_10ms;
    cal.quick_tap_10ms = s_quick_tap_10ms;
    cal.flags = (s_permissive_hold ? 1 : 0) | /* bits 1 (tap preferred), 2 (oversampling): retired, 0 */
                (s_custom_calibration ? 8 : 0) |
                ((s_debug_flags & DEBUG_FLAG_TOPRE) ? 16 : 0) |
                ((s_system & 1) << 5);

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            cal.baseline[c][r] = s_cal_baseline[c][r];
            cal.peak[c][r] = s_cal_peak[c][r];
        }
    }
    cal.crc16 = compute_crc16((const uint8_t *)&cal.baseline, sizeof(cal) - offsetof(hhkb_eeprom_cal_t, baseline));

    volatile uint32_t *dst = (volatile uint32_t *)USER_EEPROM_ADDR;
    // Magic word last: a save cut short leaves no record that looks valid.
    const size_t words = sizeof(hhkb_eeprom_cal_t) / 4;
    for (size_t n = 1; n <= words; n++) {
        size_t i = n % words;
        uint32_t w; // memcpy, not a uint32_t* cast: strict aliasing let GCC drop the byte-field stores
        memcpy(&w, (const uint8_t *)&cal + 4 * i, sizeof(w));
        if (dst[i] != w) {
            eeprom_write_word(&dst[i], w);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* "HKCD" record (ECK before the HKCE format): same offsets for calibration, rt and release. */
typedef struct __attribute__((aligned(4))) {
    uint32_t magic;
    uint8_t  actuation_pct;
    uint8_t  reserved[3]; // [0] 0x80 | scan_rate+1 | hold 0x10 | tap_pref 0x20 | oversampling 0x40;
                          // [1] tapping term 10 ms; [2] 0x80 | quick tap 10 ms
    uint16_t baseline[MATRIX_COLS][MATRIX_ROWS];
    uint16_t peak[MATRIX_COLS][MATRIX_ROWS];
    rt_cfg_t rt;
    uint8_t  custom_calibration;
    uint8_t  led_mode[2];
    uint8_t  system;
    uint16_t classic_release_pm;
} hhkb_eeprom_hkcd_t;
_Static_assert(sizeof(hhkb_eeprom_hkcd_t) == sizeof(hhkb_eeprom_cal_t) &&
               offsetof(hhkb_eeprom_hkcd_t, rt) == offsetof(hhkb_eeprom_cal_t, rt) &&
               offsetof(hhkb_eeprom_hkcd_t, classic_release_pm) == offsetof(hhkb_eeprom_cal_t, classic_release_pm),
               "HKCD layout");

/* Settings beyond calibration and rapid trigger, as the HKCD firmware read them. */
static void load_hkcd_settings(const volatile hhkb_eeprom_hkcd_t *old) {
    uint8_t r0 = old->reserved[0], r1 = old->reserved[1], r2 = old->reserved[2];
    if (r1 >= 5 && r1 <= 50) {
        s_tapping_term_10ms = r1;
        if ((r2 & 0x7f) <= 50) s_quick_tap_10ms = r2 & 0x7f;
    }
    if (r0 & 0x80) s_permissive_hold = (r0 & 0x10) != 0;
    if (old->classic_release_pm >= 25 && old->classic_release_pm <= 1000) s_classic_release_pm = old->classic_release_pm;
    s_custom_calibration = old->custom_calibration == 1;
    for (uint8_t i = 0; i < 2; i++)
        s_led_mode[i] = old->led_mode[i] <= HHKB_LED_MODE_MAX ? old->led_mode[i] : k_led_default[i];
    s_system = old->system <= 1 ? old->system : 0;
}

static void load_rt_cfg(const volatile rt_cfg_t *rt) {
    if (rt->act_pm >= 25 && rt->act_pm <= 1000) s_rt_cfg.act_pm = rt->act_pm;
    if (rt->press_pm >= 25 && rt->press_pm <= 1000) s_rt_cfg.press_pm = rt->press_pm;
    if (rt->release_pm >= 25 && rt->release_pm <= 1000) s_rt_cfg.release_pm = rt->release_pm;
    if (rt->reset_pm <= 150) s_rt_cfg.reset_pm = rt->reset_pm;
    if (s_rt_cfg.act_pm <= s_rt_cfg.reset_pm) s_rt_cfg.act_pm = s_rt_cfg.reset_pm + 25;
    s_rt_cfg.rt_enabled = rt->rt_enabled;
}

void hhkb_calibration_load(void) {
    const volatile hhkb_eeprom_cal_t *cal = (const volatile hhkb_eeprom_cal_t *)USER_EEPROM_ADDR;
    rt_cfg_defaults(&s_rt_cfg);
    s_rt_cfg.act_pm = 750;
    s_classic_release_pm = 100;
    s_rt_cfg.press_pm = 25;
    s_rt_cfg.release_pm = 25;
    s_rt_cfg.rt_enabled = false;
    s_debug_flags = 0;
    bool stored = false;

    // A bad CRC (e.g. power lost mid-save) falls through to the defaults below.
    if (cal->magic == USER_EEPROM_MAGIC && cal->crc16 == compute_crc16((const uint8_t *)&cal->baseline,
                                                     sizeof(*cal) - offsetof(hhkb_eeprom_cal_t, baseline))) {
        if (cal->tapping_term_10ms >= 5 && cal->tapping_term_10ms <= 50) s_tapping_term_10ms = cal->tapping_term_10ms;
        if (cal->quick_tap_10ms <= 50) s_quick_tap_10ms = cal->quick_tap_10ms;
        s_permissive_hold = (cal->flags & 1) != 0;
        s_custom_calibration = (cal->flags & 8) != 0;
        if (cal->flags & 16) {
            s_debug_flags |= DEBUG_FLAG_TOPRE;
        } else {
            s_debug_flags &= ~DEBUG_FLAG_TOPRE;
        }
        s_system = (cal->flags >> 5) & 1;
        if (cal->version >= 2) {
            uint8_t led0 = cal->led_modes & 0x0F, led1 = cal->led_modes >> 4;
            s_led_mode[0] = led0 <= HHKB_LED_MODE_MAX ? led0 : k_led_default[0];
            s_led_mode[1] = led1 <= HHKB_LED_MODE_MAX ? led1 : k_led_default[1];
        } else {
            s_led_mode[0] = (cal->flags >> 6) & 3; // v1 kept LED 0 modes 0..3 only
        }

#ifdef BOARD_LED_SETTINGS
        if (cal->scan_rate & 0x80U) { /* older records hold the scan rate: keep the defaults */
            uint8_t colour = cal->scan_rate & 7U, level = (cal->scan_rate >> 3) & 7U;
            if (colour) s_led_colour = colour;
            if (level >= 1 && level <= 5) s_led_level = level;
        }
#endif
        load_rt_cfg(&cal->rt);
        if (cal->classic_release_pm >= 25 && cal->classic_release_pm <= 1000) s_classic_release_pm = cal->classic_release_pm;

        stored = true;
    } else if (cal->magic == USER_EEPROM_LEGACY_MAGIC || cal->magic == USER_EEPROM_C_MAGIC || cal->magic == USER_EEPROM_PREV_MAGIC || cal->magic == USER_EEPROM_OLD_MAGIC) {
        // Legacy fallback
        if (cal->magic != USER_EEPROM_OLD_MAGIC) {
            load_rt_cfg(&cal->rt);
        }
        s_custom_calibration = true;
        if (cal->magic == USER_EEPROM_LEGACY_MAGIC) load_hkcd_settings((const volatile hhkb_eeprom_hkcd_t *)cal);
        stored = true;
    }

    // Stored calibration only with custom calibration on (HKCD without it kept 800/1800 placeholders);
    // otherwise factory baseline + typical travel.
    bool custom = stored && s_custom_calibration;
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            uint16_t b = cal->baseline[col][row], p = cal->peak[col][row];
            s_cal_baseline[col][row] = (custom && b >= 100 && b <= 2000) ? b : get_default_baseline(col, row);
            s_cal_peak[col][row] = (custom && p > s_cal_baseline[col][row] + RT_MIN_RANGE && p <= 4095)
                                       ? p : get_default_peak(col, row);
        }
    }

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            rt_key_set_range(&s_rt_keys[c][r], s_cal_baseline[c][r], s_cal_peak[c][r], s_rt_cfg.reset_pm);
            rt_key_reset(&s_rt_keys[c][r]);
        }
    }
}

/* Reset all settings (VIA): the restart waits until the VIA reply has gone out. */
#define SETTINGS_RESET_DELAY_MS 200U
static bool s_reset_pending;
static TickType_t s_reset_requested;

bool hhkb_settings_reset_request(void) {
    if (!hhkb_settings_storage_writable()) return false; /* not ECK's record: leave it */
    s_reset_requested = xTaskGetTickCount();
    s_reset_pending = true;
    return true;
}

static void settings_reset(void) {
    eeprom_write_word((volatile uint32_t *)USER_EEPROM_ADDR, 0); /* no magic: defaults at start-up */
    platform_system_reset();
}

void hhkb_settings_task(void) {
    TickType_t now = xTaskGetTickCount();
    if (s_reset_pending && (now - s_reset_requested) >= pdMS_TO_TICKS(SETTINGS_RESET_DELAY_MS)) settings_reset();
    if (!s_settings_dirty) return;
    if ((now - s_settings_dirty_time) >= pdMS_TO_TICKS(HHKB_SETTINGS_SAVE_DELAY_MS)) {
        taskENTER_CRITICAL();
        s_settings_dirty = false;
        taskEXIT_CRITICAL();
        hhkb_calibration_save_to_eeprom();
    }
}

void hhkb_calibration_start(void) {
    memset(s_cal_baseline_accum, 0, sizeof(s_cal_baseline_accum));
    s_cal_baseline_samples = CAL_BASELINE_SAMPLES;
#if KEY_MASK_WORDS == 2
    s_calibrated_mask[0] = 0;
    s_calibrated_mask[1] = 0;
    s_cal_down_mask[0] = 0;
    s_cal_down_mask[1] = 0;
#else
    memset(s_calibrated_mask, 0, sizeof(s_calibrated_mask));
    memset(s_cal_down_mask, 0, sizeof(s_cal_down_mask));
#endif
    s_capture_baseline = true;
    s_calibration_mode = true;
    if (hhkb_get_debug_flag(DEBUG_FLAG_CAL)) {
        debug_console_printf("\r\n[CAL] Calibration started: capturing baseline (16 samples)...\r\n");
    }
}

void hhkb_calibration_finish(void) {
    if (!s_calibration_mode) return;
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            uint8_t k_idx = KEY_INDEX(c, r);
            bool was_actuated = MASK_TEST(s_calibrated_mask, k_idx);
            if (was_actuated) {
                s_cal_baseline[c][r] = s_capture_base[c][r];
                s_cal_peak[c][r] = s_capture_peak[c][r];
            }
            rt_key_set_range(&s_rt_keys[c][r], s_cal_baseline[c][r], s_cal_peak[c][r], s_rt_cfg.reset_pm);
            rt_key_reset(&s_rt_keys[c][r]);
        }
    }
    if (hhkb_get_debug_flag(DEBUG_FLAG_CAL)) {
        uint8_t count = (uint8_t)MASK_COUNT(s_calibrated_mask);
        debug_console_printf("[CAL] Calibration complete and saved to EEPROM (%u/" STR(NUM_KEYS) " keys updated).\r\n", count);
    }
    s_calibration_mode = false;
    s_custom_calibration = true;
    hhkb_settings_mark_dirty();
}

#ifdef BOARD_BOOT_REST
/* Boards without factory rest levels: the scan driver reports each key's reading at start-up.
 * A reading above BOARD_BOOT_REST_MAX (key held) keeps the default. Custom calibration wins. */
void ec_boot_rest(uint8_t col, uint8_t row, uint16_t val) {
    if (s_custom_calibration || s_calibration_mode || val < 100 || val > BOARD_BOOT_REST_MAX) return;
    s_cal_baseline[col][row] = val;
    s_cal_peak[col][row] = (uint16_t)(val + TOPRE_TYPICAL_TRAVEL_COUNTS <= 4095 ? val + TOPRE_TYPICAL_TRAVEL_COUNTS : 4000);
    rt_key_set_range(&s_rt_keys[col][row], s_cal_baseline[col][row], s_cal_peak[col][row], s_rt_cfg.reset_pm);
    rt_key_reset(&s_rt_keys[col][row]);
}
#endif

bool hhkb_calibration_enabled(void) { return s_custom_calibration; }
void hhkb_calibration_set_enabled(bool enabled) {
    if (s_custom_calibration == enabled) return;
    s_custom_calibration = enabled;
    hhkb_settings_mark_dirty();
}
bool hhkb_is_calibrating(void) { return s_calibration_mode; }

uint8_t hhkb_active_scan_rate(void) {
#ifdef HHKB_HYBRID
    if (hybrid_ble_update_active()) return 0; /* 50 Hz: the scan would starve the update relay */
    if (hybrid_ble_transport_active()) return BLE_SCAN_RATE;
#endif
    return BOARD_SCAN_RATE;
}
/* USB host asleep: a key press there only wakes it (or nothing, with power saving), and the
 * Fn+Ctrl chords, so 50 Hz is enough. */
bool hhkb_usb_sleep_idle_allowed(void) {
#ifdef HHKB_HYBRID
    if (!hybrid_usb_asleep() || hybrid_ble_update_active()) return false;
#else
    if (!usb_host_asleep()) return false;
#endif
    return !s_calibration_mode;
}
bool hhkb_scan_idle_allowed(void) {
#ifdef HHKB_HYBRID
    return hybrid_on_battery() && hybrid_ble_transport_active() && !hybrid_ble_update_active() &&
           !s_calibration_mode;
#else
    return false;
#endif
}

uint8_t hhkb_get_hold_tap_setting(uint8_t id) {
    switch (id) {
    case 0: return s_tapping_term_10ms;
    case 1: return s_permissive_hold;
    case 3: return s_quick_tap_10ms;
    default: return 0;
    }
}

void hhkb_set_hold_tap_setting(uint8_t id, uint8_t value) {
    taskENTER_CRITICAL();
    if (id == 0 && value >= 5 && value <= 50) s_tapping_term_10ms = value;
    else if (id == 1 && value <= 1) s_permissive_hold = value;
    else if (id == 3 && value <= 50) s_quick_tap_10ms = value;
    else { taskEXIT_CRITICAL(); return; }
    taskEXIT_CRITICAL();
    hhkb_settings_mark_dirty();
}

uint16_t hold_tap_config_value(uint8_t id) {
    switch (id) {
    case 0: return (uint16_t)s_tapping_term_10ms * 10;
    case 1: return s_permissive_hold;
    case 3: return (uint16_t)s_quick_tap_10ms * 10;
    default: return 0;
    }
}

#ifdef BOARD_LED_SETTINGS
uint8_t hhkb_get_led_style(uint8_t which) { return which ? s_led_level : s_led_colour; }
bool hhkb_set_led_style(uint8_t which, uint8_t value) {
    if (value < 1 || value > (which ? 5 : 7)) return false;
    if (which) s_led_level = value;
    else s_led_colour = value;
    hhkb_settings_mark_dirty();
    return true;
}
#endif

uint8_t hhkb_get_led_mode(uint8_t index) { return index < 2 ? s_led_mode[index] : 0; }
void hhkb_set_led_mode(uint8_t index, uint8_t mode) {
    if (index < 2 && mode <= HHKB_LED_MODE_MAX) { s_led_mode[index] = mode; hhkb_settings_mark_dirty(); }
}

uint8_t hhkb_get_system(void) { return s_system; }
void hhkb_set_system(uint8_t system) {
    if (system <= 1 && s_system != system) { s_system = system; hhkb_settings_mark_dirty(); }
}

uint8_t hhkb_rt_get(uint8_t id) {
    switch (id) {
    case 1: return s_rt_cfg.act_pm / 25;
    case 3: return s_rt_cfg.rt_enabled;
    case 4: return s_rt_cfg.press_pm / 25;
    case 5: return s_classic_release_pm / 25;
    case 11: return s_rt_cfg.release_pm / 25;
    case 6: return s_rt_cfg.reset_pm / 25;
    default: return 0;
    }
}

void hhkb_rt_set(uint8_t id, uint8_t value) {
    taskENTER_CRITICAL();
    if (id == 3) {
        s_rt_cfg.rt_enabled = value != 0;
    } else if (id == 6) {
        if (value <= 6) {
            s_rt_cfg.reset_pm = value * 25;
            if (s_rt_cfg.act_pm <= s_rt_cfg.reset_pm) s_rt_cfg.act_pm = s_rt_cfg.reset_pm + 25;
            for (uint8_t c = 0; c < MATRIX_COLS; c++)
                for (uint8_t r = 0; r < MATRIX_ROWS; r++) rt_key_set_reset(&s_rt_keys[c][r], s_rt_cfg.reset_pm);
        }
    } else if (value >= 1 && value <= 40) {
        uint16_t pm = value * 25;
        if (id == 1) {
            if (pm <= s_rt_cfg.reset_pm) pm = s_rt_cfg.reset_pm + 25;
            s_rt_cfg.act_pm = pm;
        }
        if (id == 4) s_rt_cfg.press_pm = pm;
        if (id == 5) s_classic_release_pm = pm;
        if (id == 11) s_rt_cfg.release_pm = pm;
    }
    taskEXIT_CRITICAL();
    hhkb_settings_mark_dirty();
}

// Debug API
bool hhkb_get_debug_flag(uint8_t flag) {
    return (s_debug_flags & flag) != 0;
}

void hhkb_set_debug_flag(uint8_t flag, bool enabled) {
    if (enabled) {
        s_debug_flags |= flag;
    } else {
        s_debug_flags &= ~flag;
    }
    if (flag == DEBUG_FLAG_TOPRE) {
        hhkb_settings_mark_dirty();
    }
}

void hhkb_record_matrix_scan(void) {
    s_matrix_scans_total++;
}

uint32_t hhkb_get_matrix_rate_hz(void) {
    return s_matrix_rate_hz;
}

void hhkb_calc_rates(uint32_t elapsed_ms) {
    if (elapsed_ms == 0) return;
    uint32_t total = s_matrix_scans_total;
    uint32_t diff = total - s_matrix_scans_prev;
    s_matrix_scans_prev = total;
    s_matrix_rate_hz = (diff * 1000U + elapsed_ms / 2) / elapsed_ms; // 32-bit: diff is ~1000 per second
}


void hhkb_debug_feed_sample(uint8_t col, uint8_t row, uint16_t raw, uint16_t norm, bool is_pressed) {
    if (col >= MATRIX_COLS || row >= MATRIX_ROWS) return;

    uint16_t base = s_cal_baseline[col][row];
    bool was_pressed = is_reported_down(col, row);

    if (!hhkb_get_debug_flag(DEBUG_FLAG_TOPRE)) return;

    // 1. Immediate notification on state transitions (Press / Release)
    if (is_pressed != was_pressed) {
        set_reported_down(col, row, is_pressed);
        s_last_reported_raw[col][row] = raw;
        s_dbg_col = col;
        s_dbg_row = row;
        s_dbg_raw = raw;
        s_dbg_norm = norm;
        s_dbg_down = is_pressed;
        debug_console_printf("[Topre] C%u R%u: raw=%u base=%u peak=%u travel=%u%% %s\r\n",
            col, row, raw, base, s_cal_peak[col][row], norm / 10, is_pressed ? "DN" : "UP");
        return;
    }

    // 2. Continuous travel updates while key is pressed or significantly actuated (travel >= 5%)
    if (is_pressed || norm >= 50) {
        int16_t diff = (int16_t)(raw - s_last_reported_raw[col][row]);
        if (diff > 40 || diff < -40) {
            s_last_reported_raw[col][row] = raw;
            s_dbg_col = col;
            s_dbg_row = row;
            s_dbg_raw = raw;
            s_dbg_norm = norm;
            s_dbg_down = is_pressed;
            debug_console_printf("[Topre] C%u R%u: raw=%u base=%u peak=%u travel=%u%% %s\r\n",
                col, row, raw, base, s_cal_peak[col][row], norm / 10, is_pressed ? "DN" : "UP");
        }
    } else if (norm < 50 && !is_pressed && s_last_reported_raw[col][row] != base) {
        s_last_reported_raw[col][row] = base;
    }
}

void hhkb_get_debug_key_str(char *buf, size_t max_len) {
    fmt_snprintf(buf, max_len, "C%u R%u (%s)", s_dbg_col, s_dbg_row, s_dbg_down ? "Pressed" : "Released");
}

void hhkb_get_debug_raw_str(char *buf, size_t max_len) {
    fmt_snprintf(buf, max_len, "Raw:%u (B:%u P:%u)", s_dbg_raw, s_cal_baseline[s_dbg_col][s_dbg_row], s_cal_peak[s_dbg_col][s_dbg_row]);
}

void hhkb_get_debug_travel_str(char *buf, size_t max_len) {
    fmt_snprintf(buf, max_len, "Travel: %u.%u%%", s_dbg_norm / 10, s_dbg_norm % 10);
}

void hhkb_get_cal_status_str(char *buf, size_t max_len) {
    if (s_calibration_mode) {
        uint8_t count = (uint8_t)MASK_COUNT(s_calibrated_mask);
        fmt_snprintf(buf, max_len, "In progress: %u/" STR(NUM_KEYS) " keys", count);
    } else {
        uint8_t custom_count = 0;
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                if (s_cal_peak[c][r] >= s_cal_baseline[c][r] + RT_MIN_RANGE) {
                    custom_count++;
                }
            }
        }
        fmt_snprintf(buf, max_len, "Saved (%u/" STR(NUM_KEYS) " keys)", custom_count);
    }
}

/* Keeps the caller's interrupt state: the Hybrid's deep-sleep key check scans with
 * interrupts off, and taskEXIT_CRITICAL would turn them back on. */
rt_cfg_t hhkb_get_scan_cfg(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    rt_cfg_t cfg = s_rt_cfg;
    if (!cfg.rt_enabled) cfg.release_pm = s_classic_release_pm;
    __set_PRIMASK(primask);
    return cfg;
}

void hhkb_update_dynamic_peak(uint8_t col, uint8_t row, uint16_t val) {
    s_cal_peak[col][row] = val;
    rt_key_set_range(&s_rt_keys[col][row], s_cal_baseline[col][row], s_cal_peak[col][row], s_rt_cfg.reset_pm);
}
void hhkb_cal_accumulate_baseline(uint8_t col, uint8_t row, uint16_t val) {
    s_cal_baseline_accum[col][row] += val;
}
void hhkb_cal_record_peak(uint8_t col, uint8_t row, uint16_t val) {
    if (val > s_capture_peak[col][row]) s_capture_peak[col][row] = val;

    uint16_t press_thresh = s_capture_base[col][row] + CAL_MIN_SWING;
    uint16_t release_thresh = s_capture_base[col][row] + (CAL_MIN_SWING / 2);
    uint8_t k_idx = KEY_INDEX(col, row);
    bool is_down = MASK_TEST(s_cal_down_mask, k_idx);

    if (!is_down && val >= press_thresh) {
        // Transition: key pressed down
        MASK_SET(s_cal_down_mask, k_idx);

        s_cal_press_start_peak[col][row] = s_capture_peak[col][row];

        bool newly_calibrated = false;
#if KEY_MASK_WORDS == 2 /* the HHKB's original form (its binaries stay unchanged) */
        if (k_idx < 32) {
            if (!(s_calibrated_mask[0] & (1U << k_idx))) {
                s_calibrated_mask[0] |= (1U << k_idx);
                newly_calibrated = true;
            }
        } else {
            if (!(s_calibrated_mask[1] & (1U << (k_idx - 32)))) {
                s_calibrated_mask[1] |= (1U << (k_idx - 32));
                newly_calibrated = true;
            }
        }
#else
        if (!MASK_TEST(s_calibrated_mask, k_idx)) {
            MASK_SET(s_calibrated_mask, k_idx);
            newly_calibrated = true;
        }
#endif
        if (newly_calibrated && hhkb_get_debug_flag(DEBUG_FLAG_CAL)) {
            uint8_t count = (uint8_t)MASK_COUNT(s_calibrated_mask);
            uint8_t pct = (uint8_t)((count * 100U) / NUM_KEYS);
            char bar[21];
            uint8_t filled = (uint8_t)((count * 20U) / NUM_KEYS);
            for (uint8_t b = 0; b < 20; b++) bar[b] = (b < filled) ? '=' : ' ';
            bar[20] = '\0';
            debug_console_printf("[CAL] [%s] %u%% (%u/" STR(NUM_KEYS) ") | Key C%u R%u peak: %u\r\n",
                                 bar, pct, count, col, row, s_capture_peak[col][row]);
            if (count >= NUM_KEYS) {
                debug_console_printf("[CAL] >>> All " STR(NUM_KEYS) " keys calibrated! Click 'Finish and save' in VIA. <<<\r\n");
            }
        }
    } else if (is_down && val < release_thresh) {
        // Transition: key released
        MASK_CLEAR(s_cal_down_mask, k_idx);

        uint16_t old_peak = s_cal_press_start_peak[col][row];
        uint16_t new_peak = s_capture_peak[col][row];
        if (old_peak > press_thresh && new_peak > old_peak + 20 && hhkb_get_debug_flag(DEBUG_FLAG_CAL)) {
            debug_console_printf("[CAL] Key C%u R%u peak updated: %u -> %u\r\n", col, row, old_peak, new_peak);
        }
    }
}
bool hhkb_cal_is_capturing_baseline(void) { return s_calibration_mode && s_capture_baseline; }
void hhkb_cal_decrement_samples(void) {
    if (--s_cal_baseline_samples == 0) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                s_capture_base[c][r] = (uint16_t)(s_cal_baseline_accum[c][r] / CAL_BASELINE_SAMPLES);
                s_capture_peak[c][r] = s_capture_base[c][r];
                s_cal_press_start_peak[c][r] = s_capture_base[c][r];
            }
        }
        s_capture_baseline = false;
        if (hhkb_get_debug_flag(DEBUG_FLAG_CAL)) {
            debug_console_printf("[CAL] Baseline captured! Press every key firmly to record peak travel.\r\n");
        }
    }
}
