#ifndef EEPROM_H
#define EEPROM_H

#include <stdbool.h>
#include <stdint.h>

/* Write one data EEPROM word (skipped if unchanged); true when it reads back. Safe from any task:
 * the EEPROM stays unlocked while any writer is mid-write. Not for use from interrupts. */
bool eeprom_write_word(volatile uint32_t *dst, uint32_t value);

#ifdef HHKB_HYBRID
/* Bluetooth updates: Bank 1 staging, only 0x08001000-0x0800FFFF (never the boot stub), read-while-write
 * from Bank 2. Task context only; each suspends the scheduler for one ~3.2 ms flash operation. */
#define FLASH_STAGE_MIN 0x08001000UL
#define FLASH_STAGE_END 0x08010000UL
bool flash_stage_erase_page(uint32_t addr);                  /* 128-byte page; erased reads 0 */
bool flash_stage_write_half(uint32_t addr, const uint32_t w[16]); /* 64-byte half page, erased first */
#endif

#endif
