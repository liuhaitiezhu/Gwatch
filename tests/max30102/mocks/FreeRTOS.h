#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY UINT32_MAX
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
void MockYieldFromISR(BaseType_t woken);
#define portYIELD_FROM_ISR(woken) MockYieldFromISR(woken)
#endif
