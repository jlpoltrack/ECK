#include "config.h"
#include "events.h"
#include "app_usb_hid.h"
#include "tusb.h"
#include "via.h"
#include "fmt.h"
#include "ec_settings.h"
#include "matrix_scan.h"
#include "stm32l0xx.h"
#include <stdarg.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

extern void usb_notify_task(void);
void debug_console_flush(void);
void debug_console_printf(const char *fmt, ...);

#ifndef USB_VENDOR_ID
#define USB_VENDOR_ID 0xCafe
#endif

#ifndef USB_PRODUCT_ID
#define USB_PRODUCT_ID 0x4010
#endif

#ifndef KEYBOARD_NAME
#define KEYBOARD_NAME "ECK Keyboard"
#endif

#ifndef USB_HID_COUNTRY_CODE
#define USB_HID_COUNTRY_CODE 0 /* not localized */
#endif
/* TinyUSB's TUD_HID_DESCRIPTOR with the HID country code (bCountryCode) set. */
#define KEYBOARD_HID_DESCRIPTOR(_itfnum, _stridx, _boot_protocol, _report_desc_len, _epin, _epsize, _ep_interval) \
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 1, TUSB_CLASS_HID, (uint8_t)((_boot_protocol != HID_ITF_PROTOCOL_NONE) ? (uint8_t)HID_SUBCLASS_BOOT : 0u), _boot_protocol, _stridx,\
  9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), USB_HID_COUNTRY_CODE, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(_report_desc_len),\
  7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epsize), _ep_interval

// HID Report Descriptor: Composite Keyboard (ID 1), Consumer (ID 2)
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2)),
};

// 32-byte Raw HID (VIA) descriptor
uint8_t const desc_via_report[] = {
    0x06, 0x60, 0xFF, // Usage Page (Vendor Defined 0xFF60)
    0x09, 0x61,       // Usage (Vendor Defined 0x61)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x62,       // Usage (Vendor Defined 0x62)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x20,       // Report Count (32)
    0x81, 0x02,       // Input (Data, Var, Abs)
    0x09, 0x63,       // Usage (Vendor Defined 0x63)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x20,       // Report Count (32)
    0x91, 0x02,       // Output (Data, Var, Abs)
    0xc0              // End Collection
};

// QMK-style debug console descriptor (Usage Page 0xFF31, Usage 0x0074; hid_listen, QMK Toolbox)
uint8_t const desc_console_report[] = {
    0x06, 0x31, 0xFF, // Usage Page (Vendor Defined 0xFF31)
    0x09, 0x74,       // Usage (Vendor Defined 0x0074)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x75,       //   Usage (Vendor Defined 0x0075)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x20,       //   Report Count (32)
    0x81, 0x02,       //   Input (Data, Var, Abs)
    0xc0              // End Collection
};

enum {
    ITF_NUM_KEYBOARD = 0,
    ITF_NUM_VIA,
    ITF_NUM_CONSOLE,
    ITF_TOTAL
};

// Device descriptor
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00, // Defined at interface level
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VENDOR_ID,   // Vendor ID
    .idProduct = USB_PRODUCT_ID, // Product ID
    .bcdDevice = 0x0100,          // Device release number

    .iManufacturer = 0x01, // Manufacturer string index
    .iProduct = 0x02,      // Product string index
    .iSerialNumber = 0x03, // Serial number string index

    .bNumConfigurations = 0x01 // Number of configurations
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_HID_DESC_LEN)
#define TOTAL_INTERFACES 3

#ifndef USB_REMOTE_WAKEUP_ENABLED
#define USB_REMOTE_WAKEUP_ENABLED 1
#endif

#if USB_REMOTE_WAKEUP_ENABLED
#define USB_CONFIG_ATTRIBUTES TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP
#else
#define USB_CONFIG_ATTRIBUTES 0
#endif

// Configuration descriptor
uint8_t const desc_configuration[] = {
    // Configuration Descriptor
    TUD_CONFIG_DESCRIPTOR(1,                      // Configuration number
                          TOTAL_INTERFACES,       // Number of interfaces
                          0,                      // Configuration string index
                          CONFIG_TOTAL_LEN,       // Total length of data for this configuration
                          USB_CONFIG_ATTRIBUTES,  // Attributes (bus-powered + optional remote wakeup)
                          100),                   // Maximum power consumption in 2mA units (100 * 2mA = 200mA)

#ifndef HID_POLL_INTERVAL_MS
#define HID_POLL_INTERVAL_MS 1
#endif

    // Interface 0: Composite Keyboard/Consumer HID Descriptor
    KEYBOARD_HID_DESCRIPTOR(0,                  // Interface number
                       0,                       // String index
                       HID_ITF_PROTOCOL_NONE,   // Protocol code (composite with Report IDs)
                       sizeof(desc_hid_report), // HID report descriptor length
                       0x81,                    // Endpoint address (IN endpoint)
                       CFG_TUD_HID_EP_BUFSIZE,  // Endpoint size
                       HID_POLL_INTERVAL_MS),   // Polling interval in milliseconds (1ms = 1000Hz)

#ifndef EPNUM_VIA_OUT
#define EPNUM_VIA_OUT 0x04
#endif
#ifndef EPNUM_VIA_IN
#define EPNUM_VIA_IN 0x82
#endif

#ifndef TUD_HID_INOUT_ASYM_DESCRIPTOR
#define TUD_HID_INOUT_ASYM_DESCRIPTOR(_itfnum, _stridx, _boot_protocol, _report_desc_len, _epout, _epout_size, _epin, _epin_size, _ep_interval) \
  /* Interface */\
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, TUSB_CLASS_HID, (uint8_t)((_boot_protocol) ? (uint8_t)HID_SUBCLASS_BOOT : 0), _boot_protocol, _stridx,\
  /* HID descriptor */\
  9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(_report_desc_len),\
  /* Endpoint Out */\
  7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epout_size), _ep_interval, \
  /* Endpoint In */\
  7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epin_size), _ep_interval
#endif

    // Interface 1: VIA Raw HID Descriptor (OUT: 64 bytes, IN: 32 bytes)
    TUD_HID_INOUT_ASYM_DESCRIPTOR(ITF_NUM_VIA,             // Interface number
                                  0,                        // String index
                                  HID_ITF_PROTOCOL_NONE,    // Protocol code
                                  sizeof(desc_via_report), // HID report descriptor length
                                  EPNUM_VIA_OUT,           // Endpoint address (OUT endpoint)
                                  64,                       // Endpoint OUT size (64 bytes for short-packet completion)
                                  EPNUM_VIA_IN,            // Endpoint address (IN endpoint)
                                  32,                       // Endpoint IN size (32 bytes)
                                  1),                       // Polling interval in milliseconds

#ifndef EPNUM_CONSOLE_IN
#define EPNUM_CONSOLE_IN 0x83
#endif
    // Interface 2: debug console HID descriptor (IN endpoint)
    TUD_HID_DESCRIPTOR(ITF_NUM_CONSOLE,
                       0,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_console_report),
                       EPNUM_CONSOLE_IN,
                       32,
                       1),

};

// String Descriptors
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // Supported language (English - 0x0409)
    "ECK",                      // Manufacturer string
    KEYBOARD_NAME,         // Product string
    NULL,                   // Serial number: the chip's unique ID (serial_string)
};

/* Serial number: the 96-bit unique ID as hex. Constant across updates, so hosts keep the
 * device's permissions (Chrome) and driver binding (Windows); VIA reports the version. */
#ifdef VIAL_ENABLE
#define SERIAL_PREFIX "vial:f64c2b3c-" // the Vial app finds keyboards by this
#else
#define SERIAL_PREFIX ""
#endif
static const char *serial_string(void) {
    static char s[sizeof(SERIAL_PREFIX) + 24] = SERIAL_PREFIX;
    char *id = s + sizeof(SERIAL_PREFIX) - 1;
    static const uint32_t offs[3] = {0x14, 0x04, 0x00};
    for (int w = 0; w < 3; w++) {
        uint32_t v = *(const volatile uint32_t *)(UID_BASE + offs[w]);
        for (int i = 0; i < 8; i++) id[w * 8 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
    }
    return s;
}

#ifdef VIAL_ENABLE
#define DESC_STR_MAX 39 // Vial's serial prefix + the 24-digit chip ID
#else
#define DESC_STR_MAX 31
#endif
uint16_t _desc_str[DESC_STR_MAX + 1];

// Device descriptor callback
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

// Configuration descriptor callback
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}


// HID report descriptor callback
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == ITF_NUM_VIA) {
        return desc_via_report;
    }
    if (instance == ITF_NUM_CONSOLE) {
        return desc_console_report;
    }
    (void)instance;
    return desc_hid_report;
}

// Invoked when device is mounted
void tud_mount_cb(void) {
    indicators_leds_off();
    USB_HID_ResyncKeyboard();
    debug_console_printf("[BOOT] HID console ready\r\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void) {}

bool usb_host_asleep(void) {
    return tud_mounted() && tud_suspended();
}

// Invoked when device is suspended
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    indicators_leds_off();
}

// Re-send the current keyboard state if it changed while USB was suspended.
void tud_resume_cb(void) {
    indicators_leds_off(); // the keyboard task's next tick relights them
    USB_HID_ResyncKeyboard();
}

// This callback is invoked when the host sends a GET_REPORT request.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

static uint8_t pending_via_response[32];
static volatile bool has_pending_via_response = false;

volatile uint32_t via_rx_count = 0;
volatile uint32_t via_tx_submit_count = 0;
volatile uint32_t via_tx_complete_count = 0;
volatile uint32_t via_tx_fail_count = 0;

void via_flush_pending_report(void) {
    if (has_pending_via_response && tud_hid_n_ready(ITF_NUM_VIA)) {
        if (tud_hid_n_report(ITF_NUM_VIA, 0, pending_via_response, 32)) {
            has_pending_via_response = false;
            via_tx_submit_count++;
        } else {
            via_tx_fail_count++;
        }
    }
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)report;
    (void)len;
    if (instance == ITF_NUM_VIA) {
        via_tx_complete_count++;
        via_flush_pending_report();
    }
    if (instance == ITF_NUM_CONSOLE) {
        debug_console_flush();
    }
}

static uint32_t s_last_diag_tick = 0;
static uint32_t s_usb_loop_total = 0;
static uint32_t s_usb_loop_prev = 0;

void usb_diag_task(void) {
    s_usb_loop_total++;
    uint32_t now = xTaskGetTickCount();
    uint32_t elapsed_ticks = now - s_last_diag_tick;
    if (elapsed_ticks >= pdMS_TO_TICKS(1000)) {
        s_last_diag_tick = now;
        uint32_t elapsed_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
        if (elapsed_ms == 0) elapsed_ms = 1000;

        uint32_t usb_diff = s_usb_loop_total - s_usb_loop_prev;
        s_usb_loop_prev = s_usb_loop_total;
        uint32_t usb_rate_hz = (usb_diff * 1000U + elapsed_ms / 2) / elapsed_ms; // ~1 s of loop counts fits 32 bits

        hhkb_calc_rates(elapsed_ms);

        if (hhkb_get_debug_flag(1U << 1)) { // DEBUG_FLAG_VIA
            debug_console_printf("[VIA_DIAG] rx=%lu tx_sub=%lu tx_cpl=%lu tx_fail=%lu pend=%d\r\n",
                                 (unsigned long)via_rx_count, (unsigned long)via_tx_submit_count,
                                 (unsigned long)via_tx_complete_count, (unsigned long)via_tx_fail_count,
                                 has_pending_via_response ? 1 : 0);
        }

        if (hhkb_get_debug_flag(1U << 2)) { // DEBUG_FLAG_RATE
            static const uint16_t s_target_rates[5] = {50, 125, 250, 500, 1000};
            uint8_t r_idx = hhkb_active_scan_rate();
            uint16_t target_hz = (r_idx <= 4) ? s_target_rates[r_idx] : 1000;
#ifdef HHKB_SCAN_DIAGNOSTICS
            uint32_t scan_us = 0, scan_max_us = 0, work_max_us = 0, missed = 0, q_full = 0;
            hhkb_get_scan_diagnostics(&scan_us, &scan_max_us, &work_max_us, &missed, &q_full);
            debug_console_printf("[SCAN] target=%u actual=%lu Hz | raw=%lu us (max %lu) work_max=%lu us | miss=%lu q_full=%lu (usb=%lu)\r\n",
                                 target_hz, (unsigned long)hhkb_get_matrix_rate_hz(),
                                 (unsigned long)scan_us, (unsigned long)scan_max_us,
                                 (unsigned long)work_max_us, (unsigned long)missed, (unsigned long)q_full,
                                 (unsigned long)usb_rate_hz);
            hhkb_reset_scan_diagnostics_peaks();
#else
            debug_console_printf("[RATE] matrix=%lu Hz (target: %u Hz), usb_loop=%lu Hz\r\n",
                                 (unsigned long)hhkb_get_matrix_rate_hz(), target_hz,
                                 (unsigned long)usb_rate_hz);
#endif
        }
    }
}

// This callback is invoked when the host sends a SET_REPORT request.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)report_id;
    if (instance == ITF_NUM_VIA) {
        if (bufsize >= 32) {
            via_rx_count++;
            uint8_t response[32];
            via_process_packet(buffer, response);
            if (tud_hid_n_report(ITF_NUM_VIA, 0, response, 32)) {
                via_tx_submit_count++;
                has_pending_via_response = false;
            } else {
                via_tx_fail_count++;
                memcpy(pending_via_response, response, 32);
                has_pending_via_response = true;
            }
        }
        return;
    }
    (void)instance;
    (void)bufsize;
    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        events_hid_leds(buffer[0]);
    }
}

// String descriptor callback
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        _desc_str[0] = (TUSB_DESC_STRING << 8) | 4;
    } else {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = index == 3 ? serial_string() : string_desc_arr[index];
        uint8_t chr_count = strlen(str);
        if (chr_count > DESC_STR_MAX)
            chr_count = DESC_STR_MAX;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
        _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    }
    return _desc_str;
}

#define CONSOLE_BUF_SIZE 256
static char s_console_buf[CONSOLE_BUF_SIZE];
static volatile uint16_t s_console_head = 0;
static volatile uint16_t s_console_tail = 0;

void debug_console_write(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    bool running = xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    if (running) taskENTER_CRITICAL();

    uint16_t head = s_console_head;
    uint16_t tail = s_console_tail;
    size_t free_space = (tail > head) ? (tail - head - 1) : (CONSOLE_BUF_SIZE - 1 - (head - tail));

    if (len <= free_space) {
        for (size_t i = 0; i < len; i++) {
            s_console_buf[s_console_head] = buf[i];
            s_console_head = (s_console_head + 1) % CONSOLE_BUF_SIZE;
        }
    }
    // Drop entire message if buffer is full to prevent tearing and corrupted lines

    if (running) {
        taskEXIT_CRITICAL();
        usb_notify_task();
    }
}

void debug_console_printf(const char *fmt, ...) {
    char tmp[128];
    va_list args;
    va_start(args, fmt);
    int len = fmt_vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (len > 0) {
        if ((size_t)len > sizeof(tmp) - 1) len = sizeof(tmp) - 1;
        debug_console_write(tmp, len);
    }
}

void debug_console_flush(void) {
    if (!tud_mounted() || tud_suspended()) return;
    if (!tud_hid_n_ready(ITF_NUM_CONSOLE)) return;

    if (s_console_head == s_console_tail) return;

    uint8_t packet[32];
    memset(packet, 0, sizeof(packet));

    bool running = xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    if (running) taskENTER_CRITICAL();

    size_t count = 0;
    while (s_console_tail != s_console_head && count < sizeof(packet)) {
        packet[count++] = (uint8_t)s_console_buf[s_console_tail];
        s_console_tail = (s_console_tail + 1) % CONSOLE_BUF_SIZE;
    }

    if (running) taskEXIT_CRITICAL();

    if (count > 0) {
        tud_hid_n_report(ITF_NUM_CONSOLE, 0, packet, sizeof(packet));
    }
}
