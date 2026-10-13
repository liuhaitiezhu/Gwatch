#ifndef APP_FREERTOS_H
#define APP_FREERTOS_H

#include "stm32u5xx_hal.h"

/* Creates the queue and three tasks, then starts the scheduler.
 * process() is one iteration of the existing display/touch application loop.
 * Call after peripheral/display initialization. Returns only on startup failure. */
HAL_StatusTypeDef App_FreeRTOS_Start(void (*process)(void));

/* printf retarget: direct USART1 before startup, copied queue messages afterward.
 * Task context only. Returns bytes accepted, or -1 on failure. */
int App_UART_Write(const char *data, int length);

#endif
