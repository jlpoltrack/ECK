/* HHKB Hybrid: send key reports to the nRF52 Bluetooth bridge (nrf52/) over SPI1 while no USB
 * host is connected or Bluetooth is selected (Fn+Ctrl+1..4). Protocol: ble_link.h. */
#ifdef HHKB_HYBRID
#include "hybrid_ble.h"
#include "hybrid.h"
#include "hybrid_power.h"
#include "hybrid_chords.h"
#include "hhkb_positions.h"

#include <string.h>
#include "matrix_scan.h" /* hhkb_get_debug_flag */
#include "usb_descriptors.h"

#include "FreeRTOS.h"
#include "app_usb_hid.h"
#include "ble_link.h"
#include "events.h"
#include "keys.h"
#include "layers.h"
#include "queue.h"
#include "eeprom.h"
#include "stm32l0xx.h"
#include "task.h"
#include "task_internal.h"
#include "tusb.h"

#define CS_PIN 15U          /* PA15 */
#define POLL_MS 100U        /* status refresh while active */
#define POLL_IDLE_MS 1000U  /* ...and after IDLE_AFTER_MS without reports or link events */
#define IDLE_AFTER_MS 10000U
#define ACK_POLLS 20U       /* status polls per send attempt */
#define SEND_ATTEMPTS 10U

typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t data[8];
} link_msg_t;

static StaticQueue_t s_queue_ctl;
static uint8_t s_queue_buf[10 * sizeof(link_msg_t)]; /* the nRF52 queues 16 more */
static QueueHandle_t s_queue;
/* Newest keyboard/consumer report that found the queue full; sent once it drains. Reports
 * carry the whole state, so only the newest matters, and a failed send would look like a link
 * loss to the USB layer (it switches transport and drops the report). */
static link_msg_t s_held[2];
static volatile bool s_held_set[2];
static StaticTask_t s_task_tcb;
static StackType_t s_task_stack[128]; /* static worst case ~400 B (tools/stack_depth.py) */

static volatile uint8_t s_flags;
static uint8_t s_host_leds;
static uint8_t s_seq;
static bool s_ready; /* nRF52 is answering */
static uint8_t s_bad_replies; /* consecutive invalid replies */
/* Link health since boot, shown with the Polling Rate Monitor: frames sent, invalid replies,
 * frames given up on (their report never reached the nRF52). */
static uint32_t s_stat_frames, s_stat_bad, s_stat_lost;
static TickType_t s_last_good;   /* last valid nRF52 reply */
static TickType_t s_last_active; /* last report or status change */
#define LINK_DOWN_REPLIES 20U
static uint8_t s_nrf_slot; /* host slot the nRF52 reports */
static uint8_t s_bonded = 0xFF; /* bit n-1: slot n has a host (link v3+); 0xFF until known */
static uint8_t s_upd_state;     /* BLE_LINK_UPD_ST_* the nRF52 reports */
static uint32_t s_upd_staged;
static uint8_t s_upd_buf[BLE_LINK_DATA_MAX]; /* one update frame waiting for the link task */
static uint8_t s_upd_len;
static volatile bool s_upd_pending;
static volatile bool s_upd_failed; /* the last update frame was not acknowledged */
static TickType_t s_quiet_until;   /* link watchdog paused while PFU's bootloader installs */
static bool s_release_all;         /* send empty reports once the link is up */
static bool s_installing;          /* until the restarted nRF52 is back (or the quiet time ends) */
static int16_t s_battery = -1;     /* last percent sent (-1: none), resent after a link restart */
static volatile bool s_battery_dirty;
static volatile bool s_power_off_done; /* nRF52 told to power off: the link stays idle */
static bool s_recovery;                /* PB6 held high: the nRF52 was restarted into recovery mode */

/* Host slot (1..4), whether Bluetooth wins over a connected USB host (bit 8) and the Bluetooth
 * scan rate + 1 (bits 16-18, 0 = default), kept in one EEPROM word past ECK's settings
 * (all zero in the stock Hybrid EEPROM). */
#define SETTINGS_ADDR EEPROM_BLE_SETTINGS_ADDR
#define SETTINGS_MAGIC 0xB7000000UL
#define BLE_SCAN_RATE_DEFAULT 2U /* 250 Hz: faster than the 15-30 ms connection interval, saves power */
static uint8_t s_slot = 1;
static bool s_prefer_ble;
static uint8_t s_ble_scan_rate = BLE_SCAN_RATE_DEFAULT;

static void settings_load(void) {
    uint32_t w = *(volatile const uint32_t *)SETTINGS_ADDR;
    if ((w & 0xFF000000UL) != SETTINGS_MAGIC) return;
    uint8_t slot = (uint8_t)w;
    if (slot >= 1 && slot <= BLE_LINK_SLOTS) s_slot = slot;
    s_prefer_ble = (w >> 8) & 1U;
    uint8_t rate = (w >> 16) & 7U;
    if (rate >= 1 && rate <= 5) s_ble_scan_rate = rate - 1U;
}

static void settings_save(void) {
    uint32_t w = SETTINGS_MAGIC | ((uint32_t)(s_ble_scan_rate + 1U) << 16) | (s_prefer_ble ? 0x100U : 0) | s_slot;
    eeprom_write_word((volatile uint32_t *)SETTINGS_ADDR, w);
}

static void spi_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    GPIOA->BSRR = 1U << CS_PIN;
    GPIOA->MODER = (GPIOA->MODER & ~(3U << (2 * CS_PIN))) | (1U << (2 * CS_PIN));
    /* PB3 SCK, PB4 MISO, PB5 MOSI: AF0, medium speed */
    GPIOB->AFR[0] &= ~(0xFFFU << 12);
    GPIOB->OSPEEDR = (GPIOB->OSPEEDR & ~(0x3FU << 6)) | (0x15U << 6);
    GPIOB->MODER = (GPIOB->MODER & ~(0x3FU << 6)) | (0x2AU << 6);
    /* PB6 (nRF52 P0.18) low: high while the nRF52 starts selects its recovery mode */
    GPIOB->BRR = 1U << 6;
    GPIOB->MODER = (GPIOB->MODER & ~(3U << (2 * 6))) | (1U << (2 * 6));
    /* Master, mode 0, MSB first, software NSS, PCLK2/16: 2 MHz (1 MHz on batteries) */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (3U << SPI_CR1_BR_Pos);
    SPI1->CR1 |= SPI_CR1_SPE;
}

static uint8_t spi_byte(uint8_t out) {
    while (!(SPI1->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t *)&SPI1->DR = out;
    while (!(SPI1->SR & SPI_SR_RXNE)) {}
    return (uint8_t)SPI1->DR;
}

/* One 32-byte exchange. Returns true and updates the cached status if the reply is valid. */
static bool transact(const ble_link_frame_t *tx, ble_link_frame_t *rx) {
    const uint8_t *out = (const uint8_t *)tx;
    uint8_t *in = (uint8_t *)rx;
    GPIOA->BRR = 1U << CS_PIN;
    hybrid_delay_us(60); /* CSN setup for the nRF52 SPIS; was a ~45-50 us counted loop */
    for (unsigned i = 0; i < BLE_LINK_FRAME_LEN; i++) in[i] = spi_byte(out[i]);
    while (SPI1->SR & SPI_SR_BSY) {}
    GPIOA->BSRR = 1U << CS_PIN;
    if (rx->magic != BLE_LINK_MAGIC_TO_STM || rx->type != BLE_LINK_STATUS || rx->check != ble_link_check(rx)) {
        /* nRF52 busy (answers 0xFF) or absent: after a run of these, stop routing keys to it. */
        s_stat_bad++;
        if (s_bad_replies < LINK_DOWN_REPLIES && ++s_bad_replies == LINK_DOWN_REPLIES) {
            s_ready = false;
            s_flags = 0;
        }
        return false;
    }
    s_bad_replies = 0;
    s_last_good = xTaskGetTickCount();
    static bool boot_status = true;
    if (boot_status) {
        boot_status = false;
        /* Left in update mode by an aborted update (SoftDevice off): only a reset brings it back. */
        if (rx->data[6] != BLE_LINK_UPD_ST_IDLE) {
            hybrid_nrf_reset();
            return false;
        }
    }
    bool first = !s_ready; /* the nRF52 prepared this reply before it started: bonds may be stale */
    if (first) {
        s_installing = false; /* back after the reset, not a reply still in flight before it */
        s_seq = rx->seq; /* continue after the nRF52's last accepted frame */
        if (s_battery >= 0) s_battery_dirty = true;
        s_release_all = true;
    }
    s_ready = true;
    if (rx->data[0] != s_flags) s_last_active = xTaskGetTickCount();
    s_flags = rx->data[0];
    s_nrf_slot = rx->data[4];
    if (rx->data[2] >= 3 && !first && !(s_flags & BLE_LINK_ST_RECOVERY)) s_bonded = rx->data[5];
    if (rx->len >= 11) {
        s_upd_state = rx->data[6];
        memcpy(&s_upd_staged, &rx->data[7], sizeof(s_upd_staged));
    }
    if (rx->data[1] != s_host_leds) {
        s_host_leds = rx->data[1];
        if ((s_flags & BLE_LINK_ST_CONNECTED) && (s_prefer_ble || !tud_mounted())) events_hid_leds(s_host_leds);
    }
    return true;
}

static void frame_fill(ble_link_frame_t *f, uint8_t seq, uint8_t type, const uint8_t *data, uint8_t len) {
    memset(f, 0, sizeof(*f));
    f->magic = BLE_LINK_MAGIC_TO_NRF;
    f->seq = seq;
    f->type = type;
    f->len = len;
    if (len) memcpy(f->data, data, len);
    f->check = ble_link_check(f);
}

static void poll(ble_link_frame_t *rx) {
    ble_link_frame_t nop;
    frame_fill(&nop, 0, BLE_LINK_NOP, NULL, 0);
    transact(&nop, rx);
}

/* Send one frame and wait until the nRF52 reports it accepted. A refused frame (queue full)
 * is resent without using up an attempt, for at most QUEUE_WAIT_MS. */
#define QUEUE_WAIT_MS 500U
/* Update frames come back to back: wait ~0.3 ms for the nRF52 to re-arm, not a whole tick. */
static void link_gap(bool fast) {
    if (fast) {
        hybrid_delay_us(750); /* was a ~0.6-0.7 ms counted loop */
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static bool send_frame(uint8_t type, const uint8_t *data, uint8_t len) {
    bool fast = type == BLE_LINK_UPDATE;
    ble_link_frame_t f, rx;
    s_seq = (uint8_t)(s_seq + 1U) ? (uint8_t)(s_seq + 1U) : 1U;
    frame_fill(&f, s_seq, type, data, len);
    s_stat_frames++;
    TickType_t start = xTaskGetTickCount();
    unsigned attempt = 0;
    while (attempt < SEND_ATTEMPTS && s_ready) {
        /* The nRF52 drops a frame it was not armed for (reply 0xFF): resend on the next tick. */
        if (!transact(&f, &rx)) {
            attempt++;
            link_gap(fast);
            continue;
        }
        bool refused = false;
        for (unsigned i = 0; i < (fast ? ACK_POLLS * 4U : ACK_POLLS); i++) {
            link_gap(fast);
            ble_link_frame_t nop;
            frame_fill(&nop, 0, BLE_LINK_NOP, NULL, 0);
            if (!transact(&nop, &rx)) continue;
            if (rx.seq == s_seq) return true;
            if (rx.data[0] & BLE_LINK_ST_QUEUE_FULL) {
                refused = true;
                break;
            }
        }
        if (!refused || xTaskGetTickCount() - start >= pdMS_TO_TICKS(QUEUE_WAIT_MS)) attempt++;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    s_stat_lost++;
    return false;
}

static void send(const link_msg_t *m) {
    (void)send_frame(m->type, m->data, m->len);
}

#define FIRST_USE_MS 3000U
#define LINK_RESET_MS 5000U    /* no valid reply for this long: restart the nRF52 */
#define LINK_RESET_GAP_MS 30000U

/* Restart a silent nRF52 through PD2, or one in recovery mode nobody asked for (PFU's Bank 1 raises
 * PB6 too), at most once per LINK_RESET_GAP_MS. */
#define STRAY_RECOVERY_RESETS 3U /* then leave it: P0.18 may not follow PB6 */
static void link_watchdog(void) {
    static TickType_t last_reset;
    static uint8_t stray_resets;
    TickType_t now = xTaskGetTickCount();
    bool stray_recovery = s_ready && (s_flags & BLE_LINK_ST_RECOVERY) && !s_recovery && !s_upd_pending &&
                          s_upd_state == BLE_LINK_UPD_ST_IDLE && stray_resets < STRAY_RECOVERY_RESETS;
    if (!stray_recovery && (TickType_t)(now - s_last_good) < pdMS_TO_TICKS(LINK_RESET_MS)) return;
    if ((int32_t)(s_quiet_until - now) > 0) return;
    if (last_reset && (TickType_t)(now - last_reset) < pdMS_TO_TICKS(LINK_RESET_GAP_MS)) return;
    last_reset = now;
    if (stray_recovery) stray_resets++;
    if (s_recovery) { /* a silent recovery mode: fall back to normal mode */
        s_recovery = false;
        GPIOB->BRR = 1U << 6;
    }
    s_flags = 0;
    hybrid_nrf_reset();
}

/* An nRF52 that a power-off left in System OFF answers nothing: wake it with a pin reset. */
#define BOOT_ANSWER_MS 500U
#define OFF_READY_MS 2000U
static void wait_ready(uint32_t ms) {
    TickType_t start = xTaskGetTickCount();
    while (!s_ready && (TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(ms)) {
        ble_link_frame_t rx;
        poll(&rx);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void link_boot(void) {
    wait_ready(BOOT_ANSWER_MS);
    if (!s_ready) hybrid_nrf_reset();
    s_bad_replies = 0;
    s_last_good = xTaskGetTickCount();
}

#ifdef HHKB_HYBRID_TEST_CMD
/* Bench automation over SWD (tools/bench/hybrid_cmd.py): write {BLE_LINK_CMD_*, arg, nonzero}. */
volatile uint8_t g_hybrid_test_cmd[3];
#endif

static void link_task(void *arg) {
    (void)arg;
    spi_init();
    bool first_use_checked = false;
    link_boot();
    TickType_t stats_at = xTaskGetTickCount();
    for (;;) {
        link_watchdog();
        if (xTaskGetTickCount() - stats_at >= pdMS_TO_TICKS(1000)) {
            stats_at = xTaskGetTickCount();
            if (hhkb_get_debug_flag(DEBUG_FLAG_RATE))
                debug_console_printf("[LINK] frames=%lu bad=%lu lost=%lu ready=%u\r\n", (unsigned long)s_stat_frames,
                                     (unsigned long)s_stat_bad, (unsigned long)s_stat_lost, (unsigned)s_ready);
        }
        /* Stock first use: no host paired and no USB host, so pair slot 1 right away. */
        bool recovery = (s_flags & BLE_LINK_ST_RECOVERY) != 0; /* takes update frames only */
        if (!first_use_checked && s_ready && !recovery && xTaskGetTickCount() > pdMS_TO_TICKS(FIRST_USE_MS)) {
            first_use_checked = true;
            if (s_bonded == 0 && !tud_mounted()) {
                uint8_t slot = 1;
                s_slot = slot;
                link_msg_t pair = {.type = BLE_LINK_COMMAND, .len = 2, .data = {BLE_LINK_CMD_PAIR, slot}};
                send(&pair);
            }
        }
        if (s_ready && !recovery && s_nrf_slot != s_slot) { /* after our boot or an nRF52 restart */
            link_msg_t sel = {.type = BLE_LINK_COMMAND, .len = 2, .data = {BLE_LINK_CMD_SELECT_SLOT, s_slot}};
            send(&sel);
        }
#ifdef HHKB_HYBRID_TEST_CMD
        if (g_hybrid_test_cmd[2]) {
            link_msg_t t = {.type = BLE_LINK_COMMAND, .len = 2, .data = {g_hybrid_test_cmd[0], g_hybrid_test_cmd[1]}};
            g_hybrid_test_cmd[2] = 0;
            if ((t.data[0] == BLE_LINK_CMD_PAIR || t.data[0] == BLE_LINK_CMD_SELECT_SLOT) && t.data[1] >= 1 &&
                t.data[1] <= BLE_LINK_SLOTS) {
                s_slot = t.data[1]; /* as the key path does, or the slot check below undoes it */
            }
            send(&t);
        }
#endif
        if (s_upd_pending) {
            s_upd_failed = !send_frame(BLE_LINK_UPDATE, s_upd_buf, s_upd_len);
            s_upd_pending = false;
            continue;
        }
        if (s_ready && s_release_all) {
            /* Keys held across an STM32 reset would stay down on the host. */
            s_release_all = false;
            link_msg_t k = {.type = BLE_LINK_KEYBOARD, .len = 8};
            link_msg_t c = {.type = BLE_LINK_CONSUMER, .len = 2};
            send(&k);
            vTaskDelay(pdMS_TO_TICKS(1));
            send(&c);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (s_ready && s_battery_dirty) {
            s_battery_dirty = false;
            link_msg_t b = {.type = BLE_LINK_BATTERY, .len = 1, .data = {(uint8_t)s_battery}};
            send(&b);
        }
        link_msg_t m;
        if ((s_held_set[0] || s_held_set[1]) && uxQueueMessagesWaiting(s_queue) == 0) {
            for (int i = 0; i < 2; i++) {
                taskENTER_CRITICAL();
                bool set = s_held_set[i];
                m = s_held[i];
                s_held_set[i] = false;
                taskEXIT_CRITICAL();
                if (set) {
                    send(&m);
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
            continue;
        }
        /* Poll slowly when idle: both chips then sleep between polls. */
        bool busy = (TickType_t)(xTaskGetTickCount() - s_last_active) < pdMS_TO_TICKS(IDLE_AFTER_MS) ||
                    (s_flags & (BLE_LINK_ST_PAIRING | BLE_LINK_ST_PASSKEY)) || !s_ready || s_upd_state;
        if (xQueueReceive(s_queue, &m, pdMS_TO_TICKS(busy ? POLL_MS : POLL_IDLE_MS)) == pdTRUE) {
            if (m.type == BLE_LINK_NOP) continue; /* only a wake-up (update frame waiting) */
            bool off = m.type == BLE_LINK_COMMAND && m.data[0] == BLE_LINK_CMD_POWER_OFF;
            if (off && (s_recovery || (s_flags & BLE_LINK_ST_RECOVERY))) {
                /* Recovery mode ignores power-off and never sleeps: restart it normally first. */
                s_recovery = false;
                GPIOB->BRR = 1U << 6;
                s_flags = 0;
                s_ready = false;
                hybrid_nrf_reset();
            }
            if (off) wait_ready(OFF_READY_MS); /* an nRF52 still booting must hear it too */
            if (!send_frame(m.type, m.data, m.len) && off) {
                vTaskDelay(pdMS_TO_TICKS(20));
                wait_ready(OFF_READY_MS); /* not taken (still starting?): one more try */
                (void)send_frame(m.type, m.data, m.len);
            }
            if (off) {
                s_ready = false; /* keys and the watchdog leave the nRF52 alone from now on */
                s_power_off_done = true;
                for (;;) vTaskDelay(portMAX_DELAY);
            }
            vTaskDelay(pdMS_TO_TICKS(1)); /* let the nRF52 re-arm its SPI slave */
        } else {
            ble_link_frame_t rx;
            poll(&rx);
        }
    }
}

bool hybrid_ble_update_submit(const uint8_t *data, uint8_t len) {
    if (s_upd_pending || !s_ready || len == 0 || len > BLE_LINK_DATA_MAX) return false;
    memcpy(s_upd_buf, data, len);
    s_upd_len = len;
    s_last_active = xTaskGetTickCount();
    s_upd_pending = true;
    xQueueSend(s_queue, &(link_msg_t){.type = BLE_LINK_NOP}, 0); /* wake the link task */
    return true;
}

bool hybrid_ble_update_active(void) {
    if (s_installing && (int32_t)(s_quiet_until - xTaskGetTickCount()) <= 0) s_installing = false;
    return s_upd_pending || s_upd_state != BLE_LINK_UPD_ST_IDLE || s_installing;
}

void hybrid_ble_update_status(uint8_t *state, uint32_t *staged, bool *pending, bool *failed) {
    *state = s_upd_state;
    *staged = s_upd_staged;
    *pending = s_upd_pending;
    *failed = s_upd_failed;
}

void hybrid_ble_nrf_restart(uint32_t quiet_ms, bool recovery) {
    s_quiet_until = xTaskGetTickCount() + pdMS_TO_TICKS(quiet_ms);
    s_upd_state = 0;
    s_installing = true;
    s_recovery = recovery;
    s_flags = 0; /* no stale recovery flag while the chip restarts */
    if (recovery) GPIOB->BSRR = 1U << 6; /* held until the next normal restart */
    else GPIOB->BRR = 1U << 6;
    hybrid_nrf_reset();
}

static bool enqueue(uint8_t type, const void *data, uint8_t len) {
    /* The nRF52 drops reports while no host is secured, so the polled flag need not gate them. */
    if (!s_queue || !s_ready) return false;
    s_last_active = xTaskGetTickCount();
    link_msg_t m = {.type = type, .len = len};
    memcpy(m.data, data, len);
    int slot = type == BLE_LINK_KEYBOARD ? 0 : 1;
    taskENTER_CRITICAL();
    bool queued = !s_held_set[slot] && xQueueSend(s_queue, &m, 0) == pdTRUE; /* keeps the order */
    if (!queued) {
        s_held[slot] = m;
        s_held_set[slot] = true;
    }
    taskEXIT_CRITICAL();
    if (!queued) xQueueSend(s_queue, &(link_msg_t){.type = BLE_LINK_NOP}, 0); /* wake the link task */
    return true;
}

/* Overrides the weak USB-layer hooks: used while no USB host is connected or Bluetooth is selected. */
bool hid_alt_send_keyboard(const USB_HID_KeyboardReport_TypeDef *report) {
    uint8_t r[8] = {report->Modifier, 0};
    memcpy(&r[2], report->Keycodes, 6);
    return enqueue(BLE_LINK_KEYBOARD, r, sizeof(r));
}

bool hid_alt_send_consumer(uint16_t usage) {
    uint8_t r[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
    return enqueue(BLE_LINK_CONSUMER, r, sizeof(r));
}


bool hybrid_ble_battery(uint8_t percent) {
    taskENTER_CRITICAL();
    s_battery = percent;
    s_battery_dirty = true;
    taskEXIT_CRITICAL();
    return true;
}

uint8_t hybrid_ble_flags(void) {
    return s_flags;
}

uint8_t hybrid_ble_slot(void) {
    return s_slot;
}

bool hid_alt_preferred(void) {
    return s_prefer_ble;
}

bool hybrid_ble_transport_active(void) {
    return s_prefer_ble || !tud_mounted();
}

uint8_t hybrid_ble_scan_rate(void) {
    return s_ble_scan_rate;
}

void hybrid_ble_set_scan_rate(uint8_t rate) {
    if (rate > 4 || rate == s_ble_scan_rate) return;
    s_ble_scan_rate = rate;
    settings_save();
}

/* Stock Hybrid chords (hybrid_chords.h), detected by key position so a VIA remap of Fn,
 * Ctrl or the number row cannot break them. */
#define STANDBY_MS 600000U
#define FLASH_MS 1000U
#define MAX_SWALLOWED 4U

static uint32_t s_swallowed[MAX_SWALLOWED]; /* passkey keycodes whose release is swallowed too */
static uint64_t s_chord_keys; /* chord keys (by position) whose release is swallowed too */
static bool s_delete_held, s_delete_swallowed;
static bool s_pair_standby;
static TickType_t s_standby_at;
static TickType_t s_flash_at = 0; /* LED: 1 s on after start-up, a slot or USB switch, or cancel */
static uint8_t s_passkey[6];
static uint8_t s_passkey_len;

static bool command(uint8_t cmd, const uint8_t *arg, uint8_t arg_len) {
    if (!s_queue || arg_len > 7) return false;
    link_msg_t m = {.type = BLE_LINK_COMMAND, .len = (uint8_t)(1U + arg_len), .data = {cmd}};
    if (arg_len) memcpy(&m.data[1], arg, arg_len);
    return xQueueSend(s_queue, &m, 0) == pdTRUE;
}

bool hybrid_ble_power_off(void) {
    return command(BLE_LINK_CMD_POWER_OFF, NULL, 0);
}

bool hybrid_ble_power_off_done(void) {
    return s_power_off_done;
}

static void flash_led(void) {
    s_flash_at = xTaskGetTickCount();
}

static void swallow(uint32_t key) {
    for (unsigned i = 0; i < MAX_SWALLOWED; i++) {
        if (!s_swallowed[i]) {
            s_swallowed[i] = key;
            return;
        }
    }
}

static bool release_swallowed(uint32_t key) {
    for (unsigned i = 0; i < MAX_SWALLOWED; i++) {
        if (key && s_swallowed[i] == key) {
            s_swallowed[i] = 0;
            return true;
        }
    }
    return false;
}

/* While the host shows a passkey, digits, Backspace and Return go to the nRF52, not the host. */
static bool passkey_key(uint32_t key) {
    if (!(s_flags & BLE_LINK_ST_PASSKEY)) {
        s_passkey_len = 0;
        return false;
    }
    if (key >= K_1 && key <= K_0) {
        if (s_passkey_len < sizeof(s_passkey)) s_passkey[s_passkey_len++] = (uint8_t)(key == K_0 ? '0' : '1' + (key - K_1));
    } else if (key == K_BSPC || key == K_DEL) {
        if (s_passkey_len) s_passkey_len--;
    } else if (key == K_ENT || key == K_KP_ENTER) {
        if (s_passkey_len == sizeof(s_passkey)) command(BLE_LINK_CMD_PASSKEY, s_passkey, sizeof(s_passkey));
        s_passkey_len = 0;
    } else {
        return false;
    }
    return true;
}

static bool chord(int pos) {
    if (s_pair_standby && (TickType_t)(xTaskGetTickCount() - s_standby_at) > pdMS_TO_TICKS(STANDBY_MS)) {
        s_pair_standby = false;
    }
    uint8_t slot = 0;
    switch (hybrid_chord(pos, hhkb_ctrl_held(), s_delete_held, s_pair_standby,
                         (s_flags & BLE_LINK_ST_PAIRING) != 0, &slot)) {
    case CHORD_DELETE:
        command(BLE_LINK_CMD_DELETE_SLOT, &slot, 1);
        hybrid_power_request_off(); /* manual p.15: orange while deleting, then OFF */
        s_pair_standby = false;
        break;
    case CHORD_PAIR:
        s_slot = slot;
        s_prefer_ble = true;
        command(BLE_LINK_CMD_PAIR, &slot, 1);
        s_pair_standby = false;
        break;
    case CHORD_SELECT:
        s_slot = slot;
        s_prefer_ble = true;
        flash_led();
        s_pair_standby = false;
        break;
    case CHORD_USB:
        s_prefer_ble = false;
        s_pair_standby = false;
        flash_led();
        break;
    case CHORD_STANDBY:
        s_pair_standby = true;
        s_standby_at = xTaskGetTickCount();
        break;
    case CHORD_CANCEL:
        s_pair_standby = false;
        command(BLE_LINK_CMD_PAIR_CANCEL, NULL, 0);
        flash_led();
        break;
    case CHORD_CLEAR_ALL:
        s_pair_standby = false;
        command(BLE_LINK_CMD_CLEAR_BONDS, NULL, 0);
        hybrid_power_request_off();
        break;
    default:
        return false;
    }
    settings_save();
    return true;
}

bool hybrid_ble_process_key(uint32_t key, bool pressed) {
    if (!pressed) return release_swallowed(key);
    if (!passkey_key(key)) return false;
    swallow(key);
    return true;
}

/* Fn chords by position, never reaching the host. The Delete key also counts for the delete
 * chords; in pairing standby it never reaches the host either. */
bool hybrid_ble_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    int16_t pos = keyboard_get_flat_key_index(row, col);
    if (pos < 0 || pos >= 64) return false;
    if (pos == POS_DEL) {
        s_delete_held = pressed;
        if (pressed) s_delete_swallowed = s_pair_standby;
        bool swallowed = s_delete_swallowed;
        if (!pressed) s_delete_swallowed = false;
        return swallowed;
    }
    uint64_t bit = (uint64_t)1 << pos;
    if (!pressed) {
        if (!(s_chord_keys & bit)) return false;
        s_chord_keys &= ~bit;
        return true;
    }
    if (!hhkb_fn_held() || !chord(pos)) return false;
    s_chord_keys |= bit;
    return true;
}

/* 1 s on after start-up or a switch, 4 blinks/s while pairing or waiting for a passkey,
 * 2 blinks/s in pairing standby; otherwise off (no light while reconnecting or connected). */
bool hybrid_ble_status_led(TickType_t now) {
    if ((TickType_t)(now - s_flash_at) < pdMS_TO_TICKS(FLASH_MS)) return true;
    if (s_flags & (BLE_LINK_ST_PAIRING | BLE_LINK_ST_PASSKEY)) return (now / pdMS_TO_TICKS(125)) & 1U;
    return s_pair_standby && ((now / pdMS_TO_TICKS(250)) & 1U);
}

void hybrid_ble_early_init(void) {
    settings_load();
    s_queue = xQueueCreateStatic(sizeof(s_queue_buf) / sizeof(link_msg_t), sizeof(link_msg_t), s_queue_buf,
                                 &s_queue_ctl);
#ifdef HHKB_ADC_DIAGNOSTICS
    stack_diag_register(STACK_DIAG_BLE, s_task_stack, sizeof(s_task_stack) / sizeof(StackType_t));
#endif
    xTaskCreateStatic(link_task, "ble", sizeof(s_task_stack) / sizeof(StackType_t), NULL, TASK_PRIO_USBD,
                      s_task_stack, &s_task_tcb);
}
#endif
