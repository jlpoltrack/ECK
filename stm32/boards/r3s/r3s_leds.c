/* Realforce R3S indicators: four RGB LEDs. PB6, PB5, PB4, PB3 select Num, Caps, Scroll and Simple
 * Mode; PB7-PB9 are the shared colour lines (PB9 red); all active high (docs/r3s-hardware.md). */
#include "board.h"
#include "stm32l0xx.h"

#define LED_NUM (1U << 6)
#define LED_CAPS (1U << 5)
#define LED_SCROLL (1U << 4)
#define LED_SIMPLE (1U << 3)
#define LED_SELECT (LED_NUM | LED_CAPS | LED_SCROLL | LED_SIMPLE)
#define LED_COLOUR_ALL (7U << 7) /* PB7-PB9; all three on = white */
#ifndef R3S_LED_COLOUR
#define R3S_LED_COLOUR (1U << 9) /* red (PB9, owner's choice); PB7 and PB8 are green and blue, order unknown */
#endif
#define LED_COLOUR R3S_LED_COLOUR
#ifndef R3S_LED_BRIGHTNESS
#define R3S_LED_BRIGHTNESS 20 /* percent: TIM21 switches the colour line at 1 kHz (owner: dimmer) */
#endif

static volatile bool s_any_lit;

/* 1 kHz: colour on at the period start (if an indicator is lit), off at the compare. */
void TIM21_IRQHandler(void) {
    uint32_t sr = TIM21->SR;
    TIM21->SR = ~sr;
    if ((sr & TIM_SR_UIF) && s_any_lit) GPIOB->BSRR = LED_COLOUR;
    if (sr & TIM_SR_CC1IF) GPIOB->BRR = LED_COLOUR;
}

void board_leds_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    GPIOB->BRR = LED_SELECT | LED_COLOUR_ALL;
    for (uint32_t pin = 3; pin <= 9; pin++) {
        GPIOB->MODER = (GPIOB->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
        GPIOB->OTYPER &= ~(1U << pin);
        GPIOB->PUPDR &= ~(3U << (pin * 2));
    }
#if R3S_LED_BRIGHTNESS < 100
    RCC->APB2ENR |= RCC_APB2ENR_TIM21EN;
    TIM21->PSC = SystemCoreClock / 1000000U - 1U; /* 1 MHz */
    TIM21->ARR = 999U;                            /* 1 kHz */
    TIM21->CCR1 = R3S_LED_BRIGHTNESS * 10U;
    TIM21->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;
    TIM21->EGR = TIM_EGR_UG;
    TIM21->SR = 0;
    NVIC_SetPriority(TIM21_IRQn, (1U << __NVIC_PRIO_BITS) - 1U);
    NVIC_EnableIRQ(TIM21_IRQn);
    TIM21->CR1 = TIM_CR1_CEN;
#endif
}

void board_leds_off(void) {
    s_any_lit = false;
    GPIOB->BRR = LED_SELECT | LED_COLOUR_ALL;
}

#ifdef R3S_LED_COLOUR_TEST
#include "FreeRTOS.h"
#include "task.h"
/* Test build: Num on PB7, Caps on PB8, Scroll on PB9, one per second, to name the colour lines. */
void board_leds_write(bool lit0, bool lit1, uint8_t host_leds) {
    (void)lit0;
    (void)lit1;
    (void)host_leds;
    static const uint32_t sel[3] = {LED_NUM, LED_CAPS, LED_SCROLL};
    uint32_t phase = (xTaskGetTickCount() / pdMS_TO_TICKS(1000)) % 3;
    uint32_t on = sel[phase] | (1U << (7 + phase));
    GPIOB->BSRR = on | (((LED_SELECT | LED_COLOUR_ALL) & ~on) << 16);
}
#else
void board_leds_write(bool lit0, bool lit1, uint8_t host_leds) {
    (void)lit0;
    uint32_t on = ((host_leds & 1U) ? LED_NUM : 0) | ((host_leds & 2U) ? LED_CAPS : 0) |
                  ((host_leds & 4U) ? LED_SCROLL : 0) | (lit1 ? LED_SIMPLE : 0);
    s_any_lit = on != 0;
#if R3S_LED_BRIGHTNESS >= 100
    if (on) on |= LED_COLOUR;
    GPIOB->BSRR = on | (((LED_SELECT | LED_COLOUR_ALL) & ~on) << 16);
#else
    /* Select lines only; TIM21 drives the colour line (off at once when nothing is lit). */
    GPIOB->BSRR = on | ((LED_SELECT & ~on) << 16) | (on ? 0 : LED_COLOUR_ALL << 16);
#endif
}
#endif
