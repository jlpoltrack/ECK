/**
 * @file    app_usb_hid.h
 * @brief   USB HID Keyboard driver definitions and API.
 */

#ifndef __APP_USB_HID_H
#define __APP_USB_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
/**
 * @brief Standard HID keyboard input report.
 */
typedef struct {
    uint8_t ReportID;    /* Report ID: always 1 */
    uint8_t Modifier;    /* Modifier keys bitmask */
    uint8_t Reserved;    /* Reserved byte, must be 0 */
    uint8_t Keycodes[6]; /* Up to 6 concurrent keycodes */
} USB_HID_KeyboardReport_TypeDef;
#pragma pack(pop)

/**
 * @brief  Initializes the USB HID stack.
 * @retval true on success, false otherwise.
 */
bool USB_HID_Init(void);

/**
 * @brief  Sends a keyboard input report to the host.
 * @param  report: Pointer to the populated report structure.
 * @retval true on success, false otherwise.
 */
bool USB_HID_SendReport(const USB_HID_KeyboardReport_TypeDef *report);

/**
 * @brief  Sends a consumer report to the host.
 * @param  usage: Consumer Usage Page usage ID.
 * @retval true on success, false otherwise.
 */
bool USB_HID_SendConsumerReport(uint16_t usage);

/* Queue the state last routed to USB again (USB mount and resume). */
void USB_HID_ResyncKeyboard(void);

/* Board hooks (weak defaults in app_usb_tinyusb.c). An alternate transport (e.g. HHKB Hybrid
 * Bluetooth) takes reports while no USB host is connected, or always while preferred. */
bool hid_alt_send_keyboard(const USB_HID_KeyboardReport_TypeDef *report); /* true: taken */
bool hid_alt_send_consumer(uint16_t usage);
bool hid_alt_preferred(void);
/* Runs in each USB task pass, which serves VIA over USB too (Hybrid: VIA over Bluetooth). */
void hid_alt_task(void);
/* How long the USB task sleeps between polls (a board with USB off can poll rarely). */
TickType_t usb_device_poll_ticks(void);
void usb_notify_task(void); /* wake the USB task */

#ifdef __cplusplus
}
#endif

#endif /* __APP_USB_HID_H */
