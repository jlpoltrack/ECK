/* Hybrid idle stage on batteries: FreeRTOS tickless idle in STOP between scans. LPTIM1 on LSI
 * keeps FreeRTOS time while SysTick and HSI16 are stopped (no 32 kHz crystal on this board). */
#include "hybrid_sleep.h"

#ifdef HHKB_HYBRID
#include "FreeRTOS.h"
#include "hybrid.h"
#include "stm32l0xx.h"
#include "task.h"

#define MAX_SLEEP_MS 20000U /* the 16-bit counter wraps after ~28 s at LSI / 16 */

static volatile bool s_idle;
static uint32_t s_counts_per_s; /* LSI / 16, measured against SysTick at start-up */
static uint32_t s_carry_us;     /* sub-tick time not yet given to FreeRTOS */

/* The counter runs on LSI, asynchronous to the bus: read until two reads agree. */
static uint16_t lptim_count(void) {
    uint16_t a, b = (uint16_t)LPTIM1->CNT;
    do {
        a = b;
        b = (uint16_t)LPTIM1->CNT;
    } while (a != b);
    return a;
}

/* Set the wake compare; avoids CMP == ARR (0xFFFF), where RM0367 leaves the match unclear. */
static void lptim_wake_at(uint16_t cmp) {
    LPTIM1->ICR = LPTIM_ICR_CMPOKCF | LPTIM_ICR_CMPMCF;
    LPTIM1->CMP = cmp == 0xFFFFU ? 0U : cmp;
    while (!(LPTIM1->ISR & LPTIM_ISR_CMPOK)) {}
    LPTIM1->ICR = LPTIM_ICR_CMPMCF;
}

void LPTIM1_IRQHandler(void) {
    LPTIM1->ICR = LPTIM_ICR_CMPMCF;
}

void hybrid_sleep_init(void) {
    if (!hybrid_on_battery()) return;
    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) {}
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_LPTIM1SEL) | RCC_CCIPR_LPTIM1SEL_0; /* LSI */
    RCC->APB1ENR |= RCC_APB1ENR_LPTIM1EN | RCC_APB1ENR_PWREN;
    LPTIM1->CFGR = 4U << LPTIM_CFGR_PRESC_Pos; /* / 16 */
    LPTIM1->IER = LPTIM_IER_CMPMIE;            /* both only while disabled */
    LPTIM1->CR = LPTIM_CR_ENABLE;
    LPTIM1->ARR = 0xFFFFU;
    while (!(LPTIM1->ISR & LPTIM_ISR_ARROK)) {}
    LPTIM1->CR = LPTIM_CR_ENABLE | LPTIM_CR_CNTSTRT;
    EXTI->IMR |= 1U << 29; /* LPTIM1 wakes the core from STOP */
    NVIC_EnableIRQ(LPTIM1_IRQn);
    uint16_t c0 = lptim_count();
    vTaskDelay(pdMS_TO_TICKS(250));
    s_counts_per_s = (uint16_t)(lptim_count() - c0) * 4U;
}

bool hybrid_sleep_wake_after(uint32_t ms) {
    if (!s_counts_per_s) return false;
    uint32_t wait = ms * s_counts_per_s / 1000U;
    lptim_wake_at((uint16_t)(lptim_count() + (wait ? wait : 1U)));
    return true;
}

bool hybrid_sleep_timer_fired(void) {
    if (!(LPTIM1->ISR & LPTIM_ISR_CMPM)) return false;
    LPTIM1->ICR = LPTIM_ICR_CMPMCF;
    NVIC_ClearPendingIRQ(LPTIM1_IRQn);
    return true;
}

void hybrid_sleep_set_idle(bool idle) {
    s_idle = idle && s_counts_per_s != 0;
}

void hybrid_sleep(uint32_t expected) {
    if (!s_idle) return; /* the idle hook's wfi */
    if (expected > MAX_SLEEP_MS) expected = MAX_SLEEP_MS;
    __disable_irq();
    if (eTaskConfirmSleepModeStatus() == eAbortSleep) {
        __enable_irq();
        return;
    }
    uint32_t since_tick_us = (SysTick->LOAD - SysTick->VAL) / (SystemCoreClock / 1000000U);
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    uint16_t start = lptim_count();
    uint32_t wait = expected * s_counts_per_s / 1000U;
    lptim_wake_at((uint16_t)(start + (wait > 1U ? wait - 1U : 1U)));
    /* STOP on the low-power regulator; ULP stays 0 so VREFINT (needed by the ADC) is ready. */
    PWR->CR = (PWR->CR & ~(PWR_CR_PDDS | PWR_CR_ULP)) | PWR_CR_LPSDSR;
    RCC->CFGR |= RCC_CFGR_STOPWUCK; /* wake on HSI16, the battery-mode clock */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __ISB();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    /* count * 1e6 / cps in 32 bits, exactly: 1e6 = 15625 * 64, count < 2^16, cps <= 262140. */
    uint32_t q = (uint32_t)(uint16_t)(lptim_count() - start) * 15625U;
    uint32_t us = (q / s_counts_per_s) * 64U + (q % s_counts_per_s) * 64U / s_counts_per_s +
                  since_tick_us + s_carry_us;
    TickType_t ticks = us / 1000U;
    s_carry_us = us % 1000U;
    if (ticks > expected) {
        s_carry_us += (ticks - expected) * 1000U;
        ticks = expected;
    }
    vTaskStepTick(ticks);
    SysTick->VAL = 0;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    __enable_irq();
}
#endif
