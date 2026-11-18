/* Matrix scan driver interface: each board implements it (boards/<board>/). */
#ifndef MATRIX_SCAN_H
#define MATRIX_SCAN_H

#include "config.h"
#include "ec_settings.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint16_t matrix_row_t;

#define HHKB_SCAN_TIMER_TICKS_PER_US 4U

#ifdef __cplusplus
extern "C" {
#endif

void hhkb_matrix_hardware_init(void);
bool hhkb_matrix_scan_raw(matrix_row_t current_matrix[]);
#ifdef HHKB_ADC_DIAGNOSTICS
void hhkb_adc_diag_read(uint8_t kind, uint8_t first, uint8_t *out);
#endif
#ifdef HHKB_SCAN_DIAGNOSTICS
void hhkb_get_scan_diagnostics(uint32_t *scan_us, uint32_t *scan_max_us, uint32_t *work_max_us, uint32_t *missed, uint32_t *q_full);
void hhkb_reset_scan_diagnostics_peaks(void);
uint16_t hhkb_take_full_scan_peak_us(void); /* longest whole scan since the last call (HHKB_SCAN_DIAGNOSTICS) */
#endif

#ifdef __cplusplus
}
#endif

#endif // MATRIX_SCAN_H
