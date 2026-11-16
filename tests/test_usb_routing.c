/* USB report routing in stm32/usb/app_usb_tinyusb.c (linked separately; FreeRTOS and TinyUSB
 * mocked in tests/usb_mocks): resync after Bluetooth routing, and a host that stops polling. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app_usb_hid.h"

/* --- FreeRTOS mocks: one queue, ticks advance while a send waits --- */
static TickType_t ticks;
static uint8_t *q_buf;
static unsigned q_len, q_size, q_head, q_count, q_sends;

TickType_t xTaskGetTickCount(void) { return ticks; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) { (void)task; return pdPASS; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) { (void)task; (void)woken; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) { (void)clear; (void)wait; return 0; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name, uint32_t depth, void *arg, UBaseType_t prio,
                               StackType_t *stack, StaticTask_t *tcb) {
    (void)fn; (void)name; (void)depth; (void)arg; (void)prio; (void)stack; (void)tcb;
    return (TaskHandle_t)1; /* the USB task never runs here */
}
QueueHandle_t xQueueCreateStatic(UBaseType_t len, UBaseType_t size, uint8_t *storage, StaticQueue_t *ctl) {
    (void)ctl;
    q_buf = storage;
    q_len = (unsigned)len;
    q_size = (unsigned)size;
    return (QueueHandle_t)storage;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait) {
    (void)q;
    if (++q_sends > 100000) { puts("usb_enqueue_key_report does not return"); abort(); }
    if (q_count == q_len) {
        ticks += wait; /* blocked for the whole timeout: nobody drains the queue */
        return pdFALSE;
    }
    memcpy(&q_buf[((q_head + q_count) % q_len) * q_size], item, q_size);
    q_count++;
    return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait) {
    (void)q; (void)wait;
    if (!q_count) return pdFALSE;
    memcpy(item, &q_buf[q_head * q_size], q_size);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) {
    if (xQueuePeek(q, item, wait) != pdTRUE) return pdFALSE;
    q_head = (q_head + 1) % q_len;
    q_count--;
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t q) { (void)q; q_head = q_count = 0; return pdPASS; }
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { (void)q; return q_len - q_count; }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { (void)q; return q_count; }

/* --- TinyUSB and board mocks --- */
static bool mounted = true, hid_ready;
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return false; }
bool tud_hid_ready(void) { return hid_ready; }
bool tud_remote_wakeup(void) { return true; }
bool tud_hid_keyboard_report(uint8_t id, uint8_t mods, const uint8_t keys[6]) { (void)id; (void)mods; (void)keys; return true; }
bool tud_hid_report(uint8_t id, const void *report, uint16_t len) { (void)id; (void)report; (void)len; return true; }
void tud_task_ext(uint32_t timeout, bool in_isr) { (void)timeout; (void)in_isr; }
bool tusb_init(void) { return true; }
void hhkb_settings_task(void) {}
void via_eeprom_task(void) {}
void via_flush_pending_report(void) {}
void usb_diag_task(void) {}
void debug_console_flush(void) {}
bool keyboard_power_saving_enabled(void) { return false; }

static bool prefer_ble;
static USB_HID_KeyboardReport_TypeDef ble_report;
bool hid_alt_send_keyboard(const USB_HID_KeyboardReport_TypeDef *r) { ble_report = *r; return true; }
bool hid_alt_send_consumer(uint16_t usage) { (void)usage; return true; }
bool hid_alt_preferred(void) { return prefer_ble; }

/* Queued entries mirror queued_key_report_t: report id, keyboard report, consumer usage. */
typedef struct {
    uint8_t report_id;
    USB_HID_KeyboardReport_TypeDef keyboard;
    uint16_t consumer;
} entry_t;

static const entry_t *queued_keyboard(void) { /* newest keyboard report in the queue */
    const entry_t *found = NULL;
    for (unsigned i = 0; i < q_count; i++) {
        const entry_t *e = (const entry_t *)&q_buf[((q_head + i) % q_len) * q_size];
        if (e->report_id == 1) found = e;
    }
    return found;
}

static USB_HID_KeyboardReport_TypeDef report_with(uint8_t key) {
    USB_HID_KeyboardReport_TypeDef r = {.ReportID = 1};
    r.Keycodes[0] = key;
    return r;
}

/* A key held while reports go to Bluetooth must not reach the USB host on resync. */
static void test_resync_only_usb_state(void) {
    xQueueReset(NULL);
    prefer_ble = true;
    USB_HID_KeyboardReport_TypeDef a = report_with(0x04);
    assert(USB_HID_SendReport(&a));
    assert(ble_report.Keycodes[0] == 0x04);
    USB_HID_ResyncKeyboard(); /* tud_resume_cb / tud_mount_cb */
    const entry_t *k = queued_keyboard();
    assert(k && k->keyboard.Keycodes[0] == 0); /* was 0x04: stuck on the USB host */

    prefer_ble = false; /* back to USB: resync carries what USB holds */
    USB_HID_KeyboardReport_TypeDef b = report_with(0x05);
    assert(USB_HID_SendReport(&b));
    xQueueReset(NULL);
    USB_HID_ResyncKeyboard();
    k = queued_keyboard();
    assert(k && k->keyboard.Keycodes[0] == 0x05);
    puts("  resync sends only the USB-routed state");
}

/* Host mounted but not polling: the sender gives up after a bounded wait and keeps the newest state. */
static void test_full_queue_bounded(void) {
    xQueueReset(NULL);
    prefer_ble = false;
    hid_ready = false;
    for (unsigned i = 0; i < q_len; i++) {
        USB_HID_KeyboardReport_TypeDef r = report_with((uint8_t)(0x04 + (i & 1)));
        assert(USB_HID_SendReport(&r));
    }
    assert(q_count == q_len);
    TickType_t before = ticks;
    USB_HID_KeyboardReport_TypeDef last = report_with(0x1E);
    assert(USB_HID_SendReport(&last));
    assert(ticks - before <= 25);
    const entry_t *k = queued_keyboard();
    assert(k && k->keyboard.Keycodes[0] == 0x1E);
    /* Still stalled: no further waiting at all. */
    for (unsigned i = 0; i < q_len + 4; i++) {
        USB_HID_KeyboardReport_TypeDef r = report_with((uint8_t)(0x20 + i));
        before = ticks;
        assert(USB_HID_SendReport(&r));
        assert(ticks == before);
    }
    k = queued_keyboard();
    assert(k && k->keyboard.Keycodes[0] == (uint8_t)(0x20 + q_len + 3));
    puts("  full queue: bounded wait, newest state kept");
}

int main(void) {
    assert(USB_HID_Init());
    test_resync_only_usb_state();
    test_full_queue_bounded();
    puts("usb routing tests passed");
    return 0;
}
