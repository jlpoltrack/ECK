#pragma once
/* Host mock of stm32/board/eeprom.h: the test's EEPROM is plain memory. */
#include <stdbool.h>
#include <stdint.h>
static inline bool eeprom_write_word(volatile uint32_t *dst, uint32_t value) {
    *dst = value;
    return true;
}
