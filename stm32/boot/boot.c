/* ECK boot stub for the HHKB Hybrid's STM32 Bank 1 (replaces PFU's updater there).
 * Installs a Bluetooth-staged image, boots ECK in Bank 2, or ST's USB DFU loader for recovery; see README.md. */
#include <stdbool.h>
#include <stdint.h>

#include "bank1_stage.h"
#include "rom_dfu.h"
#include "stm32l0xx.h"

#define APP_BASE 0x08010000UL
#define APP_SIZE 0x10000UL
#define TAG_VALID 0x08080360UL /* = 1 when Bank 2 is installed (PFU's layout, kept) */
#define TAG_CRC 0x08080370UL   /* CRC-16/MCRF4XX of the 64 KB Bank 2 */
#define RAM_END 0x20005000UL
#define NVM_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR)

static uint16_t crc16_mcrf4xx(uint16_t crc, const uint8_t *p, uint32_t n) {
    while (n--) {
        crc ^= p ? *p++ : 0xFFU; /* NULL: n bytes of 0xFF padding */
        for (unsigned b = 0; b < 8; b++) crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static bool vectors_ok(uint32_t base) {
    const uint32_t *v = (const uint32_t *)base;
    return v[0] >= 0x20000000UL && v[0] <= RAM_END && v[1] >= APP_BASE && v[1] < APP_BASE + APP_SIZE;
}

static bool app_valid(void) {
    if (!vectors_ok(APP_BASE)) return false;
    if (*(const volatile uint32_t *)TAG_VALID != 1U) return false;
    return (*(const volatile uint32_t *)TAG_CRC & 0xFFFFU) == crc16_mcrf4xx(0xFFFF, (const uint8_t *)APP_BASE, APP_SIZE);
}

static void clock_hsi16(void) {
    FLASH->ACR |= FLASH_ACR_LATENCY; /* 1 wait state: 16 MHz in the reset voltage range */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR = RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
}

/* Power switch PC5 held (low) while USB power is present (PC13 low). */
static bool recovery_requested(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3U << (2 * 5))) | (1U << (2 * 5));
    GPIOC->MODER &= ~((3U << (2 * 5)) | (3U << (2 * 13)));
    for (volatile uint32_t i = 0; i < 4000; i++) {} /* pull-up settles (~1 ms) */
    uint32_t idr = GPIOC->IDR;
    GPIOC->MODER |= (3U << (2 * 5)) | (3U << (2 * 13)); /* back to reset (analog) */
    GPIOC->PUPDR &= ~(3U << (2 * 5));
    RCC->IOPENR &= ~RCC_IOPENR_GPIOCEN;
    return !(idr & (1U << 5)) && !(idr & (1U << 13));
}

/* ---------- NVM (RM0376 3.3.4) ---------- */

static void nvm_unlock(void) {
    if (FLASH->PECR & FLASH_PECR_PELOCK) { /* a wrong key hard-faults: only when locked */
        FLASH->PEKEYR = 0x89ABCDEFU;
        FLASH->PEKEYR = 0x02030405U;
    }
    if (FLASH->PECR & FLASH_PECR_PRGLOCK) {
        FLASH->PRGKEYR = 0x8C9DAEBFU;
        FLASH->PRGKEYR = 0x13141516U;
    }
    FLASH->SR = NVM_ERRORS | FLASH_SR_EOP;
}

/* EOP is set only when the operation was not aborted (FWWERR). */
static bool nvm_done(uint32_t mode) {
    while (FLASH->SR & FLASH_SR_BSY) {}
    bool ok = (FLASH->SR & FLASH_SR_EOP) && !(FLASH->SR & NVM_ERRORS);
    FLASH->SR = NVM_ERRORS | FLASH_SR_EOP;
    FLASH->PECR &= ~mode;
    return ok;
}

/* Bank 2 and the header page only: never the stub's page. */
static bool nvm_erase(uint32_t addr) {
    if (addr != STAGE_HDR && (addr < APP_BASE || addr >= APP_BASE + APP_SIZE)) return false;
    FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_PROG;
    *(volatile uint32_t *)addr = 0;
    return nvm_done(FLASH_PECR_ERASE | FLASH_PECR_PROG);
}

static bool eeprom_word(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
    while (FLASH->SR & FLASH_SR_BSY) {}
    bool ok = !(FLASH->SR & NVM_ERRORS) && *(volatile uint32_t *)addr == value;
    FLASH->SR = NVM_ERRORS | FLASH_SR_EOP;
    return ok;
}

/* The 16 words reach the NVM with no flash access in between (a fetch aborts them, a data read
 * hard-faults): from RAM (copied there at reset), from a RAM buffer, interrupts masked. */
__attribute__((section(".ramcode"), noinline, long_call)) static void half_latch(volatile uint32_t *dst,
                                                                                const uint32_t *src) {
    __disable_irq();
    for (unsigned i = 0; i < 16U; i++) dst[i] = src[i];
    __enable_irq();
}

static bool nvm_half(uint32_t addr, const uint32_t w[16]) {
    if (addr < APP_BASE || addr >= APP_BASE + APP_SIZE || (addr & 63U)) return false;
    FLASH->PECR |= FLASH_PECR_FPRG | FLASH_PECR_PROG;
    half_latch((volatile uint32_t *)addr, w);
    return nvm_done(FLASH_PECR_FPRG | FLASH_PECR_PROG);
}

/* ---------- Bluetooth-staged install (bank1_stage.h) ---------- */

static bool stage_header(uint32_t *size, uint16_t *crc) {
    const volatile uint32_t *h = (const volatile uint32_t *)STAGE_HDR;
    uint32_t n = h[1], c = h[2];
    if (h[0] != STAGE_MAGIC || !n || n > STAGE_MAX || (n & 3U) || c > 0xFFFFU || h[3] != stage_hdr_check(n, c))
        return false;
    *size = n;
    *crc = (uint16_t)c;
    return true;
}

/* Orange LED (PA9) solid while installing, as in ST's loader and the app's STM32 updates. */
static void led_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->BSRR = 1U << 9;
    GPIOA->MODER = (GPIOA->MODER & ~(3U << (2 * 9))) | (1U << (2 * 9));
}

static void led_done(void) {
    GPIOA->BRR = 1U << 9;
    GPIOA->MODER |= 3U << (2 * 9); /* back to reset (analog) */
    RCC->IOPENR &= ~RCC_IOPENR_GPIOAEN;
}

/* Bank 2 is marked invalid first: power lost mid-copy leaves the header, so the next reset copies again. */
static bool stage_copy(uint32_t n, uint16_t crc) {
    if (!eeprom_word(TAG_CRC, 0)) return false;
    for (uint32_t a = APP_BASE; a < APP_BASE + APP_SIZE; a += 128U) {
        if (!nvm_erase(a)) return false;
    }
    for (uint32_t off = 0; off < APP_SIZE; off += 64U) {
        uint32_t w[16];
        for (unsigned i = 0; i < 16U; i++) {
            uint32_t at = off + 4U * i;
            w[i] = at < n ? *(const uint32_t *)(STAGE_BASE + at) : 0xFFFFFFFFU;
        }
        if (!nvm_half(APP_BASE + off, w)) return false;
    }
    if (crc16_mcrf4xx(0xFFFF, (const uint8_t *)APP_BASE, APP_SIZE) != crc) return false;
    return eeprom_word(TAG_VALID, 1U) && eeprom_word(TAG_CRC, crc);
}

/* Installs a staged image whose header and CRC check out; the header goes either way, so a failed
 * install falls to the DFU loader (Bank 2 invalid) and never replaces a later USB update. */
static void stage_install(void) {
    uint32_t n;
    uint16_t crc;
    const volatile uint32_t *h = (const volatile uint32_t *)STAGE_HDR;
    if (!(h[0] | h[1] | h[2] | h[3])) return;
    nvm_unlock();
    if (stage_header(&n, &crc) && vectors_ok(STAGE_BASE) &&
        crc16_mcrf4xx(crc16_mcrf4xx(0xFFFF, (const uint8_t *)STAGE_BASE, n), 0, APP_SIZE - n) == crc) {
        led_init();
        for (unsigned attempt = 0; attempt < 2U && !stage_copy(n, crc); attempt++) {}
        led_done();
    }
    (void)nvm_erase(STAGE_HDR);
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

static void __attribute__((noreturn)) start_app(void) {
    RCC->CFGR = 0; /* MSI again, as after reset */
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    RCC->CR &= ~RCC_CR_HSION;
    const uint32_t *v = (const uint32_t *)APP_BASE;
    SCB->VTOR = APP_BASE;
    __asm volatile("msr msp, %0\n bx %1" ::"r"(v[0]), "r"(v[1]) : "memory");
    __builtin_unreachable();
}

extern uint32_t _ramcode_start[], _ramcode_end[], _ramcode_load[];

void __attribute__((noreturn)) Reset_Handler(void) {
    /* Entered from ST's loader ("leave") rather than a reset: start clean. */
    if (RCC->APB1ENR & RCC_APB1ENR_USBEN) NVIC_SystemReset();
    for (uint32_t *d = _ramcode_start, *s = _ramcode_load; d < _ramcode_end;) *d++ = *s++;
    bool recovery = recovery_requested();
    clock_hsi16();
    if (recovery) rom_dfu_enter(); /* no flash writes on the recovery path; USB tools clear the header */
    stage_install();
    bool valid = app_valid();
    if (recovery || !valid) rom_dfu_enter(); /* returns only on an unknown ROM */
    if (valid) start_app();
    for (;;) {}
}

static void __attribute__((noreturn)) Fault_Handler(void) {
    NVIC_SystemReset();
}

/* Stack below 0x200014D0 would be overwritten by the ROM loader's data; keep it high. */
__attribute__((section(".isr_vector"), used)) static const uintptr_t vectors[] = {
    0x20004000UL, (uintptr_t)Reset_Handler, (uintptr_t)Fault_Handler, (uintptr_t)Fault_Handler,
};
