#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
static unsigned resets;
void platform_system_reset(void) { ++resets; }

int main(void) {
    vApplicationStackOverflowHook(NULL, NULL);
    assert(resets == 1);
    puts("FreeRTOS stack overflow hook test passed");
    return 0;
}
