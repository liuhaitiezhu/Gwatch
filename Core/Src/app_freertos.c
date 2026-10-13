#include "app_freertos.h"
#include "bsp_i2c_bus.h"
#include "bsp_sht20.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define SHT20_SAMPLE_PERIOD_MS  2000U
#define UART_QUEUE_LENGTH      8U
#define UART_TEXT_SIZE         128U
#define UART_TIMEOUT_MS        100U

typedef enum
{
    UART_MESSAGE_SHT20,
    UART_MESSAGE_TEXT
} UART_MessageType;

/* Queue copies the complete object, never a pointer to a task's local buffer. */
typedef struct
{
    UART_MessageType type;
    union
    {
        struct
        {
            uint32_t sequence;
            uint32_t tick_ms;
            SHT20_Status status;
            SHT20_Data data;
        } sensor;
        struct
        {
            uint16_t length;
            char data[UART_TEXT_SIZE];
        } text;
    } payload;
} UART_Message;

static QueueHandle_t uart_queue;
static void (*application_process)(void);

static void SHT20_Task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t sequence = 0U;
    uint8_t initialized = 0U;
    for (;;)
    {
        UART_Message message = {0};
        message.type = UART_MESSAGE_SHT20;
        message.payload.sensor.sequence = ++sequence;
        SHT20_Status status = SHT20_OK;
        if (!initialized)
        {
            status = SHT20_Init();
            initialized = status == SHT20_OK;
        }
        if (status == SHT20_OK)
        {
            status = SHT20_Read(&message.payload.sensor.data);
        }
        if (status != SHT20_OK)
        {
            initialized = 0U; /* Retry initialization on the next sample. */
        }
        message.payload.sensor.status = status;
        message.payload.sensor.tick_ms = HAL_GetTick();
        (void)xQueueSend(uart_queue, &message, portMAX_DELAY);
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SHT20_SAMPLE_PERIOD_MS));
    }
}

static void UART_PrintTask(void *argument)
{
    (void)argument;
    UART_Message message;
    char line[UART_TEXT_SIZE];
    for (;;)
    {
        if (xQueueReceive(uart_queue, &message, portMAX_DELAY) != pdPASS)
        {
            continue;
        }
        if (message.type == UART_MESSAGE_TEXT)
        {
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)message.payload.text.data,
                                    message.payload.text.length, UART_TIMEOUT_MS);
            continue;
        }

        int length;
        if (message.payload.sensor.status == SHT20_OK)
        {
            int32_t temperature = message.payload.sensor.data.temperature_centi_c;
            uint32_t magnitude = (uint32_t)(temperature < 0 ? -temperature : temperature);
            uint32_t humidity = message.payload.sensor.data.humidity_centi_pct;
            length = snprintf(line, sizeof(line),
                "[SHT20] #%lu t=%lu ms T=%s%lu.%02lu C RH=%lu.%02lu %%\r\n",
                (unsigned long)message.payload.sensor.sequence,
                (unsigned long)message.payload.sensor.tick_ms,
                temperature < 0 ? "-" : "",
                (unsigned long)(magnitude / 100U), (unsigned long)(magnitude % 100U),
                (unsigned long)(humidity / 100U), (unsigned long)(humidity % 100U));
        }
        else
        {
            length = snprintf(line, sizeof(line), "[SHT20] #%lu t=%lu ms ERROR: %s\r\n",
                (unsigned long)message.payload.sensor.sequence,
                (unsigned long)message.payload.sensor.tick_ms,
                SHT20_StatusString(message.payload.sensor.status));
        }
        if (length > 0)
        {
            uint16_t bytes = (uint16_t)((size_t)length < sizeof(line)
                                          ? (size_t)length : sizeof(line) - 1U);
            /* Use HAL directly: printf/_write would feed our own queue again. */
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, bytes, UART_TIMEOUT_MS);
        }
    }
}

static void ApplicationTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        application_process();
        /* Keep the existing application polling; let the other tasks run.
         * This blocks this task only, and does not put the MCU to sleep. */
        vTaskDelay(1U);
    }
}

int App_UART_Write(const char *data, int length)
{
    if (data == NULL || length < 0 || __get_IPSR() != 0U)
    {
        return -1;
    }
    if (length == 0)
    {
        return 0;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        if (length > UINT16_MAX)
        {
            return -1;
        }
        return HAL_UART_Transmit(&huart1, (const uint8_t *)data, (uint16_t)length,
                                  UART_TIMEOUT_MS) == HAL_OK ? length : -1;
    }
    if (uart_queue == NULL || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return -1;
    }

    int sent = 0;
    while (sent < length)
    {
        UART_Message message = {0};
        message.type = UART_MESSAGE_TEXT;
        size_t count = (size_t)(length - sent);
        if (count > UART_TEXT_SIZE)
        {
            count = UART_TEXT_SIZE;
        }
        message.payload.text.length = (uint16_t)count;
        memcpy(message.payload.text.data, data + sent, count);
        /* Bounded queue wait also prevents display logging from hanging forever. */
        if (xQueueSend(uart_queue, &message, pdMS_TO_TICKS(UART_TIMEOUT_MS)) != pdPASS)
        {
            return sent != 0 ? sent : -1;
        }
        sent += (int)count;
    }
    return sent;
}

HAL_StatusTypeDef App_FreeRTOS_Start(void (*process)(void))
{
    if (process == NULL || uart_queue != NULL ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        return HAL_ERROR;
    }
    application_process = process;
    if (BSP_I2C1_InitMutex() != HAL_OK)
    {
        return HAL_ERROR;
    }
    uart_queue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(UART_Message));
    if (uart_queue == NULL)
    {
        return HAL_ERROR;
    }
    /* Stack sizes are 32-bit words: 3 KiB, 2 KiB, and 8 KiB respectively. */
    TaskHandle_t uart_task = NULL;
    TaskHandle_t sensor_task = NULL;
    TaskHandle_t app_task = NULL;
    if (xTaskCreate(UART_PrintTask, "uart_print", 768U, NULL, 2U, &uart_task) == pdPASS &&
        xTaskCreate(SHT20_Task, "sht20", 512U, NULL, 2U, &sensor_task) == pdPASS &&
        xTaskCreate(ApplicationTask, "display_touch", 2048U, NULL, 1U, &app_task) == pdPASS)
    {
        vTaskStartScheduler();
        /* A successful start never returns; treat any return as fatal. */
        return HAL_ERROR;
    }
    if (uart_task != NULL) { vTaskDelete(uart_task); }
    if (sensor_task != NULL) { vTaskDelete(sensor_task); }
    if (app_task != NULL) { vTaskDelete(app_task); }
    vQueueDelete(uart_queue);
    uart_queue = NULL;
    return HAL_ERROR;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    /* Set a breakpoint here. Avoid printf/queues after stack corruption. */
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    /* Set a breakpoint here to catch an exhausted FreeRTOS heap. */
    Error_Handler();
}
