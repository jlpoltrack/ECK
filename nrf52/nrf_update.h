#ifndef NRF_UPDATE_H
#define NRF_UPDATE_H

/* Stages a new nRF52 image (SoftDevice + app) for PFU's nRF52 bootloader, which installs it at
 * the next reset. Call only with the SoftDevice disabled: flash is written through NVMC. */
#include <stdbool.h>
#include <stdint.h>

/* BLE_LINK_UPD_ST_* from ble_link.h */
uint8_t nrf_update_state(void);
uint32_t nrf_update_staged(void);
void nrf_update_start(uint32_t size, const uint8_t md5[16]);
/* False while still erasing: the frame must be resent. */
bool nrf_update_data(uint32_t offset, const uint8_t *data, uint8_t len);
void nrf_update_finish(void);
/* Erases one page per call while erasing; call from the main loop. */
void nrf_update_poll(void);

#endif
