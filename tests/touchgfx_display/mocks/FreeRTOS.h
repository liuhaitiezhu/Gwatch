#ifndef DISPLAY_TEST_FREERTOS_H
#define DISPLAY_TEST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef struct { bool available; } StaticSemaphore_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
void testYieldFromISR(BaseType_t woken);
#define portYIELD_FROM_ISR(woken) testYieldFromISR(woken)
#endif
