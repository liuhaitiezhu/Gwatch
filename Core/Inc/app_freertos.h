#ifndef APP_FREERTOS_H
#define APP_FREERTOS_H

#include <stdbool.h>
#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int32_t temperature_centi_c;
    bool valid;
} App_TemperatureReading;

/* Creates shared resources and four tasks, then starts the scheduler.
 * Call after peripheral and TouchGFX framework initialization.
 * LCD/touch initialization runs in DisplayTouchTask. Returns only on failure. */
HAL_StatusTypeDef App_FreeRTOS_Start(void);

/* printf retarget: copied queue messages, from a running task only.
 * Returns bytes accepted, or -1 on failure. */
int App_UART_Write(const char *data, int length);

/* Single copied message, zero queue wait. For FIFO samplers that cannot wait
 * behind UART output. length must be 0..128; returns -1 if the queue is full. */
int App_UART_TryWrite(const char *data, int length);

/* GUI task only: consume the latest sample without waiting.
 * Returns true for a new result (including sensor failure), otherwise false. */
bool App_Temperature_TryRead(App_TemperatureReading *reading);

#ifdef __cplusplus
}
#endif

#endif
