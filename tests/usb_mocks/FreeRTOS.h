/* Just enough FreeRTOS for app_usb_tinyusb.c; test_usb_routing.c implements the calls. */
#ifndef MOCK_USB_FREERTOS_H
#define MOCK_USB_FREERTOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t StackType_t;
typedef struct { int unused; } StaticTask_t;
typedef struct { int unused; } StaticQueue_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY 0xFFFFFFFFUL
#define configSUPPORT_STATIC_ALLOCATION 1
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
