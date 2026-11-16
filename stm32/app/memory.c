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

