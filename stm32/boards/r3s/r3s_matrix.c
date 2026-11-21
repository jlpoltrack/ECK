/* Realforce R3S matrix scan: 16 mux channels x 6 drive lines, one ADC reading per key.
 * Pins and per-key sequence from the stock firmware (docs/r3s-hardware.md); timings to tune. */
#include "matrix_scan.h"
#include "ec_settings.h"
#include "rapid_trigger.h"
#include "stm32l0xx.h"
#include "stm32l0xx_ll_adc.h"

#ifndef R3S_SETTLE_US
#define R3S_SETTLE_US 7U /* mux address and discharge (PC5) before the strobe; stock ~6.6 us */
#endif
#ifndef R3S_STROBE_US
#define R3S_STROBE_US 1U /* row low until PC11 drops; stock ~1 us */
#endif
#ifndef R3S_SAMPLE_US
#define R3S_SAMPLE_US 4U /* PC11 low until the ADC sample; stock ~4.3 us */
#endif

/* Drive lines (rows) on GPIOC: PC13, PC14, PC15, PC0, PC4, PC3 (PC1, PC2 unused on the TKL). */
static uint16_t s_row_pin[MATRIX_ROWS] = {1U << 13, 1U << 14, 1U << 15, 1U << 0, 1U << 4, 1U << 3}; /* RAM: read in the scan */
#define ROW_PINS_ALL 0xE01FU /* all 8 drive lines */
#define MUX_ADDR_MASK (7U << 12) /* PB12-PB14; channel bit 0 on PB14 */
#define MUX1_EN (1U << 11)       /* PB11, low = on: columns 0-7 */
#define MUX2_EN (1U << 10)       /* PB10, low = on: columns 8-15 */
#define PIN_DISCHARGE (1U << 5)  /* PC5 */
#define PIN_CTRL (1U << 11)      /* PC11 */
#define PIN_POWER (1U << 1)      /* PA1: switched on at start by the stock firmware */
#define ADC_PIN 4U               /* PA4 = ADC_IN4 */

static uint16_t s_raw[MATRIX_COLS][MATRIX_ROWS];
static uint8_t s_col_rows[MATRIX_COLS]; /* rows with a key, per column (from LAYOUT) */
static uint32_t s_col_addr[MATRIX_COLS]; /* mux address BSRR value per column */
#define BOOT_REST_SKIP 8U    /* scans before averaging: supply and ADC settle */
#define BOOT_REST_SCANS 16U
static uint32_t s_boot_sum[MATRIX_COLS][MATRIX_ROWS];
static uint8_t s_boot_scans;
static bool s_recovery; /* the recovery key was held at start-up */

bool board_recovery_requested(void) { return s_recovery; }
#ifdef HHKB_ADC_DIAGNOSTICS
static volatile uint16_t s_masked_max_us; /* longest column read with interrupts off */
#endif

static inline void delay_us_nop(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 6; i++) __NOP();
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
    ADC1->CR |= ADC_CR_ADSTART;
    timeout = 5000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --timeout);
    return (uint16_t)(ADC1->DR & 0x0FFF);
}

/* Mux address of channel ch (0-7): bit 0 on PB14, bit 1 on PB13, bit 2 on PB12. */
static inline uint32_t mux_addr_bsrr(uint8_t ch) {
    uint32_t set = ((ch & 1U) << 14) | ((ch & 2U) << 12) | ((ch & 4U) << 10);
    return set | ((MUX_ADDR_MASK & ~set) << 16);
}

static void set_mode_output(GPIO_TypeDef *port, uint32_t mask) {
    for (uint32_t pin = 0; pin < 16; pin++) {
        if (!(mask & (1U << pin))) continue;
        port->MODER = (port->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
        port->OTYPER &= ~(1U << pin);
        port->OSPEEDR |= 3U << (pin * 2);
        port->PUPDR &= ~(3U << (pin * 2));
    }
}

void hhkb_matrix_hardware_init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;

    static const uint8_t layout[][2] = LAYOUT; /* {row, col} */
    for (unsigned i = 0; i < sizeof(layout) / sizeof(layout[0]); i++) s_col_rows[layout[i][1]] |= 1U << layout[i][0];
    for (uint8_t col = 0; col < MATRIX_COLS; col++) s_col_addr[col] = mux_addr_bsrr(col & 7U);

    /* Idle as the stock firmware leaves it between scans: everything low. */
    GPIOC->BRR = ROW_PINS_ALL | PIN_DISCHARGE | PIN_CTRL;
    GPIOB->BRR = MUX_ADDR_MASK | MUX1_EN | MUX2_EN;
    set_mode_output(GPIOC, ROW_PINS_ALL | PIN_DISCHARGE | PIN_CTRL);
    set_mode_output(GPIOB, MUX_ADDR_MASK | MUX1_EN | MUX2_EN);
    GPIOA->BSRR = PIN_POWER;
    set_mode_output(GPIOA, PIN_POWER);
    GPIOA->MODER |= 3U << (ADC_PIN * 2); /* analog */

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1RST;
    ADC1->CR |= ADC_CR_ADVREGEN;
    delay_us_nop(2000); /* ADC regulator start-up (and PA1 supply settling) */
    ADC1->CR |= ADC_CR_ADCAL;
    uint32_t cal_timeout = 100000;
    while ((ADC1->CR & ADC_CR_ADCAL) && --cal_timeout);
    ADC1->CFGR1 = 0;
    ADC1->CFGR2 = SystemCoreClock < 32000000U ? LL_ADC_CLOCK_SYNC_PCLK_DIV1 : LL_ADC_CLOCK_SYNC_PCLK_DIV2;
    ADC1->CHSELR = 1U << ADC_PIN;
    ADC1->SMPR = 0; /* 1.5 ADC clock cycles, as stock */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    uint32_t rdy_timeout = 100000;
    while (!(ADC1->ISR & ADC_ISR_ADRDY) && --rdy_timeout);

    hhkb_calibration_load();
}

/* Previous key's result, done inside the next key's settle wait (as the HHKB's scan). */
static inline __attribute__((always_inline)) void key_result(matrix_row_t m[], uint8_t col, uint8_t row,
        uint16_t val, const rt_cfg_t *cfg, bool is_calibrating, bool *changed) {
    rt_key_t *key = &s_rt_keys[col][row];
    if (!is_calibrating && val > s_cal_peak[col][row] && val < 4000) hhkb_update_dynamic_peak(col, row, val);
    bool is_pressed;
    if (val <= key->reset_raw) { /* reset zone of rt_update: same result, shorter path */
        key->ext = 0;
        key->down = false;
        key->latched = false;
        is_pressed = false;
    } else {
        is_pressed = rt_update(key, cfg, val);
    }
    matrix_row_t prev = m[row];
    if (is_pressed) m[row] |= (1U << col);
    else m[row] &= ~(1U << col);
    if (m[row] != prev) *changed = true;
}

/* Calibration, debug and start-up rest for one column, interrupts on (after its reads). */
static void column_report(const matrix_row_t m[], uint8_t col, bool capture, bool peaks, bool debug) {
    bool boot = s_boot_scans >= BOOT_REST_SKIP && s_boot_scans < BOOT_REST_SKIP + BOOT_REST_SCANS;
    if (!capture && !peaks && !debug && !boot) return;
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (!(s_col_rows[col] & (1U << row))) continue;
        uint16_t val = s_raw[col][row];
        if (boot) s_boot_sum[col][row] += val;
        if (capture) hhkb_cal_accumulate_baseline(col, row, val);
        if (peaks) hhkb_cal_record_peak(col, row, val);
        if (debug) hhkb_debug_feed_sample(col, row, val, rt_normalize(&s_rt_keys[col][row], val),
                                          (m[row] & (1U << col)) != 0);
    }
}

/* Per key, as stock: PC5 and PC11 high, settle, PC5 low, row low, PC11 low, sample. The mux
 * address is set once per column (rewriting it per key, as stock does, changes no pin). Interrupts
 * are off per column; each read starts from a discharge, so the gap between columns doesn't matter. */
__attribute__((section(".RamFunc"), optimize("O2")))
bool hhkb_matrix_scan_raw(matrix_row_t current_matrix[]) {
    if (!(RCC->APB1ENR & RCC_APB1ENR_TIM2EN)) return false; // the settle waits need TIM2
    hhkb_record_matrix_scan();

    bool capture = hhkb_cal_is_capturing_baseline();
    bool is_calibrating = hhkb_is_calibrating();
    rt_cfg_t scan_cfg = hhkb_get_scan_cfg();
    uint32_t primask = __get_PRIMASK();
    bool debug = hhkb_get_debug_flag(DEBUG_FLAG_TOPRE) && !primask;
    bool record_peaks = is_calibrating && !capture && !primask;
    bool changed = false;

    /* Scan start as stock: rows high (unselected), PC11 high. */
    GPIOC->BSRR = ROW_PINS_ALL | PIN_CTRL;
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        uint8_t rows = s_col_rows[col];
        if (!rows) continue;
        if (col == 0) GPIOB->BSRR = MUX2_EN | (MUX1_EN << 16);
        if (col == 8) GPIOB->BSRR = MUX1_EN | (MUX2_EN << 16);
        __disable_irq();
#ifdef HHKB_ADC_DIAGNOSTICS
        uint16_t masked_start = (uint16_t)TIM2->CNT;
#endif
        GPIOC->BSRR = PIN_DISCHARGE | PIN_CTRL;
        GPIOB->BSRR = s_col_addr[col];
        uint16_t start = (uint16_t)TIM2->CNT;
        int8_t prev_row = -1;
        uint16_t prev_val = 0;
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            if (!(rows & (1U << row))) continue;
            if (prev_row >= 0) key_result(current_matrix, col, (uint8_t)prev_row, prev_val, &scan_cfg,
                                          is_calibrating, &changed);
            scan_wait_since(start, R3S_SETTLE_US);
            GPIOC->BRR = PIN_DISCHARGE;
            GPIOC->BRR = s_row_pin[row];
            scan_delay_us(R3S_STROBE_US);
            GPIOC->BRR = PIN_CTRL;
            scan_delay_us(R3S_SAMPLE_US);
            uint16_t val = adc_read_sample();
            GPIOC->BSRR = ROW_PINS_ALL | PIN_DISCHARGE | PIN_CTRL; /* the next key's discharge starts */
            start = (uint16_t)TIM2->CNT;
            s_raw[col][row] = val;
            prev_row = (int8_t)row;
            prev_val = val;
        }
        key_result(current_matrix, col, (uint8_t)prev_row, prev_val, &scan_cfg, is_calibrating, &changed);
#ifdef HHKB_ADC_DIAGNOSTICS
        uint16_t us = (uint16_t)((uint16_t)TIM2->CNT - masked_start) / HHKB_SCAN_TIMER_TICKS_PER_US;
        if (us > s_masked_max_us) s_masked_max_us = us;
#endif
        __set_PRIMASK(primask);
        column_report(current_matrix, col, capture, record_peaks, debug);
    }
    /* Scan end as stock: rows, PC11, PC5, mux address and enables low. */
    GPIOC->BRR = ROW_PINS_ALL | PIN_CTRL | PIN_DISCHARGE;
    GPIOB->BRR = MUX_ADDR_MASK | MUX1_EN | MUX2_EN;
    if (capture) hhkb_cal_decrement_samples();
    if (s_boot_scans < BOOT_REST_SKIP + BOOT_REST_SCANS && ++s_boot_scans == BOOT_REST_SKIP + BOOT_REST_SCANS) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++)
            for (uint8_t r = 0; r < MATRIX_ROWS; r++)
                if (s_col_rows[c] & (1U << r)) ec_boot_rest(c, r, (uint16_t)(s_boot_sum[c][r] / BOOT_REST_SCANS));
        static const uint8_t keys[][2] = BOARD_RECOVERY_KEYS;
        s_recovery = true;
        for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
            s_recovery = s_recovery && s_boot_sum[keys[k][0]][keys[k][1]] / BOOT_REST_SCANS >= BOARD_RECOVERY_MIN;
    }
    return changed;
}

#ifdef HHKB_ADC_DIAGNOSTICS
/* As the HHKB's (classic_matrix.c): kind 0 depth, 1 raw, 2 rest, 3 range for up to 13 keys from
 * `first` (index = col * MATRIX_ROWS + row); kind 4: longest column read with interrupts off (us), then the
 * longest whole scan and missed slots (HHKB_SCAN_DIAGNOSTICS). */
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
