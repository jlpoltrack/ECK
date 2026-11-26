#ifndef BANK1_STAGE_H
#define BANK1_STAGE_H

/* Bluetooth-staged STM32 images in Bank 1, installed by the boot stub. The header is written just before
 * the install restart (a stale one erased at start-up), so abandoned staging never beats a USB update. */
#include <stdint.h>

#define STAGE_BASE 0x08001000UL /* below: the boot stub's page, never written by the app */
#define STAGE_MAX 0xEF80UL      /* image bytes, up to the header page */
#define STAGE_HDR 0x0800FF80UL  /* one 128-byte page; erased reads 0 */
#define STAGE_MAGIC 0x55534345UL /* "ECSU"; never rename: written to flash */
#define BOOT_TAG_CRC 0x08080370UL /* EEPROM: CRC-16 of the installed Bank 2 (PFU's tag, the stub's check) */

/* Header words: magic, size, Bank 2 CRC-16/MCRF4XX (image + 0xFF to 64 KB), check. */
static inline uint32_t stage_hdr_check(uint32_t size, uint32_t crc) {
    return ~(STAGE_MAGIC ^ size ^ (crc << 16));
}

#endif
