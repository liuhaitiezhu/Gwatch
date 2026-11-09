#include "app_max30102.h"
#include "app_freertos.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdio.h>

#define POLL_PERIOD_MS        20U
#define RETRY_PERIOD_MS       1000U
#define REPORT_PERIOD_MS      1000U
#define SAMPLE_TIMEOUT_MS     500U
#define MAX_BATCHES_PER_POLL  (MAX30102_FIFO_DEPTH / MAX30102_FIFO_BATCH_SIZE)

static QueueHandle_t reading_queue;
static TaskHandle_t sensor_task;

static void ReportReading(const App_MAX30102Reading *reading)
{
    char line[128];
    int length;
    if (reading->status == MAX30102_OK)
    {
        length = snprintf(line, sizeof(line),
            "[MAX30102] n=%lu t=%lu RED=%lu IR=%lu valid=%u flags=0x%02X\r\n",
            (unsigned long)reading->sample_count, (unsigned long)reading->tick_ms,
            (unsigned long)reading->sample.red, (unsigned long)reading->sample.ir,
            (unsigned int)reading->valid, (unsigned int)reading->interrupt_status);
    }
    else
    {
        length = snprintf(line, sizeof(line), "[MAX30102] t=%lu ERROR: %s lost=%u\r\n",
            (unsigned long)reading->tick_ms, MAX30102_StatusString(reading->status),
            (unsigned int)reading->lost_samples);
    }
    if (length > 0)
    {
        int count = (size_t)length < sizeof(line) ? length : (int)sizeof(line) - 1;
        /* UART congestion must never stall FIFO servicing. */
        (void)App_UART_TryWrite(line, count);
    }
}

static void MAX30102_Task(void *argument)
{
    (void)argument;
    bool initialized = false;
    bool reported = false;
    uint32_t sequence = 0U;
    uint32_t last_data = 0U;
    uint32_t last_report = 0U;
    MAX30102_Status reported_status = MAX30102_ERROR_NOT_INITIALIZED;
    bool reported_valid = false;
    for (;;)
    {
        App_MAX30102Reading reading = {0};
        bool publish = false;
        if (!initialized)
        {
            reading.status = MAX30102_Init();
            initialized = reading.status == MAX30102_OK;
            last_data = HAL_GetTick();
            publish = true;
        }
        if (initialized)
        {
            /* Bound each poll, releasing the bus after each <=48-byte burst. */
            for (uint32_t batch = 0U; batch < MAX_BATCHES_PER_POLL; ++batch)
            {
                MAX30102_Sample samples[MAX30102_FIFO_BATCH_SIZE];
                MAX30102_FIFOInfo info;
                reading.status = MAX30102_ReadFIFO(samples, MAX30102_FIFO_BATCH_SIZE, &info);
                reading.interrupt_status |= info.interrupt_status;
                reading.lost_samples = info.lost_samples;
                if (reading.status != MAX30102_OK)
                {
                    break;
                }
                if (info.count != 0U)
                {
                    /* Feed future HR/SpO2 processing ALL samples here at 100 Hz,
                     * before reducing the batch to the newest pair for the GUI. */
                    sequence += info.count;
                    reading.sample = samples[info.count - 1U];
                    reading.valid = true;
                    last_data = HAL_GetTick();
                    publish = true;
                }
                if (info.count < MAX30102_FIFO_BATCH_SIZE)
                {
                    break;
                }
            }
            if (reading.status == MAX30102_OK &&
                (uint32_t)(HAL_GetTick() - last_data) >= SAMPLE_TIMEOUT_MS)
            {
                reading.status = MAX30102_ERROR_SAMPLE_TIMEOUT;
            }
            if ((reading.interrupt_status & MAX30102_INT_ALC_OVERFLOW) != 0U)
            {
                reading.valid = false;
                publish = true;
            }
        }
        if (reading.status != MAX30102_OK)
        {
            initialized = false;
            reading.valid = false;
            reading.sample = (MAX30102_Sample){0};
            publish = true;
        }
        reading.sample_count = sequence;
        reading.tick_ms = HAL_GetTick();
        if (publish)
        {
            (void)xQueueOverwrite(reading_queue, &reading);
            if (!reported || reading.status != reported_status ||
                reading.valid != reported_valid ||
                (uint32_t)(reading.tick_ms - last_report) >= REPORT_PERIOD_MS)
            {
                ReportReading(&reading);
                last_report = reading.tick_ms;
                reported_status = reading.status;
                reported_valid = reading.valid;
                reported = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(initialized ? POLL_PERIOD_MS : RETRY_PERIOD_MS));
    }
}

HAL_StatusTypeDef App_MAX30102_CreateTask(void)
{
    if (__get_IPSR() != 0U || reading_queue != NULL || sensor_task != NULL ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        return HAL_ERROR;
    }
    reading_queue = xQueueCreate(1U, sizeof(App_MAX30102Reading));
    if (reading_queue == NULL)
    {
        return HAL_ERROR;
    }
    const App_MAX30102Reading initial = {.status = MAX30102_ERROR_NOT_INITIALIZED};
    (void)xQueueOverwrite(reading_queue, &initial);
    /* 512 32-bit words = 2 KiB; priority matches the other sensor task. */
    if (xTaskCreate(MAX30102_Task, "max30102", 512U, NULL, 2U, &sensor_task) != pdPASS)
    {
        vQueueDelete(reading_queue);
        reading_queue = NULL;
        sensor_task = NULL;
        return HAL_ERROR;
    }
    return HAL_OK;
}

void App_MAX30102_DeleteTask(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        return;
    }
    if (sensor_task != NULL)
    {
        vTaskDelete(sensor_task);
        sensor_task = NULL;
    }
    if (reading_queue != NULL)
    {
        vQueueDelete(reading_queue);
        reading_queue = NULL;
    }
}

bool App_MAX30102_TryRead(App_MAX30102Reading *reading)
{
    if (reading == NULL || reading_queue == NULL || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return false;
    }
    return xQueueReceive(reading_queue, reading, 0U) == pdPASS;
}
