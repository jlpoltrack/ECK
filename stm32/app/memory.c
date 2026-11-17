#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdbool.h>

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
    (void)task;
    (void)task_name;
    // A corrupted task stack cannot safely continue executing.
    extern void platform_system_reset(void);
    platform_system_reset();
}

#ifdef HHKB_ADC_DIAGNOSTICS
#include "task_internal.h"

#define STACK_FILL 0xA5A5A5A5U /* FreeRTOS fills task stacks with 0xA5 bytes */
static const uint32_t *s_stack[STACK_DIAG_COUNT];
static uint32_t s_words[STACK_DIAG_COUNT];

void stack_diag_register(uint8_t id, const void *stack, uint32_t words) {
    s_stack[id] = stack;
    s_words[id] = words;
}

/* Fill the unused main (interrupt) stack, from the end of .bss to just below SP. The scheduler
 * restarts this stack at its top, so its later peak shows as overwritten fill. */
void __attribute__((noinline)) stack_diag_paint_msp(void) {
    extern uint32_t end, _estack;
    uint32_t *p = &end, *sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    stack_diag_register(STACK_DIAG_MSP, p, (uint32_t)(&_estack - p));
    while (p < sp - 16) *p++ = STACK_FILL;
}

void stack_diag_read(uint8_t *out) {
    for (uint8_t i = 0; i < STACK_DIAG_COUNT; i++) {
        uint32_t unused = 0;
        while (unused < s_words[i] && s_stack[i][unused] == STACK_FILL) unused++;
        uint16_t size = (uint16_t)(s_words[i] * 4U), peak = (uint16_t)((s_words[i] - unused) * 4U);
        out[4 * i] = (uint8_t)size;
        out[4 * i + 1] = (uint8_t)(size >> 8);
        out[4 * i + 2] = (uint8_t)peak;
        out[4 * i + 3] = (uint8_t)(peak >> 8);
    }
}
#endif

