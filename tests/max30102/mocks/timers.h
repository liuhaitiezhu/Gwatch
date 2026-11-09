#ifndef MOCK_TIMERS_H
#define MOCK_TIMERS_H
#include "FreeRTOS.h"
typedef void *TimerHandle_t;
typedef struct { int unused; } StaticTimer_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
TimerHandle_t xTimerCreateStatic(const char *, TickType_t, BaseType_t, void *,
    TimerCallbackFunction_t, StaticTimer_t *);
BaseType_t xTimerStart(TimerHandle_t, TickType_t);
void *pvTimerGetTimerID(TimerHandle_t);
#endif
