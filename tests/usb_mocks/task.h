#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskCreateStatic(void (*fn)(void *), const char *name, uint32_t depth, void *arg, UBaseType_t prio,
                               StackType_t *stack, StaticTask_t *tcb);
