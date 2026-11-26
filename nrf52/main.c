/* ECK Bluetooth bridge for the HHKB Professional Hybrid's nRF52832.
 * Runs on Nordic's S132 v7.2.0, app at 0x26000. Receives HID reports from the STM32 over SPI
 * (common/ble_link.h) and sends them as a BLE HID keyboard; relays VIA requests to the STM32. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_error.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_bas.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_dis.h"
#include "ble_hci.h"
#include "ble_hids.h"
#include "nrf_ble_lesc.h"
#include "ble_srv_common.h"
#include "fds.h"
#include "nordic_common.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_delay.h"
#include "nrf_fstorage.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrfx_spis.h"
#include "peer_manager.h"

#include "ble_link.h"
#include "nrf_update.h"

#define DEVICE_NAME_PREFIX "HHKB-Hybrid_" /* + slot number, as stock */
#define MANUFACTURER_NAME "ECK"
#define APP_BLE_CONN_CFG_TAG 1
#define APP_BLE_OBSERVER_PRIO 3

/* Link pins (nRF52 side) */
#define PIN_CSN 22
#define PIN_SCK 25
#define PIN_MOSI 23 /* STM32 PB5 */
#define PIN_MISO 24 /* STM32 PB4 */
#define PIN_RECOVERY 18 /* STM32 PB6: high while the chip starts = recovery mode */

#define ADV_FAST_INTERVAL 0x0028 /* 25 ms */
#define ADV_FAST_DURATION 3000   /* 30 s */
#define ADV_SLOW_INTERVAL 0x0640 /* 1 s */
#define ADV_SLOW_DURATION 0      /* until connected */

/* 7.5-15 ms as ZMK asks: lower input lag on hosts that grant it. Apple's guidelines (R32 57.6)
 * want min >= 15 ms, so a Mac picks its own (VIA shows the granted values); latency <= 30, 6 s. */
#define MIN_CONN_INTERVAL 6 /* 7.5 ms in 1.25 ms units */
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(15, UNIT_1_25_MS)
#define SLAVE_LATENCY 30
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(6000, UNIT_10_MS)

#define KB_REPORT_LEN 8
#define CONSUMER_REPORT_LEN 2
#define REPORT_KB 0
#define REPORT_CONSUMER 1
#define REPORT_ID_KB 1
#define REPORT_ID_CONSUMER 2
#define OUTPUT_REPORT_LEN 1

/* VIA raw HID: its own HID service, appended last (bonded hosts keep their handles), so its
 * reports carry no report ID, as on USB. */
#define RAW_REPORT_LEN BLE_LINK_RAW_LEN

BLE_HIDS_DEF(m_hids, NRF_SDH_BLE_TOTAL_LINK_COUNT, KB_REPORT_LEN, CONSUMER_REPORT_LEN, OUTPUT_REPORT_LEN);
BLE_HIDS_DEF(m_raw, NRF_SDH_BLE_TOTAL_LINK_COUNT, RAW_REPORT_LEN, RAW_REPORT_LEN);
BLE_BAS_DEF(m_bas);
NRF_BLE_GATT_DEF(m_gatt);
NRF_BLE_QWR_DEF(m_qwr);
BLE_ADVERTISING_DEF(m_advertising);

static const nrfx_spis_t m_spis = NRFX_SPIS_INSTANCE(1);

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
static bool m_secured;
static bool m_advertising_on;
static uint8_t m_host_leds;
static ble_gap_conn_params_t m_granted; /* as the host set them, for the status reply */
static uint8_t m_slot;              /* host slot from the STM32; 0 = not told yet (acts as slot 1) */
static pm_peer_id_t m_peer_id = PM_PEER_ID_INVALID;
static bool m_pairing;              /* pairing window: advertise to anyone, allow re-pairing */
static bool m_passkey_wait;         /* the host shows a passkey for the user to type */
static uint8_t m_bonded_slots;      /* bit n-1: slot n has a bonded host */
static uint8_t m_deletes_pending;   /* advertising waits until bond deletes finish */
static bool m_updating;             /* staging an nRF52 image: SoftDevice off until reset */
static bool m_recovery;             /* recovery mode: no SoftDevice, only image updates */
static bool m_clearing;             /* all bonds being deleted */
static bool m_power_off;            /* STM32 asked for System OFF */
static volatile bool m_off_check;
static uint8_t m_off_waits;
static bool m_disc_pending;         /* disconnecting once the all-released reports reach the host */
static volatile bool m_disc_force;  /* the release wait timed out */
static uint8_t m_inflight;          /* notifications sent but not yet acknowledged by the host */
APP_TIMER_DEF(m_disc_timer);
#define DISC_RELEASE_MS 300
APP_TIMER_DEF(m_pair_timer);
APP_TIMER_DEF(m_sec_timer);
#define PAIR_WINDOW_MS 600000 /* stock: 10 min in pairing mode */
#define SECURE_TIMEOUT_MS 30000
APP_TIMER_DEF(m_reject_timer);
#define REJECT_HOLD_MS 30000 /* no advertising for this long after a host rejected our keys */
static bool m_host_rejected;        /* the slot's host failed security (it forgot us): STM32 blinks orange */
APP_TIMER_DEF(m_off_timer);
#define OFF_CHECK_MS 100
#define OFF_CONN_WAITS 30 /* give up waiting for the disconnect after 3 s */

/* VIA over Bluetooth: the host's request waits here until the STM32 replies (ble_link.h v8). */
static uint8_t m_raw_req[RAW_REPORT_LEN];
static uint8_t m_raw_resp[RAW_REPORT_LEN];
static uint8_t m_raw_id;          /* id of the newest request, 1..31 */
static bool m_raw_waiting;        /* shown to the STM32 in the status until both reply halves came */
static uint8_t m_raw_halves;      /* reply halves received for m_raw_id */
static uint8_t m_raw_part;        /* request part the next status shows */
static bool m_raw_resp_ready;     /* reply waiting for a notification slot */
static bool m_latency_off;
APP_TIMER_DEF(m_raw_timer);
#define RAW_SESSION_MS 5000 /* slave latency stays off this long after a request; unanswered requests expire */

/* SPI buffers: the status for the next transaction is prepared after each one. */
static ble_link_frame_t m_rx;
static ble_link_frame_t m_tx;
static volatile bool m_rx_pending;
static volatile uint32_t m_rx_amount;
static uint8_t m_last_seq;

/* Reports waiting for a free notification slot. */
#define QUEUE_LEN 16
typedef struct {
    uint8_t index;
    uint8_t len;
    uint8_t data[KB_REPORT_LEN];
} pending_report_t;
static pending_report_t m_queue[QUEUE_LEN];
static uint8_t m_q_head, m_q_count;


void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

static void error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

/* ---------- report queue ---------- */

static bool queue_push(uint8_t index, const uint8_t *data, uint8_t len) {
    if (m_q_count == QUEUE_LEN) return false;
    pending_report_t *r = &m_queue[(m_q_head + m_q_count) % QUEUE_LEN];
    r->index = index;
    r->len = len;
    memcpy(r->data, data, len);
    m_q_count++;
    return true;
}

static void queue_drain(void) {
    while (m_q_count && m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        pending_report_t *r = &m_queue[m_q_head];
        ret_code_t err = ble_hids_inp_rep_send(&m_hids, r->index, r->len, r->data, m_conn_handle);
        if (err == NRF_ERROR_RESOURCES) return; /* retried on BLE_GATTS_EVT_HVN_TX_COMPLETE */
        if (err == NRF_SUCCESS) m_inflight++;
        if (err != NRF_SUCCESS && err != NRF_ERROR_INVALID_STATE && err != BLE_ERROR_GATTS_SYS_ATTR_MISSING &&
            err != NRF_ERROR_FORBIDDEN && err != BLE_ERROR_INVALID_CONN_HANDLE) {
            APP_ERROR_CHECK(err);
        }
        m_q_head = (m_q_head + 1) % QUEUE_LEN;
        m_q_count--;
    }
}

/* ---------- VIA over Bluetooth ---------- */

static void raw_drain(void) {
    if (!m_raw_resp_ready || m_conn_handle == BLE_CONN_HANDLE_INVALID) return;
    ret_code_t err = ble_hids_inp_rep_send(&m_raw, 0, RAW_REPORT_LEN, m_raw_resp, m_conn_handle);
    if (err == NRF_ERROR_RESOURCES) return; /* retried on BLE_GATTS_EVT_HVN_TX_COMPLETE */
    if (err == NRF_SUCCESS) m_inflight++;
    m_raw_resp_ready = false; /* other errors: not subscribed or gone; the host asks again */
}

static void latency_set(bool off) {
    if (off == m_latency_off || m_conn_handle == BLE_CONN_HANDLE_INVALID) return;
    ble_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.gap_opt.slave_latency_disable.conn_handle = m_conn_handle;
    opt.gap_opt.slave_latency_disable.disable = off;
    if (sd_ble_opt_set(BLE_GAP_OPT_SLAVE_LATENCY_DISABLE, &opt) == NRF_SUCCESS) m_latency_off = off;
}

/* Slave latency would hold each host write up to 30 connection intervals. */
static void raw_session_end(void *p_context) {
    (void)p_context;
    latency_set(false);
    m_raw_waiting = false;
}

static void raw_reset(void) {
    (void)app_timer_stop(m_raw_timer);
    m_raw_waiting = false;
    m_raw_resp_ready = false;
    m_latency_off = false; /* per connection */
}

/* ---------- battery ---------- */

/* A failed notification is not worth a restart: the host reads the level on its next update or
 * reconnection. */
static void battery_update(uint8_t percent) {
    ret_code_t err = ble_bas_battery_level_update(&m_bas, percent, BLE_CONN_HANDLE_ALL);
    if (err != NRF_SUCCESS) NRF_LOG_WARNING("battery level update failed: %u", err);
}

/* ---------- advertising and bonds ---------- */

static uint8_t current_slot(void) {
    return m_slot ? m_slot : 1;
}

/* Host slot a bonded peer belongs to (stored as its peer-manager app data); 0 when untagged,
 * e.g. bonds left by PFU's firmware, which are deleted at start-up. */
static uint8_t peer_slot(pm_peer_id_t id) {
    uint32_t tag = 0;
    uint32_t len = sizeof(tag);
    if (pm_peer_data_app_data_load(id, &tag, &len) != NRF_SUCCESS || (tag >> 8) != 0xD3C0U) return 0;
    return (uint8_t)tag;
}

/* Each slot is a separate keyboard to hosts: slot 1 keeps the chip's own static address,
 * slots 2-4 change its lowest byte, so a host bonded in one slot never sees another. */
static void slot_address_set(void) {
    ble_gap_addr_t addr = {.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC};
    uint32_t a0 = NRF_FICR->DEVICEADDR[0], a1 = NRF_FICR->DEVICEADDR[1];
    memcpy(addr.addr, &a0, 4);
    addr.addr[4] = (uint8_t)a1;
    addr.addr[5] = (uint8_t)(a1 >> 8) | 0xC0;
    addr.addr[0] = (uint8_t)(addr.addr[0] + current_slot() - 1);
    APP_ERROR_CHECK(pm_id_addr_set(&addr));
    char name[] = DEVICE_NAME_PREFIX "0";
    name[sizeof(name) - 2] = (char)('0' + current_slot());
    ble_gap_conn_sec_mode_t open;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&open);
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&open, (const uint8_t *)name, strlen(name)));
}

static void bonded_slots_refresh(void) {
    uint8_t mask = 0;
    for (pm_peer_id_t id = pm_next_peer_id_get(PM_PEER_ID_INVALID); id != PM_PEER_ID_INVALID;
         id = pm_next_peer_id_get(id)) {
        uint8_t slot = peer_slot(id);
        if (slot >= 1 && slot <= BLE_LINK_SLOTS) mask |= (uint8_t)(1U << (slot - 1));
    }
    m_bonded_slots = mask;
}

/* Only the current slot's host may connect. Call with advertising stopped. */
static uint32_t whitelist_refresh(void) {
    pm_peer_id_t peers[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
    uint32_t count = 0;
    for (pm_peer_id_t id = pm_next_peer_id_get(PM_PEER_ID_INVALID);
         id != PM_PEER_ID_INVALID && count < BLE_GAP_WHITELIST_ADDR_MAX_COUNT; id = pm_next_peer_id_get(id)) {
        if (peer_slot(id) == current_slot()) peers[count++] = id;
    }
    APP_ERROR_CHECK(pm_whitelist_set(count ? peers : NULL, count));
    ret_code_t err = pm_device_identities_list_set(count ? peers : NULL, count);
    if (err != NRF_ERROR_NOT_SUPPORTED) APP_ERROR_CHECK(err);
    return count;
}

static void advertising_init(bool discoverable);
static void disconnect_now(void);

/* The single place advertising starts: stop it, set this slot's address and whitelist, then
 * advertise to the slot's host, or to anyone during a pairing window. An empty slot stays quiet. */
static void advertising_restart(void) {
    if (m_power_off) return;
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID || m_deletes_pending) return; /* resumes later */
    (void)sd_ble_gap_adv_stop(m_advertising.adv_handle);
    m_advertising_on = false;
    bonded_slots_refresh();
    slot_address_set();
    uint32_t bonded = whitelist_refresh();
    if (!bonded && !m_pairing) return;
    advertising_init(m_pairing);
    APP_ERROR_CHECK(ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST));
}

static void pairing_end(void) {
    m_pairing = false;
    (void)app_timer_stop(m_pair_timer);
}

static void pair_timeout(void *p_context) {
    (void)p_context;
    pairing_end();
    advertising_restart(); /* back to the slot's host, or quiet */
}

static void security_timeout(void *p_context) {
    (void)p_context;
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID && !m_secured) {
        (void)sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    }
}

static void reject_hold_end(void *p_context) {
    (void)p_context;
    advertising_restart();
}

/* Our keys failed with the slot's host (it forgot us, or lost its keys), outside a pairing window. */
static void host_rejected(void) {
    m_host_rejected = true;
    disconnect_now(); /* advertising waits REJECT_HOLD_MS (disconnect event) */
}

static void on_adv_evt(ble_adv_evt_t evt) {
    switch (evt) {
    case BLE_ADV_EVT_IDLE:
        m_advertising_on = false;
        break;
    case BLE_ADV_EVT_WHITELIST_REQUEST: {
        ble_gap_addr_t addrs[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
        ble_gap_irk_t irks[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
        uint32_t addr_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT, irk_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
        if (m_pairing) {
            addr_cnt = irk_cnt = 0; /* an empty reply advertises without a filter */
        } else {
            APP_ERROR_CHECK(pm_whitelist_get(addrs, &addr_cnt, irks, &irk_cnt));
        }
        APP_ERROR_CHECK(ble_advertising_whitelist_reply(&m_advertising, addrs, addr_cnt, irks, irk_cnt));
        break;
    }
    default:
        m_advertising_on = true;
        break;
    }
}

static void deletes_done(void) {
    if (m_deletes_pending && --m_deletes_pending == 0) advertising_restart();
}

/* A new bond belongs to the slot selected while pairing. FDS writes from this buffer later,
 * so it must outlive the call; a busy store is retried on the next peer manager event. */
static uint32_t m_slot_tag;
static pm_peer_id_t m_tag_peer = PM_PEER_ID_INVALID;

static void slot_tag_store(void) {
    if (m_tag_peer == PM_PEER_ID_INVALID) return;
    ret_code_t err = pm_peer_data_app_data_store(m_tag_peer, &m_slot_tag, sizeof(m_slot_tag), NULL);
    if (err == NRF_ERROR_BUSY || err == NRF_ERROR_STORAGE_FULL) return;
    APP_ERROR_CHECK(err);
    m_tag_peer = PM_PEER_ID_INVALID;
}

static void pm_evt_handler(pm_evt_t const *p_evt) {
    switch (p_evt->evt_id) {
    case PM_EVT_CONN_SEC_SUCCEEDED:
        m_peer_id = p_evt->peer_id;
        m_secured = true;
        m_passkey_wait = false;
        m_host_rejected = false;
        /* A new bond ends the pairing window (and its LED), whatever the flash write reports. */
        if (p_evt->params.conn_sec_succeeded.procedure == PM_CONN_SEC_PROCEDURE_BONDING) {
            m_slot_tag = 0xD3C000U | current_slot();
            m_tag_peer = p_evt->peer_id;
            pairing_end();
        }
        queue_drain();
        break;
    case PM_EVT_CONN_SEC_FAILED: {
        /* Key or pairing failures mean the host no longer has our bond; a dropped link is not one. */
        pm_sec_error_code_t err = p_evt->params.conn_sec_failed.error;
        if (!m_pairing && err != PM_CONN_SEC_ERROR_DISCONNECT && err != PM_CONN_SEC_ERROR_SMP_TIMEOUT) host_rejected();
        break;
    }
    case PM_EVT_CONN_SEC_CONFIG_REQ: {
        /* A host that forgot us may pair again only while the user asked for pairing (Fn+Ctrl+Q). */
        pm_conn_sec_config_t cfg = {.allow_repairing = m_pairing};
        pm_conn_sec_config_reply(p_evt->conn_handle, &cfg);
        break;
    }
    case PM_EVT_STORAGE_FULL: {
        ret_code_t err = fds_gc();
        if (err != FDS_ERR_NO_SPACE_IN_QUEUES) APP_ERROR_CHECK(err);
        break;
    }
    case PM_EVT_PEERS_DELETE_SUCCEEDED:
        m_clearing = false;
        advertising_restart();
        break;
    case PM_EVT_PEER_DELETE_SUCCEEDED:
    case PM_EVT_PEER_DELETE_FAILED:
        deletes_done();
        break;
    case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
        if (p_evt->params.peer_data_update_succeeded.data_id == PM_PEER_DATA_ID_APPLICATION) {
            bonded_slots_refresh();
        }
        break;
    case PM_EVT_PEER_DATA_UPDATE_FAILED:
        APP_ERROR_CHECK(p_evt->params.peer_data_update_failed.error);
        break;
    case PM_EVT_PEERS_DELETE_FAILED:
        m_clearing = false;
        APP_ERROR_CHECK(p_evt->params.peers_delete_failed_evt.error);
        break;
    case PM_EVT_ERROR_UNEXPECTED:
        APP_ERROR_CHECK(p_evt->params.error_unexpected.error);
        break;
    default:
        break;
    }
    slot_tag_store();
}

/* Delete bonds this firmware did not make (PFU's app leaves several per host, which the
 * SoftDevice rejects as duplicate identities). */
static void foreign_peers_delete(void) {
    for (pm_peer_id_t id = pm_next_peer_id_get(PM_PEER_ID_INVALID); id != PM_PEER_ID_INVALID;
         id = pm_next_peer_id_get(id)) {
        if (peer_slot(id) == 0 && pm_peer_delete(id) == NRF_SUCCESS) m_deletes_pending++;
    }
}

/* ---------- link ---------- */

static void status_prepare(bool refused) {
    memset(&m_tx, 0, sizeof(m_tx));
    m_tx.magic = BLE_LINK_MAGIC_TO_STM;
    m_tx.seq = m_last_seq;
    m_tx.type = BLE_LINK_STATUS;
    m_tx.len = 26;
    m_tx.data[0] = (m_secured ? BLE_LINK_ST_CONNECTED : 0) | (m_advertising_on ? BLE_LINK_ST_ADVERTISING : 0) |
                   (refused ? BLE_LINK_ST_QUEUE_FULL : 0) | (m_pairing ? BLE_LINK_ST_PAIRING : 0) |
                   (m_passkey_wait ? BLE_LINK_ST_PASSKEY : 0) | (m_recovery ? BLE_LINK_ST_RECOVERY : 0) |
                   (m_host_rejected ? BLE_LINK_ST_HOST_REJECTED : 0);
    m_tx.data[1] = m_host_leds;
    m_tx.data[2] = BLE_LINK_VERSION;
    m_tx.data[3] = (uint8_t)(QUEUE_LEN - m_q_count);
    m_tx.data[4] = m_slot;
    m_tx.data[5] = m_bonded_slots;
    m_tx.data[6] = nrf_update_state();
    uint32_t staged = nrf_update_staged();
    memcpy(&m_tx.data[7], &staged, sizeof(staged));
    uint16_t granted[3] = {m_granted.max_conn_interval, m_granted.slave_latency, m_granted.conn_sup_timeout};
    memcpy(&m_tx.data[11], granted, sizeof(granted));
    if (m_raw_waiting) {
        m_tx.data[17] = (uint8_t)(BLE_LINK_RAW_VALID | (m_raw_id << 2) | m_raw_part);
        memcpy(&m_tx.data[18], &m_raw_req[m_raw_part * BLE_LINK_RAW_PART], BLE_LINK_RAW_PART);
        m_raw_part = (uint8_t)((m_raw_part + 1U) & 3U);
    }
    m_tx.check = ble_link_check(&m_tx);
}

static void spis_arm(void) {
    APP_ERROR_CHECK(nrfx_spis_buffers_set(&m_spis, (uint8_t *)&m_tx, sizeof(m_tx), (uint8_t *)&m_rx, sizeof(m_rx)));
}

static void spis_evt(nrfx_spis_evt_t const *p_event, void *p_context) {
    (void)p_context;
    if (p_event->evt_type == NRFX_SPIS_XFER_DONE) { /* handled in the main loop */
        m_rx_amount = p_event->rx_amount;
        m_rx_pending = true;
    }
}

static void disconnect_now(void) {
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        (void)sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    }
}

/* Disconnects once queued reports, if any, have reached the host (or the wait timed out). */
static void disconnect_try(void) {
    if (m_disc_pending && (m_disc_force || (m_q_count == 0 && m_inflight == 0))) {
        m_disc_force = false;
        disconnect_now();
    }
}

/* Releases all keys on the host first: Windows keeps keys held at a disconnect (e.g. Ctrl from
 * the Fn+Ctrl+slot chord) pressed, even after the keyboard reconnects. */
static void disconnect(void) {
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID || m_disc_pending) return;
    if (!m_secured) {
        disconnect_now();
        return;
    }
    static const uint8_t none[KB_REPORT_LEN] = {0};
    m_q_count = 0; /* the host's final state is "nothing pressed" */
    (void)queue_push(REPORT_KB, none, KB_REPORT_LEN);
    (void)queue_push(REPORT_CONSUMER, none, CONSUMER_REPORT_LEN);
    m_disc_pending = true;
    m_disc_force = false;
    APP_ERROR_CHECK(app_timer_start(m_disc_timer, APP_TIMER_TICKS(DISC_RELEASE_MS), NULL));
    queue_drain();
    disconnect_try();
}

static void disc_timeout(void *p_context) {
    (void)p_context;
    m_disc_force = true; /* handled in the main loop */
}

static void slot_delete(uint8_t slot) {
    for (pm_peer_id_t id = pm_next_peer_id_get(PM_PEER_ID_INVALID); id != PM_PEER_ID_INVALID;
         id = pm_next_peer_id_get(id)) {
        if (peer_slot(id) == slot && pm_peer_delete(id) == NRF_SUCCESS) m_deletes_pending++;
    }
    m_bonded_slots &= (uint8_t)~(1U << (slot - 1));
}

/* Fn+Ctrl+Q, then Fn+Ctrl+slot: select the slot, forget its host and accept a new one for PAIR_WINDOW_MS. */
static void pair_slot(uint8_t slot) {
    m_slot = slot;
    m_pairing = true;
    m_host_rejected = false;
    (void)app_timer_stop(m_reject_timer);
    (void)app_timer_stop(m_pair_timer);
    APP_ERROR_CHECK(app_timer_start(m_pair_timer, APP_TIMER_TICKS(PAIR_WINDOW_MS), NULL));
    slot_delete(slot);
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        disconnect(); /* advertising restarts from the disconnect event */
    } else {
        advertising_restart();
    }
}

static void select_slot(uint8_t slot) {
    if (slot == m_slot) return;
    bool first = m_slot == 0;
    m_slot = slot;
    pairing_end();
    m_host_rejected = false;
    (void)app_timer_stop(m_reject_timer);
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) {
        advertising_restart();
    } else if (!(first && slot == 1)) {
        disconnect(); /* reconnects through the new slot from the disconnect event */
    }
}

/* Stop BLE for good (until reset): flash is then written through NVMC. */
static void update_begin(void) {
    if (m_updating) return;
    m_updating = true;
    app_timer_stop_all();
    m_q_count = 0;
    m_raw_waiting = false;
    m_raw_resp_ready = false;
    m_secured = false;
    m_advertising_on = false;
    m_pairing = false;
    (void)nrf_sdh_disable_request();
}

static bool update_frame(const ble_link_frame_t *f) {
    uint32_t word = 0;
    if (f->len >= 5) memcpy(&word, &f->data[1], sizeof(word));
    if (f->data[0] == BLE_LINK_UPD_START && f->len >= 21) {
        update_begin();
        nrf_update_start(word, &f->data[5]);
    } else if (!m_updating) {
        return true;
    } else if (f->data[0] == BLE_LINK_UPD_DATA && f->len >= 5) {
        return nrf_update_data(word, &f->data[5], (uint8_t)(f->len - 5));
    } else if (f->data[0] == BLE_LINK_UPD_FINISH) {
        nrf_update_finish();
    }
    return true;
}

/* Power switch off: go quiet now, enter System OFF once the host is gone and flash is idle.
 * Only a pin reset (STM32 PD2) restarts the chip. */
static void power_off_start(void) {
    if (m_power_off) return;
    m_power_off = true;
    pairing_end();
    m_passkey_wait = false;
    (void)sd_ble_gap_adv_stop(m_advertising.adv_handle);
    m_advertising_on = false;
    disconnect();
    APP_ERROR_CHECK(app_timer_start(m_off_timer, APP_TIMER_TICKS(OFF_CHECK_MS), NULL));
}

static void off_timer_tick(void *p_context) {
    (void)p_context;
    m_off_check = true;
}

/* Runs from the main loop, so the STM32 has read the command's acknowledgement by now. */
static void power_off_try(void) {
    if (m_deletes_pending || m_clearing || nrf_fstorage_is_busy(NULL)) return;
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID && ++m_off_waits < OFF_CONN_WAITS) return;
    (void)app_timer_stop(m_off_timer);
    NRF_LOG_FINAL_FLUSH();
    nrfx_spis_uninit(&m_spis); /* pin settings persist in System OFF: no floating inputs */
    const uint32_t link_pins[] = {PIN_CSN, PIN_SCK, PIN_MOSI, PIN_MISO};
    for (unsigned i = 0; i < sizeof(link_pins) / sizeof(link_pins[0]); i++) nrf_gpio_cfg_default(link_pins[i]);
    (void)sd_power_system_off();
    for (;;) (void)sd_app_evt_wait(); /* only reached in emulated System OFF under a debugger */
}

/* Returns false when the frame could not be taken and must be resent. */
static bool frame_handle(const ble_link_frame_t *f) {
    if (f->type == BLE_LINK_UPDATE && f->len >= 1) return update_frame(f);
    if (m_updating) return true; /* BLE is off */
    switch (f->type) {
    case BLE_LINK_KEYBOARD:
        if (f->len != KB_REPORT_LEN) return true;
        /* drop while not connected, or after the release reports of a disconnect */
        return !m_secured || m_disc_pending || queue_push(REPORT_KB, f->data, KB_REPORT_LEN);
    case BLE_LINK_CONSUMER:
        if (f->len != CONSUMER_REPORT_LEN) return true;
        return !m_secured || m_disc_pending || queue_push(REPORT_CONSUMER, f->data, CONSUMER_REPORT_LEN);
    case BLE_LINK_BATTERY:
        if (f->len == 1) battery_update(f->data[0]);
        return true;
    case BLE_LINK_RAW: {
        if (f->len != 1 + BLE_LINK_RAW_HALF) return true;
        if (!m_raw_waiting || (f->data[0] >> 1) != m_raw_id) return true; /* stale: the host asked again */
        if (m_raw_resp_ready) return false; /* the previous reply still waits for a notification slot */
        uint8_t half = f->data[0] & 1U;
        memcpy(&m_raw_resp[half * BLE_LINK_RAW_HALF], &f->data[1], BLE_LINK_RAW_HALF);
        m_raw_halves |= (uint8_t)(1U << half);
        if (m_raw_halves == 3U) {
            m_raw_waiting = false;
            m_raw_resp_ready = true;
            raw_drain();
        }
        return true;
    }
    case BLE_LINK_COMMAND:
        if (f->len < 1) return true;
        uint8_t slot = f->len >= 2 && f->data[1] >= 1 && f->data[1] <= BLE_LINK_SLOTS ? f->data[1] : 0;
        if (f->data[0] == BLE_LINK_CMD_PAIR) {
            pair_slot(slot ? slot : current_slot());
        } else if (f->data[0] == BLE_LINK_CMD_PAIR_CANCEL) {
            if (m_pairing) {
                pairing_end();
                if (m_conn_handle != BLE_CONN_HANDLE_INVALID && !m_secured) disconnect();
                else advertising_restart();
            }
        } else if (f->data[0] == BLE_LINK_CMD_DELETE_SLOT && slot) {
            slot_delete(slot);
            if (slot == current_slot()) disconnect();
            if (!m_deletes_pending) advertising_restart();
        } else if (f->data[0] == BLE_LINK_CMD_PASSKEY && f->len >= 7) {
            if (m_passkey_wait && m_conn_handle != BLE_CONN_HANDLE_INVALID) {
                (void)sd_ble_gap_auth_key_reply(m_conn_handle, BLE_GAP_AUTH_KEY_TYPE_PASSKEY, &f->data[1]);
            }
            m_passkey_wait = false;
        } else if (f->data[0] == BLE_LINK_CMD_SELECT_SLOT) {
            if (f->len >= 2 && f->data[1] >= 1 && f->data[1] <= BLE_LINK_SLOTS) select_slot(f->data[1]);
        } else if (f->data[0] == BLE_LINK_CMD_CLEAR_BONDS) {
            disconnect();
            (void)sd_ble_gap_adv_stop(m_advertising.adv_handle);
            m_advertising_on = false;
            m_bonded_slots = 0;
            m_clearing = true;
            APP_ERROR_CHECK(pm_peers_delete()); /* advertising restarts when this finishes */
        } else if (f->data[0] == BLE_LINK_CMD_POWER_OFF) {
            power_off_start();
        } else if (f->data[0] == BLE_LINK_CMD_DISCONNECT) {
            disconnect();
        }
        return true;
    default:
        return true;
    }
}

static void link_process(void) {
    if (!m_rx_pending) return;
    m_rx_pending = false;
    bool refused = false;
    if (m_rx_amount == sizeof(m_rx) && m_rx.magic == BLE_LINK_MAGIC_TO_NRF && m_rx.check == ble_link_check(&m_rx) && m_rx.seq != 0 &&
        m_rx.seq != m_last_seq && m_rx.len <= BLE_LINK_DATA_MAX) {
        if (frame_handle(&m_rx)) {
            m_last_seq = m_rx.seq;
        } else {
            refused = true;
        }
    }
    if (!m_updating) {
        queue_drain();
        raw_drain();
    }
    status_prepare(refused);
    spis_arm();
}

static void link_init(void) {
    nrfx_spis_config_t cfg = NRFX_SPIS_DEFAULT_CONFIG;
    cfg.csn_pin = PIN_CSN;
    cfg.sck_pin = PIN_SCK;
    cfg.mosi_pin = PIN_MOSI;
    cfg.miso_pin = PIN_MISO;
    cfg.mode = NRF_SPIS_MODE_0;
    cfg.bit_order = NRF_SPIS_BIT_ORDER_MSB_FIRST;
    cfg.def = 0xFF; /* a not-ready nRF52 answers 0xFF, which fails the magic check */
    cfg.orc = 0xFF;
    cfg.csn_pullup = NRF_GPIO_PIN_PULLUP;
    APP_ERROR_CHECK(nrfx_spis_init(&m_spis, &cfg, spis_evt, NULL));
    status_prepare(false);
    spis_arm();
}

/* ---------- BLE events ---------- */

static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
    (void)p_context;
    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        m_granted = p_ble_evt->evt.gap_evt.params.connected.conn_params;
        m_advertising_on = false;
        APP_ERROR_CHECK(nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle));
        APP_ERROR_CHECK(app_timer_start(m_sec_timer, APP_TIMER_TICKS(SECURE_TIMEOUT_MS), NULL));
        break;
    case BLE_GAP_EVT_CONN_PARAM_UPDATE:
        m_granted = p_ble_evt->evt.gap_evt.params.conn_param_update.conn_params;
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        memset(&m_granted, 0, sizeof(m_granted));
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        m_secured = false;
        m_passkey_wait = false;
        m_host_leds = 0;
        m_q_count = 0;
        m_inflight = 0;
        raw_reset();
        m_disc_pending = false;
        (void)app_timer_stop(m_disc_timer);
        (void)app_timer_stop(m_sec_timer);
        if (m_host_rejected) {
            (void)app_timer_stop(m_reject_timer);
            APP_ERROR_CHECK(app_timer_start(m_reject_timer, APP_TIMER_TICKS(REJECT_HOLD_MS), NULL));
        } else {
            advertising_restart();
        }
        break;
    case BLE_GAP_EVT_AUTH_KEY_REQUEST: /* the host shows a passkey: the STM32 collects the typed digits */
        if (p_ble_evt->evt.gap_evt.params.auth_key_request.key_type == BLE_GAP_AUTH_KEY_TYPE_PASSKEY) {
            m_passkey_wait = true;
        }
        break;
    case BLE_GAP_EVT_AUTH_STATUS:
        m_passkey_wait = false;
        break;
    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
        ble_gap_phys_t const phys = {.rx_phys = BLE_GAP_PHY_AUTO, .tx_phys = BLE_GAP_PHY_AUTO};
        APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
        break;
    }
    case BLE_GATTS_EVT_HVN_TX_COMPLETE: {
        uint8_t done = p_ble_evt->evt.gatts_evt.params.hvn_tx_complete.count;
        m_inflight = done < m_inflight ? (uint8_t)(m_inflight - done) : 0;
        queue_drain();
        raw_drain();
        disconnect_try();
        break;
    }
    case BLE_GATTC_EVT_TIMEOUT:
        APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                              BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
        break;
    case BLE_GATTS_EVT_TIMEOUT:
        APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                              BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
        break;
    default:
        break;
    }
}

static void on_hids_evt(ble_hids_t *p_hids, ble_hids_evt_t *p_evt) {
    if (p_evt->evt_type != BLE_HIDS_EVT_REP_CHAR_WRITE ||
        p_evt->params.char_write.char_id.rep_type != BLE_HIDS_REP_TYPE_OUTPUT) {
        return;
    }
    uint8_t leds;
    if (ble_hids_outp_rep_get(p_hids, 0, OUTPUT_REPORT_LEN, 0, m_conn_handle, &leds) == NRF_SUCCESS) {
        m_host_leds = leds;
    }
}

/* A VIA request from the host: shown to the STM32 in the status until it replies. A newer
 * request replaces a waiting one (a host that timed out and asked again). */
static void on_raw_evt(ble_hids_t *p_hids, ble_hids_evt_t *p_evt) {
    (void)p_hids;
    if (p_evt->evt_type != BLE_HIDS_EVT_REP_CHAR_WRITE ||
        p_evt->params.char_write.char_id.rep_type != BLE_HIDS_REP_TYPE_OUTPUT || m_updating ||
        p_evt->params.char_write.offset != 0 || p_evt->params.char_write.len == 0) {
        return;
    }
    uint16_t len = p_evt->params.char_write.len;
    memset(m_raw_req, 0, sizeof(m_raw_req));
    memcpy(m_raw_req, p_evt->params.char_write.data, len < RAW_REPORT_LEN ? len : RAW_REPORT_LEN);
    m_raw_id = (uint8_t)(m_raw_id % 31U + 1U);
    m_raw_halves = 0;
    m_raw_part = 0;
    m_raw_waiting = true;
    latency_set(true);
    (void)app_timer_stop(m_raw_timer);
    APP_ERROR_CHECK(app_timer_start(m_raw_timer, APP_TIMER_TICKS(RAW_SESSION_MS), NULL));
}

/* ---------- init ---------- */

static void ble_stack_init(void) {
    APP_ERROR_CHECK(nrf_sdh_enable_request());
    /* DC/DC instead of the LDO, as PFU's app does (so the module's inductor is fitted). */
    APP_ERROR_CHECK(sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE));
    uint32_t ram_start = 0;
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static void gap_init(void) {
    ble_gap_conn_sec_mode_t sec_mode;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    APP_ERROR_CHECK(sd_ble_gap_appearance_set(BLE_APPEARANCE_HID_KEYBOARD));
    ble_gap_conn_params_t params = {.min_conn_interval = MIN_CONN_INTERVAL, .max_conn_interval = MAX_CONN_INTERVAL,
                                    .slave_latency = SLAVE_LATENCY, .conn_sup_timeout = CONN_SUP_TIMEOUT};
    APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&params));
    APP_ERROR_CHECK(nrf_ble_gatt_init(&m_gatt, NULL));
}

#ifdef ECK_JIS
#define HID_PRODUCT_ID 0x4012 /* the JIS keyboard's USB IDs and country code (Japan), as PFU's JIS image */
#else
#define HID_PRODUCT_ID 0x4010
#endif

static void dis_init(void) {
    ble_dis_init_t init;
    ble_dis_pnp_id_t pnp = {.vendor_id_source = 0x02, .vendor_id = 0xCAFE, .product_id = HID_PRODUCT_ID,
                            .product_version = 0x0001}; /* same IDs as ECK's USB device */
    memset(&init, 0, sizeof(init));
    ble_srv_ascii_to_utf8(&init.manufact_name_str, MANUFACTURER_NAME);
    init.p_pnp_id = &pnp;
    init.dis_char_rd_sec = SEC_JUST_WORKS;
    APP_ERROR_CHECK(ble_dis_init(&init));
}

static void bas_init(void) {
    ble_bas_init_t init;
    memset(&init, 0, sizeof(init));
    init.support_notification = true;
    init.initial_batt_level = 100;
    init.bl_rd_sec = SEC_JUST_WORKS;
    init.bl_cccd_wr_sec = SEC_JUST_WORKS;
    init.bl_report_rd_sec = SEC_JUST_WORKS;
    APP_ERROR_CHECK(ble_bas_init(&m_bas, &init));
}

static void hids_init(void) {
    static const uint8_t report_map[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,       /* Keyboard collection */
        0x85, REPORT_ID_KB,                       /* Report ID */
        0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,       /* modifiers */
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
        0x95, 0x01, 0x75, 0x08, 0x81, 0x01,       /* reserved */
        0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, /* LEDs */
        0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
        /* keys: logical max 255 needs the two-byte form (0x25 0xFF would be -1), as in ECK's USB map */
        0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00,
        0xC0,
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,       /* Consumer control collection */
        0x85, REPORT_ID_CONSUMER,
        0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
        0xC0,
    };
    static ble_hids_inp_rep_init_t inputs[2];
    static ble_hids_outp_rep_init_t outputs[1];
    memset(inputs, 0, sizeof(inputs));
    memset(outputs, 0, sizeof(outputs));

    inputs[REPORT_KB].max_len = KB_REPORT_LEN;
    inputs[REPORT_KB].rep_ref.report_id = REPORT_ID_KB;
    inputs[REPORT_CONSUMER].max_len = CONSUMER_REPORT_LEN;
    inputs[REPORT_CONSUMER].rep_ref.report_id = REPORT_ID_CONSUMER;
    for (int i = 0; i < 2; i++) {
        inputs[i].rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;
        inputs[i].sec.cccd_wr = SEC_JUST_WORKS;
        inputs[i].sec.rd = SEC_JUST_WORKS;
        inputs[i].sec.wr = SEC_JUST_WORKS;
    }
    outputs[0].max_len = OUTPUT_REPORT_LEN;
    outputs[0].rep_ref.report_id = REPORT_ID_KB;
    outputs[0].rep_ref.report_type = BLE_HIDS_REP_TYPE_OUTPUT;
    outputs[0].sec.rd = SEC_JUST_WORKS;
    outputs[0].sec.wr = SEC_JUST_WORKS;

    ble_hids_init_t init;
    memset(&init, 0, sizeof(init));
    init.evt_handler = on_hids_evt;
    init.error_handler = error_handler;
    init.is_kb = true;
    init.inp_rep_count = 2;
    init.p_inp_rep_array = inputs;
    init.outp_rep_count = 1;
    init.p_outp_rep_array = outputs;
    init.rep_map.data_len = sizeof(report_map);
    init.rep_map.p_data = (uint8_t *)report_map;
    init.hid_information.bcd_hid = 0x0111;
#ifdef ECK_JIS
    init.hid_information.b_country_code = 15;
#endif
    init.hid_information.flags = HID_INFO_FLAG_REMOTE_WAKE_MSK | HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;
    /* Encrypted access (SEC_JUST_WORKS: no MITM needed per attribute); unset fields are SEC_NO_ACCESS. */
    init.rep_map.rd_sec = SEC_JUST_WORKS;
    init.hid_information.rd_sec = SEC_JUST_WORKS;
    init.boot_kb_inp_rep_sec.cccd_wr = SEC_JUST_WORKS;
    init.boot_kb_inp_rep_sec.rd = SEC_JUST_WORKS;
    init.boot_kb_outp_rep_sec.rd = SEC_JUST_WORKS;
    init.boot_kb_outp_rep_sec.wr = SEC_JUST_WORKS;
    init.protocol_mode_rd_sec = SEC_JUST_WORKS;
    init.protocol_mode_wr_sec = SEC_JUST_WORKS;
    init.ctrl_point_wr_sec = SEC_JUST_WORKS;
    APP_ERROR_CHECK(ble_hids_init(&m_hids, &init));
}

static void raw_hids_init(void) {
    static const uint8_t report_map[] = {
        0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01, /* Vendor 0xFF60, usage 0x61: as ECK's USB VIA interface */
        0x09, 0x62, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, RAW_REPORT_LEN, 0x81, 0x02,
        0x09, 0x63, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, RAW_REPORT_LEN, 0x91, 0x02,
        0xC0,
    };
    static ble_hids_inp_rep_init_t input;
    static ble_hids_outp_rep_init_t output;
    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.max_len = RAW_REPORT_LEN;
    input.rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;
    input.sec.cccd_wr = SEC_JUST_WORKS;
    input.sec.rd = SEC_JUST_WORKS;
    input.sec.wr = SEC_JUST_WORKS;
    output.max_len = RAW_REPORT_LEN;
    output.rep_ref.report_type = BLE_HIDS_REP_TYPE_OUTPUT;
    output.sec.rd = SEC_JUST_WORKS;
    output.sec.wr = SEC_JUST_WORKS;

    ble_hids_init_t init;
    memset(&init, 0, sizeof(init));
    init.evt_handler = on_raw_evt;
    init.error_handler = error_handler;
    init.inp_rep_count = 1;
    init.p_inp_rep_array = &input;
    init.outp_rep_count = 1;
    init.p_outp_rep_array = &output;
    init.rep_map.data_len = sizeof(report_map);
    init.rep_map.p_data = (uint8_t *)report_map;
    init.rep_map.rd_sec = SEC_JUST_WORKS;
    init.hid_information.bcd_hid = 0x0111;
    init.hid_information.flags = HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;
    init.hid_information.rd_sec = SEC_JUST_WORKS;
    init.ctrl_point_wr_sec = SEC_JUST_WORKS;
    APP_ERROR_CHECK(ble_hids_init(&m_raw, &init));
}

static void services_init(void) {
    nrf_ble_qwr_init_t qwr = {.error_handler = error_handler};
    APP_ERROR_CHECK(nrf_ble_qwr_init(&m_qwr, &qwr));
    dis_init();
    bas_init();
    hids_init();
    raw_hids_init();
}

/* Re-run before each start: the advertising module never restores the discoverable flag itself. */
static void advertising_init(bool discoverable) {
    static ble_uuid_t uuids[] = {{BLE_UUID_HUMAN_INTERFACE_DEVICE_SERVICE, BLE_UUID_TYPE_BLE}};
    /* Windows Swift Pair (Microsoft beacon: vendor 0x0006, ID 3, scenario 0, RSSI byte 0x80), only
     * while pairing; it fills the 31-byte advert, so the name moves to the scan response. */
    static uint8_t swift_pair[] = {0x03, 0x00, 0x80};
    static ble_advdata_manuf_data_t ms = {.company_identifier = 0x0006,
                                          .data = {.size = sizeof(swift_pair), .p_data = swift_pair}};
    ble_advertising_init_t init;
    memset(&init, 0, sizeof(init));
    if (discoverable) {
        init.advdata.p_manuf_specific_data = &ms;
        init.srdata.name_type = BLE_ADVDATA_FULL_NAME;
    } else {
        init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
    }
    init.advdata.include_appearance = true;
    init.advdata.flags = discoverable ? BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE : BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;
    init.advdata.uuids_complete.uuid_cnt = 1;
    init.advdata.uuids_complete.p_uuids = uuids;
    init.config.ble_adv_whitelist_enabled = true;
    init.config.ble_adv_fast_enabled = true;
    init.config.ble_adv_fast_interval = ADV_FAST_INTERVAL;
    init.config.ble_adv_fast_timeout = ADV_FAST_DURATION;
    init.config.ble_adv_slow_enabled = true;
    init.config.ble_adv_slow_interval = ADV_SLOW_INTERVAL;
    init.config.ble_adv_slow_timeout = ADV_SLOW_DURATION;
    init.config.ble_adv_on_disconnect_disabled = true; /* advertising_restart() runs instead */
    init.evt_handler = on_adv_evt;
    init.error_handler = error_handler;
    APP_ERROR_CHECK(ble_advertising_init(&m_advertising, &init));
    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

static void conn_params_init(void) {
    ble_conn_params_init_t init;
    memset(&init, 0, sizeof(init));
    init.first_conn_params_update_delay = APP_TIMER_TICKS(5000);
    init.next_conn_params_update_delay = APP_TIMER_TICKS(30000);
    init.max_conn_params_update_count = 3;
    init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    init.error_handler = error_handler;
    APP_ERROR_CHECK(ble_conn_params_init(&init));
}

static void peer_manager_init(void) {
    APP_ERROR_CHECK(pm_init());
    ble_gap_sec_params_t sec = {0};
    sec.bond = 1;
    sec.mitm = 1;
    sec.lesc = 1;
    sec.io_caps = BLE_GAP_IO_CAPS_KEYBOARD_ONLY; /* the host shows a passkey, typed on the keyboard */
    sec.min_key_size = 16;
    sec.max_key_size = 16;
    sec.kdist_own.enc = 1;
    sec.kdist_own.id = 1;
    sec.kdist_peer.enc = 1;
    sec.kdist_peer.id = 1;
    APP_ERROR_CHECK(pm_sec_params_set(&sec));
    APP_ERROR_CHECK(pm_register(pm_evt_handler));
}

/* Sampled before anything else starts, so a Bluetooth bug cannot block it. The pull-down holds the
 * line low while the STM32 leaves PB6 floating; the pin is released after sampling. */
static bool recovery_requested(void) {
    nrf_gpio_cfg_input(PIN_RECOVERY, NRF_GPIO_PIN_PULLDOWN);
    nrf_delay_us(100);
    bool high = true;
    for (unsigned i = 0; i < 8 && high; i++) {
        high = nrf_gpio_pin_read(PIN_RECOVERY) != 0;
        nrf_delay_us(50);
    }
    nrf_gpio_cfg_default(PIN_RECOVERY);
    return high;
}

/* Only the link and the update receiver run; frame_handle drops everything but update frames.
 * A crash resets into this mode again while the STM32 still holds PB6 high. */
static void recovery_main(void) {
    m_recovery = true;
    m_updating = true; /* SoftDevice never started: update_begin has nothing to stop */
    NVIC->ICER[0] = 0xFFFFFFFFUL; /* only our SPIS interrupt, whatever the bootloader left enabled */
    NVIC->ICER[1] = 0xFFFFFFFFUL;
    link_init();
    NRF_LOG_INFO("recovery mode, link v%d", BLE_LINK_VERSION);
    for (;;) {
        link_process();
        nrf_update_poll();
        (void)NRF_LOG_PROCESS();
    }
}

/* A host that disconnects during the DH computation is not an error worth a restart. */
static void lesc_service(void) {
    ret_code_t err = nrf_ble_lesc_request_handler();
    if (err != BLE_ERROR_INVALID_CONN_HANDLE && err != NRF_ERROR_INVALID_STATE) APP_ERROR_CHECK(err);
}

int main(void) {
    APP_SCHED_INIT(APP_TIMER_SCHED_EVENT_DATA_SIZE, 20); /* SoftDevice events run in the main loop */
    APP_ERROR_CHECK(NRF_LOG_INIT(NULL));
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    if (recovery_requested()) recovery_main();
    APP_ERROR_CHECK(app_timer_init());
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());
    ble_stack_init();
    gap_init();
    advertising_init(false);
    services_init();
    conn_params_init();
    peer_manager_init();
    APP_ERROR_CHECK(app_timer_create(&m_pair_timer, APP_TIMER_MODE_SINGLE_SHOT, pair_timeout));
    APP_ERROR_CHECK(app_timer_create(&m_sec_timer, APP_TIMER_MODE_SINGLE_SHOT, security_timeout));
    APP_ERROR_CHECK(app_timer_create(&m_off_timer, APP_TIMER_MODE_REPEATED, off_timer_tick));
    APP_ERROR_CHECK(app_timer_create(&m_disc_timer, APP_TIMER_MODE_SINGLE_SHOT, disc_timeout));
    APP_ERROR_CHECK(app_timer_create(&m_reject_timer, APP_TIMER_MODE_SINGLE_SHOT, reject_hold_end));
    APP_ERROR_CHECK(app_timer_create(&m_raw_timer, APP_TIMER_MODE_SINGLE_SHOT, raw_session_end));
    bonded_slots_refresh(); /* the first status reply, prepared in link_init, reports the bonds */
    link_init();
    NRF_LOG_INFO("ECK BLE bridge started, link v%d", BLE_LINK_VERSION);
    foreign_peers_delete();
    advertising_restart(); /* slot 1 until the STM32 selects one; waits for the deletes above */

    for (;;) {
        app_sched_execute();
        lesc_service(); /* DH key for LESC pairing */
        link_process();
        nrf_update_poll(); /* one page erase (~85 ms) per pass while staging */
        if (m_off_check) {
            m_off_check = false;
            power_off_try();
        }
        if (m_disc_force) disconnect_try();
        uint8_t upd = nrf_update_state();
        if (!NRF_LOG_PROCESS() && upd != BLE_LINK_UPD_ST_ERASING && upd != BLE_LINK_UPD_ST_VERIFYING) nrf_pwr_mgmt_run();
    }
}
