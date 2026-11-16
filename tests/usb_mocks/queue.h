#include "FreeRTOS.h"
QueueHandle_t xQueueCreateStatic(UBaseType_t len, UBaseType_t size, uint8_t *storage, StaticQueue_t *ctl);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
