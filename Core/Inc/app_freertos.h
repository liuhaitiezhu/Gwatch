#ifndef APP_FREERTOS_H
#define APP_FREERTOS_H

#include "stm32u5xx_hal.h"

/* Creates shared resources and three tasks, then starts the scheduler.
 * Call after peripheral and TouchGFX framework initialization.
 * LCD/touch initialization runs in DisplayTouchTask. Returns only on failure. */
HAL_StatusTypeDef App_FreeRTOS_Start(void);

/* printf retarget: copied queue messages, from a running task only.
 * Returns bytes accepted, or -1 on failure. */
int App_UART_Write(const char *data, int length);

#endif
