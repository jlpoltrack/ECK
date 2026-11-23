#include "matrix_scan.h"
#include "ec_settings.h"
#include "rapid_trigger.h"
#include "stm32l0xx.h"
#include "stm32l0xx_ll_adc.h"
#include "FreeRTOS.h"
#include "task.h"
#ifdef HHKB_HYBRID
#include "hybrid.h"
#endif
#ifndef HHKB_OPAMP_SETTLE_US
#define HHKB_OPAMP_SETTLE_US 20U
#endif
#ifndef HHKB_MUX_SETTLE_US
#define HHKB_MUX_SETTLE_US 50U // after the second mux half switches on (c8r0)
#endif
/* Last reading per key: lets calibration and debug output run after each masked half. */
static uint16_t s_raw[MATRIX_COLS][MATRIX_ROWS];
#ifdef HHKB_ADC_DIAGNOSTICS
static volatile uint16_t s_masked_max_us; /* longest scan half with interrupts held off */
#endif

/* PFU's per-key press threshold (+ offset): used while custom calibration and Rapid Trigger
 * are off, and the key has a valid one. */
#define HHKB_FACTORY_HYSTERESIS 50
static uint16_t get_factory_threshold(uint8_t col, uint8_t row) {
    const uint16_t *press = (const uint16_t *)EEPROM_FACTORY_PRESS_ADDR;
    const int16_t *offset = (const int16_t *)EEPROM_FACTORY_OFFSET_ADDR;
    uint8_t idx = (col << 3) | (row & 7);
    int32_t threshold = (int32_t)press[idx] + (int32_t)offset[idx];
    return (threshold > HHKB_FACTORY_HYSTERESIS && threshold <= 4095) ? (uint16_t)threshold : 0;
}

static inline void delay_us_nop(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 6; i++) {
        __NOP();
    }
}

/* TIM2 runs from matrix_task's start on; hhkb_matrix_scan_raw checks it once per scan. */
static inline void scan_wait_since(uint16_t start, uint16_t us) {
    uint16_t ticks = us * HHKB_SCAN_TIMER_TICKS_PER_US;
    while ((uint16_t)(TIM2->CNT - start) < ticks) {}
}
#define scan_delay_us(us) scan_wait_since((uint16_t)TIM2->CNT, (us))

static inline uint16_t adc_read_sample(void) {
    uint32_t timeout = 5000;
    while ((ADC1->CR & ADC_CR_ADSTP) && --timeout);
    ADC1->ISR = ADC_ISR_EOC; /* a timed-out conversion may finish later: don't take it as this one */
    ADC1->CR |= ADC_CR_ADSTART;
    timeout = 5000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --timeout);
    if (!timeout) return ADC_TIMED_OUT;
    return (uint16_t)(ADC1->DR & 0x0FFF);
}

void hhkb_matrix_hardware_init(void) {
#ifdef HHKB_HYBRID
    hybrid_hardware_init();
#endif
    // Enable GPIO clocks
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN;

    // Row drive pins (B0, B1, B2, B10) & Demux pins (B12, B13, B14, B15) as Push-Pull Output
    GPIOB->MODER &= ~(
        (3U << (0 * 2)) | (3U << (1 * 2)) | (3U << (2 * 2)) | (3U << (10 * 2)) |
        (3U << (12 * 2)) | (3U << (13 * 2)) | (3U << (14 * 2)) | (3U << (15 * 2))
    );
    GPIOB->MODER |= (
        (1U << (0 * 2)) | (1U << (1 * 2)) | (1U << (2 * 2)) | (1U << (10 * 2)) |
        (1U << (12 * 2)) | (1U << (13 * 2)) | (1U << (14 * 2)) | (1U << (15 * 2))
    );
    GPIOB->OSPEEDR |= (
        (3U << (0 * 2)) | (3U << (1 * 2)) | (3U << (2 * 2)) | (3U << (10 * 2)) |
        (3U << (12 * 2)) | (3U << (13 * 2)) | (3U << (14 * 2)) | (3U << (15 * 2))
    );

    // Row drive pins idle HIGH (unselected)
    GPIOB->BSRR = 0x0407;

    // Analog control: PC6, PC7, PC8 and sensor board power PC14 as Push-Pull Output
    GPIOC->MODER &= ~((3U << (6 * 2)) | (3U << (7 * 2)) | (3U << (8 * 2)) | (3U << (14 * 2)));
    GPIOC->MODER |=  ((1U << (6 * 2)) | (1U << (7 * 2)) | (1U << (8 * 2)) | (1U << (14 * 2)));
    GPIOC->OSPEEDR |= ((3U << (6 * 2)) | (3U << (7 * 2)) | (3U << (8 * 2)) | (3U << (14 * 2)));
    GPIOC->BRR = (1U << 6) | (1U << 7) | (1U << 8);
    GPIOC->BSRR = (1U << 14); // Enable power for sensor board

    // ADC input: PA1 (IN1) as Analog
    GPIOA->MODER |= (3U << (1 * 2));

    // Enable ADC clock
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1RST;

    ADC1->CR |= ADC_CR_ADVREGEN;
    delay_us_nop(2000); // Restore conservative ADC regulator startup delay

    // Calibrate ADC
    ADC1->CR |= ADC_CR_ADCAL;
    uint32_t cal_timeout = 100000;
    while ((ADC1->CR & ADC_CR_ADCAL) && --cal_timeout);

    ADC1->CFGR1 = 0;
    // 16 MHz synchronous ADC clock at 32 or 16 MHz HCLK.
    ADC1->CFGR2 = SystemCoreClock < 32000000U ? LL_ADC_CLOCK_SYNC_PCLK_DIV1 : LL_ADC_CLOCK_SYNC_PCLK_DIV2;
    ADC1->CHSELR = ADC_CHSELR_CHSEL1;
    ADC1->SMPR = 0; // 1.5 ADC clock cycles

    // Enable ADC
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    uint32_t rdy_timeout = 100000;
    while (!(ADC1->ISR & ADC_ISR_ADRDY) && --rdy_timeout);

    // Initialize calibration, thresholds and rapid-trigger state before scanning.
    hhkb_calibration_load();
}

/* Calibration (rest totals, peaks) and debug streaming for cols [first, end), run once the
 * half's readings are taken: printing re-enables interrupts, and any per-key work inside the
 * read loop would lengthen the gap before the next key's read and shift its reading. */
static void report_half(uint8_t first, uint8_t end, const matrix_row_t *matrix, bool rest, bool peaks,
                        bool debug) {
    if (!rest && !peaks && !debug) return;
    for (uint8_t col = first; col < end; col++) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            uint16_t val = s_raw[col][row];
            if (rest) hhkb_cal_accumulate_baseline(col, row, val);
            if (peaks) hhkb_cal_record_peak(col, row, val);
            if (debug) hhkb_debug_feed_sample(col, row, val, rt_normalize(&s_rt_keys[col][row], val),
                                              (matrix[row] & (1U << col)) != 0);
        }
    }
}

__attribute__((section(".RamFunc"), optimize("O2")))
bool hhkb_matrix_scan_raw(matrix_row_t current_matrix[]) {
    if (!(RCC->APB1ENR & RCC_APB1ENR_TIM2EN)) return false; // the settle waits need TIM2
    hhkb_record_matrix_scan();
#ifdef HHKB_HYBRID
    // On batteries the ADC is off between scans (~1 mA); calibration survives ADEN/ADVREGEN = 0.
    bool adc_cycle = hybrid_on_battery();
    if (adc_cycle && !(ADC1->CR & ADC_CR_ADEN)) { // ADRDY only rises when ADEN goes 0 -> 1
        while (PWR->CSR & PWR_CSR_REGLPF) {} // RM0367 14.3.2: main regulator back after STOP
        ADC1->CR |= ADC_CR_ADVREGEN;
        scan_delay_us(20);
        ADC1->ISR = ADC_ISR_ADRDY;
        ADC1->CR |= ADC_CR_ADEN;
        while (!(ADC1->ISR & ADC_ISR_ADRDY));
    }
#endif
    bool changed = false;
    static const uint16_t row_pins[MATRIX_ROWS] = {0x1, 0x2, 0x4, 0x400};
    static const uint32_t col_mux_bsrr[MATRIX_COLS] = {
        0x70000000, 0x60001000, 0x50002000, 0x40003000,
        0x30004000, 0x20005000, 0x10006000, 0x7000,
        0x70000000, 0x60001000, 0x50002000, 0x40003000,
        0x30004000, 0x20005000, 0x10006000
    };

    GPIOB->BSRR = 0x0407;    // Deselect all rows
    GPIOC->BSRR = (1U << 7); // Enable OpAmp

    GPIOC->BSRR = (1U << 6);
    GPIOB->BRR  = (1U << 15);
    scan_delay_us(HHKB_OPAMP_SETTLE_US); // else the first key (c0r0) reads noisy (Hybrid, measured)

    bool capture = hhkb_cal_is_capturing_baseline();
    bool is_calibrating = hhkb_is_calibrating();
    rt_cfg_t scan_cfg = hhkb_get_scan_cfg();
    bool debug_topre = hhkb_get_debug_flag(DEBUG_FLAG_TOPRE);
    bool record_peaks = is_calibrating && !capture;
    bool factory = !scan_cfg.rt_enabled && !hhkb_calibration_enabled(); /* factory thresholds */

    // Each reading depends on the timing since the previous key: an interrupt inside a scan
    // skews readings by up to ~200 counts (measured), so each mux half runs with interrupts off.
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (primask) record_peaks = debug_topre = false; // entered masked (deep-sleep key check): no printing
#ifdef HHKB_ADC_DIAGNOSTICS
    uint16_t masked_start = (uint16_t)TIM2->CNT;
#endif

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        rt_key_t *col_keys = s_rt_keys[col];

        if (col == 8) {
#ifdef HHKB_ADC_DIAGNOSTICS
            uint16_t half_us = (uint16_t)((uint16_t)TIM2->CNT - masked_start) / HHKB_SCAN_TIMER_TICKS_PER_US;
            if (half_us > s_masked_max_us) s_masked_max_us = half_us;
#endif
            __set_PRIMASK(primask);
            report_half(0, 8, current_matrix, capture, record_peaks, debug_topre);
            __disable_irq();
#ifdef HHKB_ADC_DIAGNOSTICS
            masked_start = (uint16_t)TIM2->CNT;
#endif
            GPIOC->BRR  = (1U << 6);
            GPIOB->BSRR = (1U << 15);
            scan_delay_us(HHKB_MUX_SETTLE_US); // as for c0r0: a long, so stable, gap before c8r0
        }

        GPIOB->BSRR = col_mux_bsrr[col];
        uint16_t settle_start = (uint16_t)TIM2->CNT;

        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            matrix_row_t prev_row = current_matrix[row];

            scan_wait_since(settle_start, 2);

            // Discharge capacitor C57
            GPIOC->BSRR = (1U << 8);
            scan_delay_us(2);
            GPIOC->BRR  = (1U << 8);

            // Drive row strobe active (LOW)
            GPIOB->BRR = row_pins[row];
            scan_delay_us(3);

            uint16_t val = adc_read_sample();
            GPIOB->BSRR = row_pins[row];
            settle_start = (uint16_t)TIM2->CNT;
            if (val == ADC_TIMED_OUT) continue; /* key keeps its state and last reading */
            s_raw[col][row] = val;

            bool is_pressed;
            rt_key_t *key = &col_keys[row];
            uint16_t threshold = factory ? get_factory_threshold(col, row) : 0;
            if (threshold != 0) {
                if (prev_row & (1U << col)) threshold -= HHKB_FACTORY_HYSTERESIS;
                is_pressed = val > threshold;
                key->ext = 0; // rt_key_reset, inline: it runs from flash
                key->down = false;
                key->latched = false;
            } else {
                if (val <= key->reset_raw) { // reset zone of rt_update: same result, shorter path
                    key->ext = 0;
                    key->down = false;
                    key->latched = false;
                    is_pressed = false;
                } else {
                    is_pressed = rt_update(key, &scan_cfg, val);
                }
            }

            if (is_pressed) current_matrix[row] |= (1U << col);
            else current_matrix[row] &= ~(1U << col);

            if (current_matrix[row] != prev_row) {
                changed = true;
            }
        }
    }

#ifdef HHKB_ADC_DIAGNOSTICS
    uint16_t masked_us = (uint16_t)((uint16_t)TIM2->CNT - masked_start) / HHKB_SCAN_TIMER_TICKS_PER_US;
    if (masked_us > s_masked_max_us) s_masked_max_us = masked_us;
#endif
    __set_PRIMASK(primask);
    report_half(8, MATRIX_COLS, current_matrix, capture, record_peaks, debug_topre);

    if (capture) {
        hhkb_cal_decrement_samples();
    }

    // Disable OpAmp
    GPIOC->BRR = (1U << 7);
#ifdef HHKB_HYBRID
    if (adc_cycle) {
        ADC1->CR |= ADC_CR_ADDIS;
        while (ADC1->CR & ADC_CR_ADEN);
        ADC1->CR &= ~ADC_CR_ADVREGEN;
    }
#endif
    return changed;
}

#ifdef HHKB_ADC_DIAGNOSTICS
/* Up to 13 keys from `first` (index = col * MATRIX_ROWS + row), one u16 each: kind 0 depth
 * (permille of the calibrated range), 1 raw ADC, 2 rest level, 3 range. For tools/bench/key_depth.py.
 * Kind 4: longest interrupts-off scan half and longest whole scan (us, since the last kind-4 read), then
 * missed scan slots since start (low 16 bits); the last two need HHKB_SCAN_DIAGNOSTICS, else 0. */
void hhkb_adc_diag_read(uint8_t kind, uint8_t first, uint8_t *out) {
    if (kind == 4) {
        out[0] = (uint8_t)s_masked_max_us;
        out[1] = (uint8_t)(s_masked_max_us >> 8);
        s_masked_max_us = 0;
#ifdef HHKB_SCAN_DIAGNOSTICS
        uint16_t full = hhkb_take_full_scan_peak_us();
        out[2] = (uint8_t)full;
        out[3] = (uint8_t)(full >> 8);
        uint32_t missed;
        hhkb_get_scan_diagnostics(NULL, NULL, NULL, &missed, NULL);
        out[4] = (uint8_t)missed;
        out[5] = (uint8_t)(missed >> 8);
#endif
        return;
    }
    uint8_t n = 0;
    for (uint16_t i = first; i < MATRIX_COLS * MATRIX_ROWS && n < 13; i++, n++) {
        const rt_key_t *k = &s_rt_keys[i / MATRIX_ROWS][i % MATRIX_ROWS];
        uint16_t raw = s_raw[i / MATRIX_ROWS][i % MATRIX_ROWS];
        uint16_t v = kind == 0 ? rt_normalize(k, raw) : kind == 1 ? raw : kind == 2 ? k->base
                   : (uint16_t)(k->scale ? ((uint32_t)RT_FULL_SCALE << 16) / k->scale : 0);
        out[2 * n] = (uint8_t)v;
        out[2 * n + 1] = (uint8_t)(v >> 8);
    }
}
#endif
