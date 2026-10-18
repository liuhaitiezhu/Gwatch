#ifndef DISPLAY_TEST_SEMPHR_H
#define DISPLAY_TEST_SEMPHR_H
#include "FreeRTOS.h"
typedef StaticSemaphore_t* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken);
#endif
