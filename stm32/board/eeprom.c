#include "eeprom.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32l0xx.h"

#define FLASH_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR)

static uint8_t s_writers; /* tasks between unlock() and relock() */

static void unlock(void) {
    taskENTER_CRITICAL();
    if (s_writers++ == 0 && (FLASH->PECR & FLASH_PECR_PELOCK)) {
        FLASH->PEKEYR = 0x89ABCDEFU;
        FLASH->PEKEYR = 0x02030405U;
    }
    taskEXIT_CRITICAL();
}

static void relock(void) {
    taskENTER_CRITICAL();
    if (--s_writers == 0) FLASH->PECR |= FLASH_PECR_PELOCK;
    taskEXIT_CRITICAL();
}

bool eeprom_write_word(volatile uint32_t *dst, uint32_t value) {
    if (*dst == value) return true;
    unlock();
    bool ok = false;
    if (!(FLASH->PECR & FLASH_PECR_PELOCK)) {
        while (FLASH->SR & FLASH_SR_BSY) {}
        FLASH->SR = FLASH_ERRORS;
        *dst = value;
        while (FLASH->SR & FLASH_SR_BSY) {}
        ok = *dst == value;
    }
    relock();
    return ok;
}
