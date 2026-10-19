#ifndef APP_FREERTOS_H
#define APP_FREERTOS_H

#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int32_t temperature_centi_c;
    uint8_t valid;
} App_TemperatureReading;

/* Creates shared resources and three tasks, then starts the scheduler.
 * Call after peripheral and TouchGFX framework initialization.
 * LCD/touch initialization runs in DisplayTouchTask. Returns only on failure. */
HAL_StatusTypeDef App_FreeRTOS_Start(void);

/* printf retarget: copied queue messages, from a running task only.
 * Returns bytes accepted, or -1 on failure. */
int App_UART_Write(const char *data, int length);

/* GUI task only: consume the latest sample without waiting.
 * Returns 1 for a new result (including sensor failure), otherwise 0. */
int App_Temperature_TryRead(App_TemperatureReading *reading);

#ifdef __cplusplus
}
#endif

#endif
