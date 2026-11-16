// FreeRTOS includes
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

// General C includes
#include <stdbool.h>

// Project includes
#include "config.h"
#include "events.h"
#include "keyboard.h"
#include "platform.h"
#include "matrix.h"
#include "task_internal.h"
#include "usb.h"

#ifndef __NOP
#define __NOP() __asm__ volatile("nop")
#endif

// Global FreeRTOS inter-task communication queues
QueueHandle_t matrix_queue;

static uint8_t s_matrix_queue_storage[QUEUE_DEF_SIZE * sizeof(matrix_event_t)];
static StaticQueue_t s_matrix_queue_struct;

static StaticTask_t s_keyboard_task_tcb;
static StackType_t s_keyboard_task_stack[TASK_STACK_KEYBOARD];

static StaticTask_t s_matrix_task_tcb;
static StackType_t s_matrix_task_stack[TASK_STACK_MATRIX];

void vApplicationIdleHook(void) {
    __asm__ volatile("wfi");
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/**
 * @brief Firmware entry point: initializes platform HAL, allocates queues, spawns tasks, and starts RTOS scheduler.
 */
int main(void) {
    // Platform-specific low-level clock and system peripheral setup
    platform_init();

    // Status LEDs off
    indicators_init();

    // Initialize USB HID hardware and endpoints
    usb_init();

    // Create FreeRTOS matrix event queue
    matrix_queue =
        xQueueCreateStatic(QUEUE_DEF_SIZE, sizeof(matrix_event_t), s_matrix_queue_storage, &s_matrix_queue_struct);

    if (matrix_queue == NULL) {
        platform_system_reset();
    }

    BaseType_t status = pdPASS;

    // Keyboard state machine, layer stack, and tap engine task
    if (xTaskCreateStatic(keyboard_task, "keyboard", TASK_STACK_KEYBOARD, NULL, TASK_PRIO_KEYBOARD,
                          s_keyboard_task_stack, &s_keyboard_task_tcb) == NULL) {
        status = pdFAIL;
    }

    // Matrix switch scanner task
    if (xTaskCreateStatic(matrix_task, "matrix", TASK_STACK_MATRIX, NULL, TASK_PRIO_MATRIX, s_matrix_task_stack,
                          &s_matrix_task_tcb) == NULL) {
        status = pdFAIL;
    }

    if (status != pdPASS) {
        platform_system_reset();
    }

    // Invoke user modules / extension early initialization
    events_early_init();

    // Start FreeRTOS preemptive multitasking scheduler
    vTaskStartScheduler();

    // Returns only if the scheduler could not start (all objects are static, so not expected)
    platform_system_reset();
}
