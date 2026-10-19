#include "app_freertos.h"
#include "bsp_i2c_bus.h"
#include "bsp_sht20.h"
#include "bsp_ft6336.h"
#include "bsp_ili9341_4line.h"
#include "app_touchgfx.h"
#include "touchgfx_vsync.h"
#include "main.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
#include <stdio.h>
#include <string.h>

#define SHT20_SAMPLE_PERIOD_MS  2000U
#define UART_QUEUE_LENGTH      8U
#define UART_TEXT_SIZE         128U
#define UART_TIMEOUT_MS        100U
#define GUI_FRAME_PERIOD_MS    16U

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
static QueueHandle_t temperature_queue;
static volatile uint8_t display_ready;
static uint32_t display_gui_polls;
static StaticTimer_t display_timer_storage;
static volatile uint32_t display_timer_count;

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
        const App_TemperatureReading reading = {
            .temperature_centi_c = status == SHT20_OK
                                       ? message.payload.sensor.data.temperature_centi_c : 0,
            .valid = status == SHT20_OK ? 1U : 0U
        };
        /* GUI gets its own copy; a slow display never blocks the sampler. */
        (void)xQueueOverwrite(temperature_queue, &reading);
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

static void DisplayTickTimerCallback(TimerHandle_t timer)
{
    ++display_timer_count;
    /* Timer-service task only wakes the GUI owner; it never touches the HAL. */
    (void)xTaskNotifyGive((TaskHandle_t)pvTimerGetTimerID(timer));
}

static void DisplayTouchTask(void *argument)
{
    (void)argument;
    printf("\r\nTouchGFX + FT6336: USART1 baud=%lu\r\n",
           (unsigned long)huart1.Init.BaudRate);
    const HAL_StatusTypeDef lcd_status = ILI9341_Init();
    if (lcd_status != HAL_OK)
    {
        printf("[display] initialization failed: status=%u\r\n", (unsigned int)lcd_status);
    }
    /* Initialize touch after the display setup has completed. */
    FT6336_init();
    display_ready = lcd_status == HAL_OK;
    if (display_ready)
    {
        TimerHandle_t timer = xTimerCreateStatic("gui_tick",
            pdMS_TO_TICKS(GUI_FRAME_PERIOD_MS), pdTRUE,
            xTaskGetCurrentTaskHandle(), DisplayTickTimerCallback, &display_timer_storage);
        if (timer == NULL || xTimerStart(timer, 0U) != pdPASS)
        {
            printf("[display] GUI timer start failed\r\n");
            Error_Handler();
            return;
        }
        printf("[display] tick=timer period=%lu ms\r\n",
               (unsigned long)GUI_FRAME_PERIOD_MS);
    }
    printf("[rtos] SHT20 -> queue -> USART1, sample period=%lu ms\r\n",
           (unsigned long)SHT20_SAMPLE_PERIOD_MS);
    TickType_t last_display_report = xTaskGetTickCount();
    for (;;)
    {
        /* Scan and read the touch cache in the same task as TouchGFX. */
        FT6336_scan_task();
        if (display_ready)
        {
            /* Coalesce elapsed periods after a slow draw; no catch-up burst. */
            if (ulTaskNotifyTake(pdTRUE, 0U) != 0U)
            {
                TouchGFX_TimerTick();
            }
            MX_TouchGFX_Process();
        }
        const TickType_t now = xTaskGetTickCount();
        if (now - last_display_report >= pdMS_TO_TICKS(SHT20_SAMPLE_PERIOD_MS))
        {
            printf("[display] ready=%u tick=timer TIMER=%lu GUI=%lu\r\n",
                   (unsigned int)display_ready, (unsigned long)display_timer_count,
                   (unsigned long)display_gui_polls);
            last_display_report = now;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

int App_Temperature_TryRead(App_TemperatureReading *reading)
{
    if (reading == NULL || temperature_queue == NULL || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return 0;
    }
    ++display_gui_polls;
    return xQueueReceive(temperature_queue, reading, 0U) == pdPASS;
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

HAL_StatusTypeDef App_FreeRTOS_Start(void)
{
    if (uart_queue != NULL || temperature_queue != NULL ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        return HAL_ERROR;
    }
    /* Leave CubeMX-generated setup intact, but release unused PA11 and its IRQ.
     * This also keeps the pin inactive after regeneration from the unchanged .ioc. */
    HAL_NVIC_DisableIRQ(EXTI11_IRQn);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11);
    HAL_NVIC_ClearPendingIRQ(EXTI11_IRQn);
    if (BSP_I2C1_InitMutex() != HAL_OK)
    {
        return HAL_ERROR;
    }
    uart_queue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(UART_Message));
    if (uart_queue == NULL)
    {
        return HAL_ERROR;
    }
    /* One element is required by xQueueOverwrite: only the newest result matters. */
    temperature_queue = xQueueCreate(1U, sizeof(App_TemperatureReading));
    if (temperature_queue == NULL)
    {
        vQueueDelete(uart_queue);
        uart_queue = NULL;
        return HAL_ERROR;
    }
    /* Stack sizes are 32-bit words: 3 KiB, 2 KiB, and 8 KiB respectively. */
    TaskHandle_t uart_task = NULL;
    TaskHandle_t sensor_task = NULL;
    TaskHandle_t app_task = NULL;
    if (xTaskCreate(UART_PrintTask, "uart_print", 768U, NULL, 2U, &uart_task) == pdPASS &&
        xTaskCreate(SHT20_Task, "sht20", 512U, NULL, 2U, &sensor_task) == pdPASS &&
        xTaskCreate(DisplayTouchTask, "display_touch", 2048U, NULL, 1U, &app_task) == pdPASS)
    {
        vTaskStartScheduler();
        /* A successful start never returns; treat any return as fatal. */
        return HAL_ERROR;
    }
    if (uart_task != NULL) { vTaskDelete(uart_task); }
    if (sensor_task != NULL) { vTaskDelete(sensor_task); }
    if (app_task != NULL) { vTaskDelete(app_task); }
    vQueueDelete(temperature_queue);
    temperature_queue = NULL;
    vQueueDelete(uart_queue);
    uart_queue = NULL;
    return HAL_ERROR;
}

/* Hardware IRQs notify the owning task; never perform I2C/SPI in the ISR. */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_5)
    {
        FT6336_irq_fuc();
    }
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
