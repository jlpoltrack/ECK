#include "matrix_scan.h"
#ifdef HHKB_ADC_DIAGNOSTICS
#include "task_internal.h"
#endif
#include "via.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifdef HHKB_HYBRID
#include "hybrid_ble.h"
#include "ble_link.h"
/* Hybrid and diagnostic IDs follow the shared IDs 1..28 (29, the Bluetooth scan rate, is retired). */
#define HHKB_ID_NRF_UPDATE 30 /* tools/hybrid_update.py nrf */
#define NRF_INSTALL_QUIET_MS 30000U

/* Set: [3] op (BLE_LINK_UPD_*, 4 = restart the nRF52, 5 = restart it in recovery mode), START [4..7]
 * size [8..23] MD5, DATA [4..7] offset [8] n [9..] data; [3] answers 0 = taken, 1 = busy.
 * Get: [3] state, [4..7] staged, [8] pending, [9] failed, [10] recovery mode. */
static bool nrf_update_command(uint8_t const *request, uint8_t *response) {
    if (request[0] == 0x08) {
        uint8_t state;
        uint32_t staged;
        bool pending, failed;
        hybrid_ble_update_status(&state, &staged, &pending, &failed);
        response[3] = state;
        memcpy(&response[4], &staged, sizeof(staged));
        response[8] = pending;
        response[9] = failed;
        response[10] = (hybrid_ble_flags() & BLE_LINK_ST_RECOVERY) != 0;
        return true;
    }
    if (request[0] != 0x07) return false;
    uint8_t op = request[3], buf[BLE_LINK_DATA_MAX], len = 0;
    buf[0] = op;
    if (op == BLE_LINK_UPD_START) {
        memcpy(&buf[1], &request[4], 20);
        len = 21;
    } else if (op == BLE_LINK_UPD_DATA && request[8] <= BLE_LINK_UPD_CHUNK) {
        memcpy(&buf[1], &request[4], 4);
        memcpy(&buf[5], &request[9], request[8]);
        len = (uint8_t)(5 + request[8]);
    } else if (op == BLE_LINK_UPD_FINISH) {
        len = 1;
    } else if (op == 4 || op == 5) {
        hybrid_ble_nrf_restart(NRF_INSTALL_QUIET_MS, op == 5);
        response[3] = 0;
        return true;
    } else {
        return false;
    }
    response[3] = hybrid_ble_update_submit(buf, len) ? 0 : 1;
    return true;
}
#endif
#define HHKB_ID_ADC_DIAG 31 /* tools/bench/key_depth.py (HHKB_ADC_DIAGNOSTICS builds) */
#define HHKB_ID_RESET_ARM 32 /* "Allow settings reset": not saved, off at start-up */
#define HHKB_ID_RESET 33     /* "Reset all settings": only while armed; restarts the keyboard */
static bool s_reset_armed;

/* Channel 0, the via.json "HHKB Settings" menu: IDs 1..28 on every model, 29..31 as above. */
bool hhkb_via_command(uint8_t const *request, uint8_t *response) {
    if (request[1] != 0) return false;
    uint8_t id = request[2];
    if (request[0] == 0x09) { hhkb_settings_mark_dirty(); return true; }
#ifdef HHKB_ID_NRF_UPDATE
    if (id == HHKB_ID_NRF_UPDATE) return nrf_update_command(request, response);
#endif
#ifdef HHKB_ADC_DIAGNOSTICS
    if (id == HHKB_ID_ADC_DIAG && request[0] == 0x08) { /* [3] kind, [4] first key */
        if (request[3] == 5) stack_diag_read(&response[5]);
        else hhkb_adc_diag_read(request[3], request[4], &response[5]);
        return true;
    }
#endif
#define HHKB_ID_BLE_CONN 36 /* Bluetooth connection timing the host granted (text, read-only) */
    if (id == HHKB_ID_BLE_CONN) {
        if (request[0] != 0x08) return false;
#ifdef HHKB_HYBRID
        hybrid_ble_conn_str((char *)&response[3], 28);
#else
        strcpy((char *)&response[3], "no Bluetooth");
#endif
        return true;
    }
    if (id == HHKB_ID_RESET_ARM || id == HHKB_ID_RESET) {
        if (request[0] == 0x08) response[3] = id == HHKB_ID_RESET_ARM && s_reset_armed;
        else if (request[0] != 0x07 || request[3] > 1) return false;
        else if (id == HHKB_ID_RESET_ARM) s_reset_armed = request[3] != 0;
        else if (request[3] && s_reset_armed) s_reset_armed = !hhkb_settings_reset_request();
        return true;
    }
#ifdef BOARD_LED_SETTINGS
#define HHKB_ID_LED_COLOUR 34 /* indicator colour 1-7 and brightness 1-5 (boards with RGB indicators) */
    if (id == HHKB_ID_LED_COLOUR || id == HHKB_ID_LED_COLOUR + 1) {
        if (request[0] == 0x08) response[3] = hhkb_get_led_style(id - HHKB_ID_LED_COLOUR);
        else if (request[0] != 0x07 || !hhkb_set_led_style(id - HHKB_ID_LED_COLOUR, request[3])) return false;
        return true;
    }
#endif
    if (id < 1 || id > 28) return false;
    if (id == 13 || id == 16 || id == 18) return false; /* retired: scan rate, tap preferred, oversampling */
    if (request[0] == 0x08) {
        if (id == 12) {
            const char *status = hhkb_settings_storage_writable() ? "Writable" : "Occupied: not saved";
            strcpy((char *)&response[3], status);
        } else if (id == 28) {
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
            strncpy((char *)&response[3], FW_VERSION, 28);
            response[31] = '\0';
        } else if (id == 19) {
            response[3] = hhkb_get_debug_flag(DEBUG_FLAG_TOPRE);
        } else if (id == 23) {
            response[3] = hhkb_get_debug_flag(DEBUG_FLAG_VIA);
        } else if (id == 24) {
            response[3] = hhkb_get_debug_flag(DEBUG_FLAG_RATE);
        } else if (id == 25) {
            response[3] = hhkb_get_debug_flag(DEBUG_FLAG_CAL);
        } else if (id == 26) {
            hhkb_get_cal_status_str((char *)&response[3], 28);
        } else if (id == 27) {
            response[3] = 0;
        } else if (id == 20) {
            hhkb_get_debug_key_str((char *)&response[3], 28);
        } else if (id == 21) {
            hhkb_get_debug_raw_str((char *)&response[3], 28);
        } else if (id == 22) {
            hhkb_get_debug_travel_str((char *)&response[3], 28);
        } else if (id >= 14) {
            response[3] = hhkb_get_hold_tap_setting(id - 14);
        } else if (id == 2) {
            response[3] = hhkb_calibration_enabled();
        } else if (id == 9 || id == 10) {
            response[3] = hhkb_get_led_mode(id - 9);
        } else if (id == 7 || id == 8) {
            response[3] = 0;
        } else {
            response[3] = hhkb_rt_get(id);
        }
        return true;
    }
    if (request[0] == 0x07) {
        if (id == 12 || id == 26 || id == 28 || (id >= 20 && id <= 22)) return false;
        if (id == 19) {
            if (request[3] > 1) return false;
            hhkb_set_debug_flag(DEBUG_FLAG_TOPRE, request[3] != 0);
        } else if (id == 23) {
            if (request[3] > 1) return false;
            hhkb_set_debug_flag(DEBUG_FLAG_VIA, request[3] != 0);
        } else if (id == 24) {
            if (request[3] > 1) return false;
            hhkb_set_debug_flag(DEBUG_FLAG_RATE, request[3] != 0);
        } else if (id == 25) {
            if (request[3] > 1) return false;
            hhkb_set_debug_flag(DEBUG_FLAG_CAL, request[3] != 0);
        } else if (id >= 14) {
            hhkb_set_hold_tap_setting(id - 14, request[3]);
        } else if (id == 2) {
            hhkb_calibration_set_enabled(request[3] != 0);
        } else if (id == 7 && request[3]) {
            hhkb_calibration_start();
        } else if (id == 8 && request[3]) {
            hhkb_calibration_finish();
        } else if (id == 9 || id == 10) {
            hhkb_set_led_mode(id - 9, request[3]);
        } else if (id == 27) {
            // Refresh action: triggers VIA to re-read submenu controls
        } else if (id != 7 && id != 8) {
            hhkb_rt_set(id, request[3]);
        }
        return true;
    }
    return false;
}
