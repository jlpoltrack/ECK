/* ECK boot stub for the HHKB Hybrid's STM32 Bank 1 (replaces PFU's updater there).
 * Boots ECK in Bank 2, or ST's USB DFU loader for recovery; see README.md. */
#include <stdbool.h>
#include <stdint.h>

#include "rom_dfu.h"
#include "stm32l0xx.h"

#define APP_BASE 0x08010000UL
#define APP_SIZE 0x10000UL
#define TAG_VALID 0x08080360UL /* = 1 when Bank 2 is installed (PFU's layout, kept) */
#define TAG_CRC 0x08080370UL   /* CRC-16/MCRF4XX of the 64 KB Bank 2 */
#define RAM_END 0x20005000UL

static uint16_t crc16_mcrf4xx(const uint8_t *p, uint32_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; b++) crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static bool app_valid(void) {
    const uint32_t *v = (const uint32_t *)APP_BASE;
    if (v[0] < 0x20000000UL || v[0] > RAM_END || v[1] < APP_BASE || v[1] >= APP_BASE + APP_SIZE) return false;
    if (*(const volatile uint32_t *)TAG_VALID != 1U) return false;
    return (*(const volatile uint32_t *)TAG_CRC & 0xFFFFU) == crc16_mcrf4xx((const uint8_t *)APP_BASE, APP_SIZE);
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

static void __attribute__((noreturn)) start_app(void) {
    RCC->CFGR = 0; /* MSI again, as after reset */
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    RCC->CR &= ~RCC_CR_HSION;
    const uint32_t *v = (const uint32_t *)APP_BASE;
    SCB->VTOR = APP_BASE;
    __asm volatile("msr msp, %0\n bx %1" ::"r"(v[0]), "r"(v[1]) : "memory");
    __builtin_unreachable();
}

void __attribute__((noreturn)) Reset_Handler(void) {
    /* Entered from ST's loader ("leave") rather than a reset: start clean. */
    if (RCC->APB1ENR & RCC_APB1ENR_USBEN) NVIC_SystemReset();
    bool recovery = recovery_requested();
    clock_hsi16();
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
