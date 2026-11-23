/* HHKB Hybrid power handling. Stock behaviour: docs/hybrid-stock-behaviour.md ("Power, battery
 * and LED"); user-visible rules: PFU manual P3PC-6641-01EN; summary in docs/hybrid-port.md. */
#ifdef HHKB_HYBRID
#include "hybrid_power.h"

#include <string.h>

#include "FreeRTOS.h"
#include "ble_link.h"
#include "dip_switch.h"
#include "events.h"
#include "hybrid_ble.h"
#include "hybrid_power_logic.h"
#include "hybrid_sleep.h"
#include "matrix_scan.h"
#include "stm32l0xx.h"
#include "task.h"

_Static_assert(configTICK_RATE_HZ == 1000, "power timing counts 1 ms ticks");

#define SW_PIN 5U      /* PC5: power switch, low = pressed */
#define USB_PIN 13U    /* PC13: low = USB power present */
#define LED_BLUE 8U    /* PA8 */
#define LED_ORANGE 9U  /* PA9 */
#define STEP_MS 100U   /* stock battery and LED period */
#define SW_POLL_MS 10U /* tick period while the switch is down */
#define SW_DEBOUNCE 2U
#define BOOT_SAMPLES 11U /* stock power-on battery check */
#define OFF_ORANGE_MS 1000U
#define EMPTY_ORANGE_MS 2000U
#define NRF_OFF_WAIT_MS 5000U /* the link waits up to 2 s for a booting nRF52, twice if needed */
#define SLOT_BLINK_HALF 3U  /* 0.3 s phases */
#define USB_FLASH_STEPS 10U /* 1 s */
#define WAKE_RELEASE_MS 20U

typedef enum { ST_BOOT, ST_ON, ST_OFF } state_t;

static state_t s_state;
static bool s_init;
static TickType_t s_step_at, s_off_at, s_key_at, s_pairing_at, s_standby_at;
static uint16_t s_off_orange_ms;
static bool s_cold;      /* power-on reset (batteries in), not our own restart */
static bool s_quiet_off; /* off without the orange LED */
static bool s_key_wake;  /* automatic deep sleep: any key wakes (not after a switch hold) */
static bool s_boot_usb;  /* clocks and USB were set up for this power source */
static bool s_power_saving; /* SW6 as read at start-up; on batteries the DIP pins are released */
static TickType_t s_usb_changed_at;
static bool s_nrf_queued, s_was_pairing, s_was_standby;
static uint8_t s_boot_samples;
static uint8_t s_percent = 0xFF;
static pwr_batt_filter_t s_batt;
static pwr_debounce_t s_sw;
static bool s_sw_raw;
static pwr_switch_t s_switch;
static pwr_warn_t s_warn;
static pwr_blink_t s_blue, s_orange;
static bool s_warn_on, s_blue_on, s_orange_on;

/* PC5 pull-up (switch to ground; stock pull unknown); PC13 and the PB7-PB9 detector outputs
 * without pulls (stock configuration not found). */
static void pins_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    GPIOC->MODER &= ~((3U << (2 * SW_PIN)) | (3U << (2 * USB_PIN)));
    GPIOC->PUPDR = (GPIOC->PUPDR & ~((3U << (2 * SW_PIN)) | (3U << (2 * USB_PIN)))) | (1U << (2 * SW_PIN));
    GPIOB->MODER &= ~((3U << (2 * 7)) | (3U << (2 * 8)) | (3U << (2 * 9)));
    GPIOB->PUPDR &= ~((3U << (2 * 7)) | (3U << (2 * 8)) | (3U << (2 * 9)));
}

static bool usb_power(void) {
    return !(GPIOC->IDR & (1U << USB_PIN));
}

static bool switch_raw(void) {
    return !(GPIOC->IDR & (1U << SW_PIN));
}

static uint8_t batt_raw(void) {
    uint32_t idr = GPIOB->IDR;
    return pwr_batt_raw_level(!(idr & (1U << 7)), !(idr & (1U << 8)), !(idr & (1U << 9)));
}

void hybrid_power_confirm_forget(void) {
    pwr_blink_start(&s_orange, 3, SLOT_BLINK_HALF);
}

void hybrid_power_host_rejected(void) {
    pwr_blink_start(&s_orange, 5, 2U); /* 0.2 s phases: unlike the battery and forget patterns */
}

/* ---- OFF state: STOP mode until the switch is held 1 s or USB power appears, then reset ---- */

static void usb_stop(void) {
    NVIC_DisableIRQ(USB_IRQn);
    if (RCC->APB1ENR & RCC_APB1ENR_USBEN) {
        USB->BCDR &= ~USB_BCDR_DPPU; /* the host sees the keyboard unplugged */
        USB->CNTR = USB_CNTR_FRES | USB_CNTR_PDWN;
    }
    RCC->APB1ENR &= ~(RCC_APB1ENR_USBEN | RCC_APB1ENR_CRSEN);
    RCC->CRRCR &= ~RCC_CRRCR_HSI48ON; /* USB, HSI48 and CRS: 1.4 mA on batteries (measured) */
    SYSCFG->CFGR3 &= ~SYSCFG_CFGR3_ENREF_HSI48;
}

/* DIP switches analog: an ON switch costs ~0.1 mA through its pull-up (3 ON: 0.31 mA measured). */
static void dip_pins_release(void) {
    GPIOA->PUPDR &= ~(0x3FFU << (2 * 3)); /* SW1-SW5: PA3-PA7 */
    GPIOA->MODER |= 0x3FFU << (2 * 3);
    GPIOC->PUPDR &= ~(3U << (2 * 4)); /* SW6: PC4 */
    GPIOC->MODER |= 3U << (2 * 4);
}

/* Sensor supply (PC14), op-amp, discharge, mux and row lines low (stock idle levels not known);
 * DIP switches, battery inputs and SPI data lines analog so no pull-up current flows. */
static void pins_idle(void) {
    if (RCC->APB2ENR & RCC_APB2ENR_ADC1EN) {
        if (ADC1->CR & ADC_CR_ADEN) {
            ADC1->CR |= ADC_CR_ADDIS;
            while (ADC1->CR & ADC_CR_ADEN) {}
        }
        ADC1->CR &= ~ADC_CR_ADVREGEN;
        RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;
    }
    GPIOC->BRR = (1U << 14) | (1U << 6) | (1U << 7) | (1U << 8);
    GPIOB->BRR = HHKB_ROW_MASK | (0xFU << 12);
    SPI1->CR1 = 0;
    RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;
    /* CS (PA15) stays high against the nRF52's pull-up. */
    const uint32_t pb = (3U << (2 * 3)) | (3U << (2 * 4)) | (3U << (2 * 5)) | (3U << (2 * 7)) | (3U << (2 * 8)) |
                        (3U << (2 * 9));
    GPIOB->PUPDR &= ~pb;
    GPIOB->MODER |= pb;
    dip_pins_release();
}

/* After a wake the clock is HSI16 (STOPWUCK): time the switch with polled 1 ms SysTick periods. */
static bool wake_hold(void) {
    SysTick->LOAD = 16000U - 1U;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    pwr_debounce_t d = {.state = true};
    uint32_t held = 0;
    for (;;) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {}
        bool pressed = pwr_debounce_update(&d, switch_raw(), WAKE_RELEASE_MS);
        if (pressed) held++;
        switch (pwr_wake_action(pressed, held)) {
        case PWR_WAKE_BLUE:
            GPIOA->BSRR = 1U << LED_BLUE;
            break;
        case PWR_WAKE_ON:
            return true;
        case PWR_WAKE_SLEEP:
            SysTick->CTRL = 0;
            return false;
        default:
            break;
        }
    }
}

#define KEY_WAKE_MS 125U    /* deep sleep: scan 8 times a second */
#define SENSOR_SETTLE_US 1000U

/* Deep sleep: scan once with the sensor supply left on (recharging it costs ~25 uC per check)
 * and the pins as the scan leaves them: rows deselected, op-amp and ADC off.
 * True when a key went down since the previous scan (keys held at sleep never count). */
static bool key_wake_scan(matrix_row_t prev[MATRIX_ROWS]) {
    matrix_row_t cur[MATRIX_ROWS];
    memcpy(cur, prev, sizeof(cur));
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)hhkb_matrix_scan_raw(cur);
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;
    bool pressed = false;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (cur[r] & ~prev[r]) pressed = true;
        prev[r] = cur[r];
    }
    return pressed;
}

/* Never returns: power-on is a reset, which also restarts the nRF52 (hybrid_ble.c link_boot). */
__attribute__((noreturn)) static void enter_off(void) {
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    GPIOA->BRR = (1U << LED_BLUE) | (1U << LED_ORANGE);
    usb_stop();
    pins_idle();

    bool key_wake = s_key_wake && hybrid_sleep_wake_after(KEY_WAKE_MS);
    const uint32_t lines = (1U << SW_PIN) | (1U << USB_PIN);
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~SYSCFG_EXTICR2_EXTI5) | SYSCFG_EXTICR2_EXTI5_PC;
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~SYSCFG_EXTICR4_EXTI13) | SYSCFG_EXTICR4_EXTI13_PC;
    EXTI->EMR = 0;
    EXTI->RTSR = 0;
    EXTI->FTSR = lines;
    EXTI->IMR = lines | (key_wake ? 1U << 29 : 0U); /* 29: LPTIM1 */
    NVIC_EnableIRQ(EXTI4_15_IRQn); /* wakes WFI while PRIMASK keeps the handler from running */
    if (key_wake) NVIC_EnableIRQ(LPTIM1_IRQn);
    matrix_row_t keys[MATRIX_ROWS] = {0};
    if (key_wake) {
        GPIOB->BSRR = HHKB_ROW_MASK;    /* rows deselected */
        GPIOC->BSRR = 1U << 14;                                         /* sensor supply */
        uint16_t t0 = (uint16_t)TIM2->CNT;
        while ((uint16_t)(TIM2->CNT - t0) < SENSOR_SETTLE_US * HHKB_SCAN_TIMER_TICKS_PER_US) {}
        (void)key_wake_scan(keys); /* what is held now */
    }

    DBGMCU->CR = 0; /* debug in STOP keeps clocks running; set by bench tools, survives resets */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    RCC->CFGR |= RCC_CFGR_STOPWUCK;
    /* ULP stops VREFINT in STOP; key wake keeps it for the ADC (~2 uA). */
    PWR->CR = (PWR->CR & ~(PWR_CR_PDDS | PWR_CR_ULP)) | PWR_CR_LPSDSR | PWR_CR_FWU | (key_wake ? 0U : PWR_CR_ULP);
    bool usb_was = usb_power(); /* off on USB power stays off until USB power comes back */
    for (;;) {
        /* Manual p.7: power from a USB cable turns the keyboard on (level, not only the edge). */
        if (usb_power()) {
            if (!usb_was) NVIC_SystemReset();
        } else {
            usb_was = false;
        }
        EXTI->PR = lines;
        NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
        PWR->CR |= PWR_CR_CWUF;
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI();
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
        if ((EXTI->PR & (1U << USB_PIN)) && usb_power()) NVIC_SystemReset();
        if (key_wake && hybrid_sleep_timer_fired()) {
            /* A new key must still be down in a second scan: one noisy reading does not wake. */
            matrix_row_t before[MATRIX_ROWS], first[MATRIX_ROWS];
            memcpy(before, keys, sizeof(before));
            if (key_wake_scan(keys)) {
                memcpy(first, keys, sizeof(first));
                (void)key_wake_scan(keys);
                for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                    if (first[r] & ~before[r] & keys[r]) NVIC_SystemReset(); /* on, with the 1 s blue */
                }
            }
            (void)hybrid_sleep_wake_after(KEY_WAKE_MS);
            if (!(EXTI->PR & (1U << SW_PIN))) continue;
        }
        if (wake_hold()) NVIC_SystemReset();
        GPIOA->BRR = 1U << LED_BLUE;
    }
}

/* ---- Normal operation ---- */

static void off_begin(TickType_t now, uint16_t orange_ms) {
    s_state = ST_OFF;
    s_off_at = now;
    s_off_orange_ms = orange_ms;
    s_nrf_queued = hybrid_ble_power_off();
}

/* Manual p.12: a light press blinks the Bluetooth slot number, or lights 1 s on USB. */
void hybrid_power_show_slot(void) {
    if (hybrid_ble_transport_active()) {
        pwr_blink_start(&s_blue, hybrid_ble_slot(), SLOT_BLINK_HALF);
    } else {
        pwr_blink_start(&s_blue, 1, USB_FLASH_STEPS);
    }
}

static void step(TickType_t now, bool usb) {
    uint8_t level = pwr_batt_filter_update(&s_batt, batt_raw());
    uint8_t percent = pwr_batt_percent(level, usb);
    if (percent != s_percent) {
        s_percent = percent;
        (void)hybrid_ble_battery(percent); /* only levels are known (PB7-PB9), no voltage */
    }
    if (s_state == ST_BOOT && ++s_boot_samples >= BOOT_SAMPLES) {
        if (!usb && level == PWR_BATT_EMPTY) {
            off_begin(now, EMPTY_ORANGE_MS); /* stock: empty on batteries refuses to start */
        } else if (!usb && s_cold && !s_sw_raw) { /* a switch held through the check turns it on */
            s_quiet_off = true; /* stock (tested): batteries in -> off until the switch is held */
            off_begin(now, 0);
        } else {
            s_state = ST_ON;
        }
    }
    s_warn_on = pwr_warn_step(&s_warn, level, s_state == ST_ON && !usb && hybrid_ble_transport_active());
    s_blue_on = pwr_blink_step(&s_blue);
    s_orange_on = pwr_blink_step(&s_orange);
}

static void auto_off_check(TickType_t now, bool usb) {
    uint8_t flags = hybrid_ble_flags();
    bool pairing = (flags & (BLE_LINK_ST_PAIRING | BLE_LINK_ST_PASSKEY)) != 0;
    bool standby = hybrid_ble_transport_active() && !(flags & BLE_LINK_ST_CONNECTED) &&
                   (flags & BLE_LINK_ST_ADVERTISING) && !pairing;
    if (pairing != s_was_pairing || usb) s_pairing_at = now;
    if (standby != s_was_standby || usb) s_standby_at = now;
    if (usb) s_key_at = now; /* time on USB power never counts towards an auto power-off */
    s_was_pairing = pairing;
    s_was_standby = standby;
    pwr_auto_t a = {
        .usb_power = usb,
        .power_saving = s_power_saving,
        .pairing = pairing,
        .standby = standby,
        .idle_ms = now - s_key_at,
        .pairing_ms = now - s_pairing_at,
        .standby_ms = now - s_standby_at,
    };
    if (pwr_auto_off_due(&a)) {
        s_key_wake = true; /* deep sleep: quiet, and any key wakes it */
        s_quiet_off = true;
        off_begin(now, 0);
    }
}

void hybrid_power_tick(TickType_t now) {
    if (!s_init) {
        s_init = true;
        pins_init();
        /* Power-on reset only; our power-on is a software reset (flags cleared for the next boot). */
        s_cold = (RCC->CSR & RCC_CSR_PORRSTF) && !(RCC->CSR & RCC_CSR_SFTRSTF);
        s_boot_usb = usb_power();
        s_power_saving = hhkb_is_power_saving_enabled();
        if (!s_boot_usb) {
            usb_stop();
            dip_pins_release(); /* all six were read once at start-up */
        }
        RCC->CSR |= RCC_CSR_RMVF;
        s_step_at = s_key_at = s_pairing_at = s_standby_at = now;
        pwr_batt_filter_init(&s_batt, batt_raw());
        /* A press held through boot (power-on, recovery) does nothing until released. */
        s_sw.state = s_switch.pressed = s_switch.fired = switch_raw();
    }
    s_sw_raw = switch_raw();
    bool pressed = pwr_debounce_update(&s_sw, s_sw_raw, SW_DEBOUNCE);
    bool usb = usb_power();
    /* USB power came or went: restart so clocks and USB match it (keeps the switch state). */
    if (usb == s_boot_usb) {
        s_usb_changed_at = now;
    } else if (s_state == ST_ON && (TickType_t)(now - s_usb_changed_at) >= pdMS_TO_TICKS(300)) {
        NVIC_SystemReset();
    }
    while ((TickType_t)(now - s_step_at) >= pdMS_TO_TICKS(STEP_MS)) {
        s_step_at += pdMS_TO_TICKS(STEP_MS);
        step(now, usb);
    }
    if (s_state != ST_OFF) {
        pwr_sw_event_t ev = pwr_switch_update(&s_switch, pressed, now);
        if (ev == PWR_SW_SHORT && s_state == ST_ON) hybrid_power_show_slot();
        if (ev == PWR_SW_OFF) {
            off_begin(now, OFF_ORANGE_MS);
        } else if (s_state == ST_ON) {
            auto_off_check(now, usb);
        }
    }
    if (s_state == ST_OFF) {
        if (!s_nrf_queued) s_nrf_queued = hybrid_ble_power_off();
        TickType_t elapsed = now - s_off_at;
        /* Manual p.4: orange while it turns off, then dark. */
        if (!pressed && elapsed >= s_off_orange_ms && (hybrid_ble_power_off_done() || elapsed >= NRF_OFF_WAIT_MS)) {
            enter_off();
        }
    }
}

TickType_t hybrid_power_timeout(TickType_t now) {
    if (s_sw_raw || s_sw.state || s_state == ST_OFF) return pdMS_TO_TICKS(SW_POLL_MS);
    TickType_t since = now - s_step_at;
    return since >= pdMS_TO_TICKS(STEP_MS) ? 1 : pdMS_TO_TICKS(STEP_MS) - since;
}

void hybrid_power_leds(bool *blue, bool *orange) {
    if (s_state == ST_OFF) {
        *orange = !s_quiet_off;
    } else {
        if (pwr_blink_busy(&s_blue)) *blue = s_blue_on;
        /* nRF52 update in progress: orange 2 blinks/s */
        if (hybrid_ble_update_active()) *orange = (xTaskGetTickCount() / pdMS_TO_TICKS(250)) & 1U;
        if (pwr_blink_busy(&s_warn.blink)) *orange = s_warn_on;
        if (pwr_blink_busy(&s_orange)) *orange = s_orange_on;
    }
    if (*orange) *blue = false; /* stock LED driver: one colour at a time, orange first */
}

bool hybrid_power_matrix_change(uint8_t row, uint8_t col, bool pressed) {
    (void)row;
    (void)col;
    if (pressed) s_key_at = xTaskGetTickCount();
    return false;
}
#endif
