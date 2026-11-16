#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"

extern volatile uint32_t malloc_failure_count;
void vApplicationMallocFailedHook(void);
void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
static unsigned resets;
void platform_system_reset(void) { ++resets; }

int main(void) {
    malloc_failure_count = 0;
    vApplicationMallocFailedHook();
    vApplicationMallocFailedHook();
    assert(malloc_failure_count == 2);
    malloc_failure_count = UINT32_MAX;
    vApplicationMallocFailedHook();
    assert(malloc_failure_count == UINT32_MAX);
    vApplicationStackOverflowHook(NULL, NULL);
    assert(resets == 1);
    puts("FreeRTOS failure hook tests passed");
    return 0;
}
