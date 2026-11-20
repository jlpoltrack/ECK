#ifndef HHKB_HYBRID_BLE_H
#define HHKB_HYBRID_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

/* Report the battery level to the nRF52; resent after a link restart. */
bool hybrid_ble_battery(uint8_t percent);
/* Queue the nRF52 power-off; false if the queue is full (retry). Done once it was sent. */
bool hybrid_ble_power_off(void);
bool hybrid_ble_power_off_done(void);
/* Bluetooth host slot 1..4 in use. */
uint8_t hybrid_ble_slot(void);
/* True while key reports go to Bluetooth (no USB host, or Bluetooth selected). */
bool hybrid_ble_transport_active(void);
/* USB is the output and the host has suspended it. */
bool hybrid_usb_asleep(void);
/* Bluetooth status LED in the stock patterns (see hybrid_ble.c). */
bool hybrid_ble_status_led(TickType_t now);
/* nRF52 update: queue one BLE_LINK_UPDATE payload (false while the previous one is in flight). */
bool hybrid_ble_update_submit(const uint8_t *data, uint8_t len);
bool hybrid_ble_update_active(void);
void hybrid_ble_update_status(uint8_t *state, uint32_t *staged, bool *pending, bool *failed);
/* Reset the nRF52 and pause the link watchdog for quiet_ms (PFU's bootloader copying);
 * recovery: hold PB6 high so it starts in recovery mode (until the next normal restart). */
void hybrid_ble_nrf_restart(uint32_t quiet_ms, bool recovery);
/* Last BLE_LINK_ST_* flags reported by the nRF52. */
uint8_t hybrid_ble_flags(void);

#endif
