#ifndef TASK_INTERNAL_H
#define TASK_INTERNAL_H

#include <stdint.h>

#define TASK_DELAY_DEF 20
#define TASK_DELAY_FAST 5

#define TASK_PRIO_DEF 1

// Prioritized FreeRTOS task levels:
// The matrix scanner runs highest so scans keep their timing; keyboard logic, USB and the
// Hybrid Bluetooth link task share the next level.
#define TASK_PRIO_KEYBOARD 2
#define TASK_PRIO_USBD 2
#define TASK_PRIO_MATRIX 3

// Task stack sizes in words (4 bytes). Static worst cases (tools/stack_depth.py, 2026-11-17) plus
// ~90 B for saved context: matrix ~650 B, keyboard ~780 B (battery wake scan with debug output)
#define TASK_STACK_MATRIX 192
#ifdef VIAL_ENABLE
#define TASK_STACK_KEYBOARD 512 // feature presses nest up to 3 deep: ~1.3 KB worst case (docs/vial.md)
#else
#define TASK_STACK_KEYBOARD 256
#endif
#define TASK_STACK_DEF 384

#define TASK_PARAMS_DEF NULL
#define TASK_HANDLER_DEF NULL

// Sane queue sizes (saving RAM while preventing dropped keys)
#ifndef QUEUE_DEF_SIZE
#define QUEUE_DEF_SIZE 16
#endif

#ifdef HHKB_ADC_DIAGNOSTICS
// Stack use readout (VIA diag kind 5): each stack's size and peak use, in bytes
enum { STACK_DIAG_MATRIX, STACK_DIAG_KEYBOARD, STACK_DIAG_USBD, STACK_DIAG_BLE, STACK_DIAG_IDLE, STACK_DIAG_MSP, STACK_DIAG_COUNT };
void stack_diag_register(uint8_t id, const void *stack, uint32_t words);
void stack_diag_paint_msp(void);
void stack_diag_read(uint8_t *out);
#endif

#define TASK_DEFS TASK_STACK_DEF, TASK_PARAMS_DEF, TASK_PRIO_DEF, TASK_HANDLER_DEF

#endif // TASK_INTERNAL_H
