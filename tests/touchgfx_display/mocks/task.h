#ifndef DISPLAY_TEST_TASK_H
#define DISPLAY_TEST_TASK_H
#include "FreeRTOS.h"
#define taskSCHEDULER_NOT_STARTED 1
#define taskSCHEDULER_RUNNING 2
#define taskSCHEDULER_SUSPENDED 0
BaseType_t xTaskGetSchedulerState(void);
#endif
