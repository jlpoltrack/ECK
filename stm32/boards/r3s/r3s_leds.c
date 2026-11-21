/* Realforce R3S indicators: four RGB LEDs. PB6, PB5, PB4, PB3 select Num, Caps, Scroll and Simple
 * Mode; PB7 blue, PB8 green, PB9 red are the shared colour lines; all active high
 * (docs/r3s-hardware.md). TIM21 lights one LED at a time (1 ms slots), so each has its own colour. */
#include "board.h"
#include "board_config.h"
#include "ec_settings.h"
#include "stm32l0xx.h"

#define BLUE (1U << 7)
#define GREEN (1U << 8)
#define RED (1U << 9)
#define COLOUR_ALL (BLUE | GREEN | RED)
#define SELECT_ALL ((1U << 6) | (1U << 5) | (1U << 4) | (1U << 3))
#define SIMPLE 3U /* slot of the Simple Mode LED */
#define NOTIFY_COLOUR BLUE /* confirmation blinks, as the Hybrid's blue LED */
#define LEVEL_TICKS 200U   /* 5 % of the 4 ms cycle per brightness level (VIA); level 5 = the whole slot */
#define CCR1_OF(level) ((level) * LEVEL_TICKS - 1U) /* at most ARR: a larger CCR1 fires with the update (RM0367 22.4.5) */

static const uint32_t k_select[4] = {1U << 6, 1U << 5, 1U << 4, 1U << 3}; /* Num, Caps, Scroll, Simple */
static volatile uint32_t s_colour[4]; /* colour lines per LED; 0 = off */

/* 1 kHz: each period lights the next LED in its colour; the compare ends it (brightness). */
void TIM21_IRQHandler(void) {
    static uint8_t slot;
    uint32_t sr = TIM21->SR;
    TIM21->SR = ~sr;
    /* Slot end before slot start: the scan masks interrupts, so both can be pending together. */
    if (sr & TIM_SR_CC1IF) GPIOB->BRR = SELECT_ALL | COLOUR_ALL;
    if (sr & TIM_SR_UIF) {
        slot = (uint8_t)((slot + 1U) & 3U);
        uint32_t on = s_colour[slot] ? k_select[slot] | s_colour[slot] : 0;
        GPIOB->BSRR = on | (((SELECT_ALL | COLOUR_ALL) & ~on) << 16);
    }
}

void board_leds_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    GPIOB->BRR = SELECT_ALL | COLOUR_ALL;
    for (uint32_t pin = 3; pin <= 9; pin++) {
        GPIOB->MODER = (GPIOB->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
        GPIOB->OTYPER &= ~(1U << pin);
        GPIOB->PUPDR &= ~(3U << (pin * 2));
    }
    RCC->APB2ENR |= RCC_APB2ENR_TIM21EN;
    TIM21->PSC = SystemCoreClock / 1000000U - 1U; /* 1 MHz */
    TIM21->ARR = 999U;                            /* 1 kHz */
    TIM21->CCR1 = CCR1_OF(BOARD_LED_LEVEL_DEFAULT);
    TIM21->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM21->EGR = TIM_EGR_UG;
    TIM21->SR = 0;
    NVIC_SetPriority(TIM21_IRQn, (1U << __NVIC_PRIO_BITS) - 1U);
    NVIC_EnableIRQ(TIM21_IRQn);
    TIM21->CR1 = TIM_CR1_CEN;
}

void board_leds_off(void) {
    for (unsigned i = 0; i < 4; i++) s_colour[i] = 0;
    GPIOB->BRR = SELECT_ALL | COLOUR_ALL;
}

void board_leds_write(bool lit0, bool lit1, uint8_t host_leds) {
    (void)lit0;
    uint8_t c = hhkb_get_led_style(0); /* VIA: bit 0 red, 1 green, 2 blue */
    uint32_t colour = (c & 1U ? RED : 0) | (c & 2U ? GREEN : 0) | (c & 4U ? BLUE : 0);
    TIM21->CCR1 = CCR1_OF(hhkb_get_led_style(1));
    for (unsigned i = 0; i < 3; i++) s_colour[i] = ((host_leds >> i) & 1U) ? colour : 0;
    s_colour[SIMPLE] = lit1 ? colour : 0;
}

void board_leds_notify(bool on) {
    for (unsigned i = 0; i < 3; i++) s_colour[i] = 0;
    s_colour[SIMPLE] = on ? NOTIFY_COLOUR : 0;
}

/* Before ST's DFU loader: Simple Mode lit magenta (red + blue; static, so no orange: owner's
 * choice) and left so, as the jump keeps GPIOB (ROM_DFU_KEEP_IOP). */
void board_leds_dfu(void) {
    NVIC_DisableIRQ(TIM21_IRQn);
    TIM21->CR1 = 0;
    uint32_t on = k_select[SIMPLE] | RED | BLUE;
    GPIOB->BSRR = on | (((SELECT_ALL | COLOUR_ALL) & ~on) << 16);
}
