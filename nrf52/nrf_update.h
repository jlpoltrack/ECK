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

/* Bluetooth updates: the same staging and record through nrf_fstorage_sd while the SoftDevice runs;
 * pages in order, each assembled from data or copied from live flash (unchanged), then committed. */
enum {
    NRF_BUPD_IDLE = 0,
    NRF_BUPD_ERASING = 1,
    NRF_BUPD_READY = 2,   /* takes data for page `done`, or its commit */
    NRF_BUPD_WRITING = 3, /* committing a page */
    NRF_BUPD_RECORDING = 4,
    NRF_BUPD_STAGED = 5, /* PFU's bootloader installs it at the next reset */
    NRF_BUPD_ERROR = 6,
    NRF_BUPD_STM_READY = 7, /* an STM32 image waits for the STM32 to pull it (no record: never installed here) */
    NRF_BUPD_STM_DONE = 8,  /* the STM32 checked it */
};
enum { NRF_BUPD_E_SIZE = 1, NRF_BUPD_E_ERASE, NRF_BUPD_E_WRITE, NRF_BUPD_E_MD5, NRF_BUPD_E_ORDER, NRF_BUPD_E_BUSY,
       NRF_BUPD_E_STM32 /* the STM32 rejected the image */, NRF_BUPD_E_STUB /* its boot stub can't install */,
       NRF_BUPD_E_IMAGE /* not an ECK nRF52 image */, NRF_BUPD_E_MODEL /* STM32 image for another model */,
       NRF_BUPD_E_ARM /* not allowed on the keyboard (Fn+Ctrl+U) */ };
enum { NRF_BUPD_TARGET_NRF = 0, NRF_BUPD_TARGET_STM32 = 1 };
#define NRF_BUPD_STAGE_BASE 0x37000UL
#define NRF_BUPD_PAGE 0x1000UL
void nrf_bupd_init(void); /* after the SoftDevice is enabled */
/* STM32 target: size is the image without its 0xFF tail; stm_crc the .hfb's CRC of the padded 64 KB. */
void nrf_bupd_begin(uint32_t size, const uint8_t md5[16], uint8_t target, uint16_t stm_crc);
uint32_t nrf_bupd_size(void);
uint8_t nrf_bupd_target(void);
uint16_t nrf_bupd_stm_crc(void);
void nrf_bupd_stm_result(uint8_t err); /* 0: the STM32 accepted the pulled image */
void nrf_bupd_data(uint32_t offset, const uint8_t *data, uint8_t len);
void nrf_bupd_commit(uint32_t page, bool from_live);
void nrf_bupd_copy(uint32_t page, uint16_t count); /* count unchanged pages from live flash */
void nrf_bupd_finish(void);
void nrf_bupd_abort(void);
void nrf_bupd_refuse(uint8_t err); /* ERROR with err, when idle (nothing staged is touched) */
uint32_t nrf_bupd_live_crc(uint32_t page); /* CRC-32 (zlib) of the installed image's page */
uint8_t nrf_bupd_state(void);
uint8_t nrf_bupd_error(void);
uint16_t nrf_bupd_done(void); /* pages committed */

#endif
