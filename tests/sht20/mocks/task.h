#ifndef MOCK_TASK_H
#define MOCK_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING 1
#define taskSCHEDULER_SUSPENDED 2
BaseType_t xTaskGetSchedulerState(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
BaseType_t xTaskDelayUntil(TickType_t *, TickType_t);
BaseType_t xTaskCreate(TaskFunction_t, const char *, size_t, void *, UBaseType_t, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskStartScheduler(void);
#endif
