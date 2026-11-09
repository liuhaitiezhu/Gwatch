#ifndef APP_MAX30102_H
#define APP_MAX30102_H

#include <stdbool.h>
#include "bsp_max30102.h"
#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    MAX30102_Sample sample;
    uint32_t sample_count; /* Total pairs read since task start, across retries. */
    uint32_t tick_ms;      /* HAL time when read/reported, not ADC sample time. */
    MAX30102_Status status;
    uint8_t interrupt_status;
    uint8_t lost_samples;
    bool valid; /* Successful raw read without ALC overflow; NOT finger/HR validity. */
} App_MAX30102Reading;

/* Boot-only resource management, before scheduler start. I2C1 mutex and UART
 * queue must already exist. DeleteTask is startup-failure cleanup only. */
HAL_StatusTypeDef App_MAX30102_CreateTask(void);
void App_MAX30102_DeleteTask(void);

/* GUI task: consume newest result without waiting. One consumer; queue copies
 * the value, including invalid/error results. This is NOT a lossless PPG stream.
 * Future HR/SpO2 algorithms must consume every batch inside the sampling task. */
bool App_MAX30102_TryRead(App_MAX30102Reading *reading);

#ifdef __cplusplus
}
#endif

#endif
