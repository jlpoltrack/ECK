#include "app_usb_hid.h"
#include "config.h"
#include "keyboard.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "task_internal.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "via.h"
#include "ec_settings.h"

static TaskHandle_t s_usb_task_handle = NULL;

#ifndef KEYBOARD_REPORT_QUEUE_LENGTH
#define KEYBOARD_REPORT_QUEUE_LENGTH 32
#endif
typedef struct {
    uint8_t report_id;
    USB_HID_KeyboardReport_TypeDef keyboard;
    uint16_t consumer;
} queued_key_report_t;
static StaticQueue_t s_keyboard_queue_control;
static uint8_t s_keyboard_queue_storage[KEYBOARD_REPORT_QUEUE_LENGTH * sizeof(queued_key_report_t)];
static QueueHandle_t s_keyboard_queue;
/* State last routed to USB: what the USB host should hold. Resync sends only this, so keys
 * pressed while reports went to Bluetooth never appear on (and stick on) the USB host. */
static USB_HID_KeyboardReport_TypeDef s_usb_keyboard = {.ReportID = 1};
static uint16_t s_usb_consumer;
/* The host stopped taking reports while the queue was full: merge instead of waiting. */
static volatile bool s_usb_stalled;
#define USB_QUEUE_WAIT_MS 20U
#ifdef HHKB_SCAN_DIAGNOSTICS
static volatile uint32_t s_usb_keyboard_queue_peak;
static volatile uint32_t s_usb_keyboard_queue_full;
#endif

// Resend current states after reconnect, even if no new event occurs.
void USB_HID_ResyncKeyboard(void) {
    if (!s_keyboard_queue) return;
    taskENTER_CRITICAL();
    xQueueReset(s_keyboard_queue);
    queued_key_report_t keyboard = {.report_id = 1, .keyboard = s_usb_keyboard};
    queued_key_report_t consumer = {.report_id = 2, .consumer = s_usb_consumer};
    xQueueSend(s_keyboard_queue, &keyboard, 0);
    xQueueSend(s_keyboard_queue, &consumer, 0);
    taskEXIT_CRITICAL();
}

static void usb_keyboard_flush(void) {
    queued_key_report_t report;
    if (!s_keyboard_queue || !tud_mounted() || tud_suspended() || !tud_hid_ready() ||
        xQueuePeek(s_keyboard_queue, &report, 0) != pdTRUE) return;
    bool sent = report.report_id == 1
        ? tud_hid_keyboard_report(1, report.keyboard.Modifier, report.keyboard.Keycodes)
        : tud_hid_report(2, &report.consumer, sizeof(report.consumer));
    if (sent) {
        xQueueReceive(s_keyboard_queue, &report, 0);
        s_usb_stalled = false;
    }
}

static bool usb_enqueue_key_report(const queued_key_report_t *report) {
    taskENTER_CRITICAL();
    if (report->report_id == 1) s_usb_keyboard = report->keyboard;
    else s_usb_consumer = report->consumer;
    taskEXIT_CRITICAL();
    if (tud_suspended()) {
        // If Power Saving is NOT enabled (or disabled via DIP switch/config),
        // issue USB Remote Wakeup on keypress!
        if (!keyboard_power_saving_enabled()) {
            tud_remote_wakeup();
        }
    }
    /* Bounded: the keyboard task also runs the power switch, battery and LED ticks. */
    TickType_t start = xTaskGetTickCount();
    while (tud_mounted() && !tud_suspended()) {
#ifdef HHKB_SCAN_DIAGNOSTICS
        if (uxQueueSpacesAvailable(s_keyboard_queue) == 0) s_usb_keyboard_queue_full++;
#endif
        if (xQueueSend(s_keyboard_queue, report, s_usb_stalled ? 0 : pdMS_TO_TICKS(1)) == pdTRUE) {
#ifdef HHKB_SCAN_DIAGNOSTICS
            UBaseType_t waiting = uxQueueMessagesWaiting(s_keyboard_queue);
            if (waiting > s_usb_keyboard_queue_peak) s_usb_keyboard_queue_peak = waiting;
#endif
            if (s_usb_task_handle) xTaskNotifyGive(s_usb_task_handle);
            return true;
        }
        if (s_usb_stalled || (TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(USB_QUEUE_WAIT_MS)) {
            /* Reports carry the whole state: keep only the newest keyboard and consumer report. */
            s_usb_stalled = true;
            USB_HID_ResyncKeyboard();
            if (s_usb_task_handle) xTaskNotifyGive(s_usb_task_handle);
            return true;
        }
    }
    return true;
}

static StaticTask_t s_usb_task_tcb;
static StackType_t s_usb_task_stack[256]; /* static worst case ~640 B (tools/stack_depth.py) */

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
    (void)rhport;
    (void)eventid;
    if (s_usb_task_handle == NULL) {
        return;
    }
    if (in_isr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(s_usb_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        xTaskNotifyGive(s_usb_task_handle);
    }
}

static void usb_device_task(void *param) {
    (void)param;
    s_usb_task_handle = xTaskGetCurrentTaskHandle();
    while (1) {
        tud_task_ext(0, false);
        usb_keyboard_flush();
        via_flush_pending_report();
        hid_alt_task();
#ifdef VIAL_ENABLE
        via_mode_task();
#endif
        hhkb_settings_task();
        via_eeprom_task();
        usb_diag_task();
        debug_console_flush();
        ulTaskNotifyTake(pdTRUE, usb_device_poll_ticks());
    }
}

bool USB_HID_Init(void) {
    s_keyboard_queue = xQueueCreateStatic(KEYBOARD_REPORT_QUEUE_LENGTH, sizeof(queued_key_report_t),
                                           s_keyboard_queue_storage, &s_keyboard_queue_control);
    if (!s_keyboard_queue) return false;

    tusb_init();
#ifdef HHKB_ADC_DIAGNOSTICS
    stack_diag_register(STACK_DIAG_USBD, s_usb_task_stack, sizeof(s_usb_task_stack) / sizeof(StackType_t));
#endif
    s_usb_task_handle = xTaskCreateStatic(usb_device_task, "usbd", sizeof(s_usb_task_stack) / sizeof(StackType_t), NULL, TASK_PRIO_USBD, s_usb_task_stack, &s_usb_task_tcb);
    return (s_usb_task_handle != NULL);
}

void usb_notify_task(void) {
    if (s_usb_task_handle) {
        xTaskNotifyGive(s_usb_task_handle);
    }
}

/* Alternate transport for key reports while no USB host is connected (e.g. HHKB Hybrid Bluetooth).
 * Return true when the report was taken. */
__attribute__((weak)) bool hid_alt_send_keyboard(const USB_HID_KeyboardReport_TypeDef *report) {
    (void)report;
    return false;
}

__attribute__((weak)) bool hid_alt_send_consumer(uint16_t usage) {
    (void)usage;
    return false;
}

__attribute__((weak)) void hid_alt_task(void) {}

/* Board hook: how long the USB task sleeps between polls (a board with USB off can poll rarely). */
__attribute__((weak)) TickType_t usb_device_poll_ticks(void) {
    return pdMS_TO_TICKS(2);
}

/* True when the alternate transport should win even while a USB host is connected. */
__attribute__((weak)) bool hid_alt_preferred(void) {
    return false;
}

/* Transport that took the last report: the one left behind gets a release, so no key or
 * modifier stays held on a host when reports move between USB and the alternate transport. */
static bool s_keyboard_on_alt;
static bool s_consumer_on_alt;

static bool hid_route_to_alt(void) {
    return !tud_mounted() || hid_alt_preferred();
}

bool USB_HID_SendReport(const USB_HID_KeyboardReport_TypeDef *report) {
    if (!report || !s_keyboard_queue) return false;
    // On a routing change, release everything on the transport left behind, even if the new one
    // drops this report (else a key held there stays down).
    bool to_alt = hid_route_to_alt();
    if (to_alt != s_keyboard_on_alt) {
        static const USB_HID_KeyboardReport_TypeDef released;
        queued_key_report_t usb_released = {.report_id = 1};
        if (s_keyboard_on_alt) (void)hid_alt_send_keyboard(&released);
        else (void)usb_enqueue_key_report(&usb_released);
        s_keyboard_on_alt = to_alt;
    }
    if (to_alt && hid_alt_send_keyboard(report)) return true;
    if (hid_alt_preferred()) return false; /* alternate transport selected but not ready: drop */
    queued_key_report_t queued = {.report_id = 1, .keyboard = *report};
    return usb_enqueue_key_report(&queued);
}

bool USB_HID_SendConsumerReport(uint16_t usage) {
    if (!s_keyboard_queue) return false;
    bool to_alt = hid_route_to_alt();
    if (to_alt != s_consumer_on_alt) {
        queued_key_report_t usb_released = {.report_id = 2};
        if (s_consumer_on_alt) (void)hid_alt_send_consumer(0);
        else (void)usb_enqueue_key_report(&usb_released);
        s_consumer_on_alt = to_alt;
    }
    if (to_alt && hid_alt_send_consumer(usage)) return true;
    if (hid_alt_preferred()) return false;
    queued_key_report_t queued = {.report_id = 2, .consumer = usage};
    return usb_enqueue_key_report(&queued);
}
