#ifndef EEPROM_H
#define EEPROM_H

#include <stdbool.h>
#include <stdint.h>

/* Write one data EEPROM word (skipped if unchanged); true when it reads back. Safe from any task:
 * the EEPROM stays unlocked while any writer is mid-write. Not for use from interrupts. */
bool eeprom_write_word(volatile uint32_t *dst, uint32_t value);

#endif
