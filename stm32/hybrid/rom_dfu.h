#ifndef HHKB_ROM_DFU_H
#define HHKB_ROM_DFU_H

/* Start ST's USB DFU bootloader (system memory) on the Hybrid's STM32L072.
 * Its reset code jumps back into any valid flash bank unless the BOOT0 pin selected it, so
 * this runs the ROM's data init itself and enters the loader after that bank check. */

#include <stdbool.h>
#include <stdint.h>

#include "stm32l0xx.h"

#define ROM_DFU_SP 0x200014D0UL /* the ROM's initial stack (vector 0); its data lies below */

/* Code this entry relies on, as read from the Hybrid's ROM (bootloader ID 0x447). */
static inline bool rom_dfu_known(void) {
    static const struct {
        uint32_t addr, value;
    } k[] = {
        {0x1FF00000UL, ROM_DFU_SP},  /* vector 0 */
        {0x1FF01A80UL, 0x25014C06UL}, /* init loop: ldr r4,=table; movs r5,#1 */
        {0x1FF01A98UL, 0xFB16F7FEUL}, /* bl 0x1ff000c8 (to the bank check) */
        {0x1FF00364UL, 0xFF62F7FFUL}, /* bl bank check */
        {0x1FF00368UL, 0xFECEF7FFUL}, /* bl loader init */
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (*(const volatile uint32_t *)k[i].addr != k[i].value) return false;
    }
    return true;
}

/* Put the core back near reset state: MSI clock, peripherals reset, no IRQs. */
static inline void rom_dfu_reset_state(void) {
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    RCC->CR |= RCC_CR_MSION;
    while (!(RCC->CR & RCC_CR_MSIRDY)) {}
    RCC->CFGR = 0; /* SYSCLK = MSI, no prescalers */
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSION);
    RCC->CRRCR &= ~RCC_CRRCR_HSI48ON;
    RCC->APB1RSTR = 0xFFFFFFFFUL;
    RCC->APB1RSTR = 0;
    RCC->APB2RSTR = 0xFFFFFFFFUL;
    RCC->APB2RSTR = 0;
    RCC->AHBRSTR = 0xFFFFFFFFUL;
    RCC->AHBRSTR = 0;
    RCC->IOPRSTR = 0xFFFFFFFFUL;
    RCC->IOPRSTR = 0;
    RCC->APB1ENR = 0;
    RCC->APB2ENR = 0;
    RCC->IOPENR = 0;
    EXTI->IMR = 0;
    EXTI->EMR = 0;
    EXTI->PR = 0xFFFFFFFFUL;
    SCB->VTOR = 0;
}

/* Switch to a fresh main stack at the top of RAM (task stacks may lie in the ROM's data area),
 * run the ROM's data init, then enter the loader. */
static void __attribute__((naked, noreturn, used)) rom_dfu_jump(void) {
    __asm volatile(
        ".syntax unified\n"
        "movs r0, #0\n msr control, r0\n isb\n"
        "ldr r0, =0x20005000\n msr msp, r0\n"
        "ldr r4, =0x1FF01A9C\n ldr r4, [r4]\n"
        "ldr r6, =0x1FF01AA0\n ldr r6, [r6]\n"
        "1: cmp r4, r6\n bhs 2f\n"
        "ldmia r4!, {r0-r3}\n movs r5, #1\n orrs r3, r5\n blx r3\n b 1b\n"
        "2: ldr r0, =0x200014D0\n msr msp, r0\n"
        "cpsie i\n" /* as after reset: the loader's USB runs on interrupts */
        "ldr r0, =0x1FF00369\n bx r0\n"
        ".ltorg\n");
}

/* Does not return when the ROM matches; returns false otherwise. */
static inline bool rom_dfu_enter(void) {
    if (!rom_dfu_known()) return false;
    rom_dfu_reset_state();
    rom_dfu_jump();
}

#endif
