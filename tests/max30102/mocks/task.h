#ifndef MOCK_TASK_H
#define MOCK_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING 1
BaseType_t xTaskGetSchedulerState(void);
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint16_t, void *, UBaseType_t, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskStartScheduler(void);
void vTaskDelay(TickType_t);
BaseType_t xTaskDelayUntil(TickType_t *, TickType_t);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
typedef enum { eNoAction, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;
BaseType_t xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction);
BaseType_t xTaskNotifyFromISR(TaskHandle_t, uint32_t, eNotifyAction, BaseType_t *);
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, TickType_t);
#endif
