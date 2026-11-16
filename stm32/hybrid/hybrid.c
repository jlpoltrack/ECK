#ifdef HHKB_HYBRID
#include "hybrid.h"

#include "FreeRTOS.h"
#include "app_usb_hid.h"
#include "task.h"
#include "rom_dfu.h"
#include "stm32l0xx.h"

void hybrid_early_init(void) {
    __disable_irq();
    SysTick->CTRL = 0;
    /* Bank 1 leaves SysTick pending; NVIC->ICPR does not cover core exceptions. */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    EXTI->IMR = 0;
    EXTI->EMR = 0;
    EXTI->PR = 0xFFFFFFFFUL;
    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
    RCC->AHBRSTR |= RCC_AHBRSTR_DMARST;
    RCC->AHBRSTR &= ~RCC_AHBRSTR_DMARST;
    /* SWD through the idle WFI only on USB power (PC13 low): on batteries it keeps the core
     * clock running in Sleep (~1.5-3 mA), and DBGMCU survives resets, so clear it there. */
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN;
    GPIOC->MODER &= ~(3U << (2 * 13));
    RCC->APB2ENR |= RCC_APB2ENR_DBGEN;
    DBGMCU->CR = (GPIOC->IDR & (1U << 13)) ? 0 : DBGMCU_CR_DBG_SLEEP;
    __enable_irq();
}

static bool s_battery_mode;

bool hybrid_on_battery(void) {
    return s_battery_mode;
}

/* Battery mode has USB powered down (hybrid_power); the task still saves settings (VIA EEPROM). */
TickType_t usb_device_poll_ticks(void) {
    return pdMS_TO_TICKS(s_battery_mode ? 1000U : 2U);
}

void hybrid_hardware_init(void) {
    /* On batteries run from HSI16 without the PLL: the PLL at 32 MHz costs ~2.5 mA (measured).
     * Scan delays (TIM2), the ADC clock and the RTOS tick follow SystemCoreClock. */
    s_battery_mode = (GPIOC->IDR & (1U << 13)) != 0; /* PC13 low = USB power */
    if (s_battery_mode) {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
        RCC->CR &= ~RCC_CR_PLLON;
        SystemCoreClock = 16000000U;
    }
    /* Stock pulses D2 high during init and keeps it low afterwards. */
    RCC->IOPENR |= RCC_IOPENR_GPIODEN;
    GPIOD->BRR = 1U << 2;
    GPIOD->MODER = (GPIOD->MODER & ~(3U << (2 * 2))) | (1U << (2 * 2));
}

/* ST's USB DFU loader: tools/hybrid_update.py writes ECK (and stock) through it.
 * Refused on batteries (it needs USB power) and on an unknown ROM: the keyboard keeps running. */
bool board_bootloader_jump(void) {
    if (!s_battery_mode) (void)rom_dfu_enter(); /* returns only on an unknown ROM */
    return true;
}

void hybrid_delay_us(uint32_t us) {
    if (!(SysTick->CTRL & SysTick_CTRL_ENABLE_Msk)) {
        /* SysTick stopped (power-off path): a loop iteration takes >= 12 cycles, so 4 per us
         * is at least 1.5x long at 32 MHz and 3x at 16 MHz. */
        for (volatile uint32_t i = 0; i < us * 4U; i++) {}
        return;
    }
    uint32_t load = SysTick->LOAD + 1U, need = us * (SystemCoreClock / 1000000U);
    uint32_t prev = SysTick->VAL, done = 0;
    while (done < need) { /* SysTick counts down from LOAD and reloads */
        uint32_t now = SysTick->VAL;
        done += (prev >= now) ? prev - now : prev + load - now;
        prev = now;
    }
}

void hybrid_nrf_reset(void) {
    GPIOD->BSRR = 1U << 2;
    hybrid_delay_us(10000); /* was a ~8-9 ms counted loop */
    GPIOD->BRR = 1U << 2;
}

#endif
