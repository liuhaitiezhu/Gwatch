/* Include production tasks to exercise startup rollback and the real copied
 * UART/latest-reading queues; no test hooks are added to firmware sources. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "../../../Core/Src/app_freertos.c"
#include "../../../Core/Src/app_max30102.c"

struct MockQueue
{
    unsigned int length, item_size, count, head;
    unsigned char *items;
};
typedef struct
{
    TaskFunction_t function;
    const char *name;
    bool active;
} MockTask;

static MockTask tasks[8];
static unsigned int task_calls, queue_calls, live_tasks, live_queues;
static unsigned int fail_task, fail_queue, isr, step, init_calls, read_calls;
static unsigned int nonzero_uart_waits, raw_sequence;
static BaseType_t scheduler;
static uint32_t tick;
static jmp_buf scheduler_exit, sampler_exit;
UART_HandleTypeDef huart1;

uint32_t __get_IPSR(void) { return isr; }
uint32_t HAL_GetTick(void) { return tick; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler; }
TickType_t xTaskGetTickCount(void) { return tick; }

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    if (++queue_calls == fail_queue) { return NULL; }
    QueueHandle_t queue = calloc(1U, sizeof(*queue));
    assert(queue != NULL);
    queue->items = calloc(length, item_size);
    assert(queue->items != NULL);
    queue->length = length;
    queue->item_size = item_size;
    ++live_queues;
    return queue;
}
void vQueueDelete(QueueHandle_t queue)
{
    assert(queue != NULL && live_queues != 0U);
    free(queue->items); free(queue); --live_queues;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue != NULL && queue->length == 1U);
    memcpy(queue->items, item, queue->item_size);
    queue->head = 0U; queue->count = 1U;
    return pdPASS;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    assert(queue != NULL);
    if (queue == uart_queue && wait != 0U) { ++nonzero_uart_waits; }
    if (queue->count == queue->length) { return pdFAIL; }
    unsigned int tail = (queue->head + queue->count) % queue->length;
    memcpy(queue->items + tail * queue->item_size, item, queue->item_size);
    ++queue->count;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)wait;
    assert(queue != NULL);
    if (queue->count == 0U) { return pdFAIL; }
    memcpy(item, queue->items + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1U) % queue->length;
    --queue->count;
    return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t stack,
    void *argument, UBaseType_t priority, TaskHandle_t *handle)
{
    (void)argument;
    assert(stack >= 512U && priority >= 1U && priority <= 2U);
    ++task_calls;
    if (task_calls == fail_task) { return pdFAIL; }
    MockTask *task = &tasks[task_calls - 1U];
    *task = (MockTask){function, name, true};
    *handle = task;
    ++live_tasks;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t handle)
{
    MockTask *task = handle;
    assert(task->active && live_tasks != 0U);
    task->active = false; --live_tasks;
}
void vTaskStartScheduler(void)
{
    scheduler = taskSCHEDULER_RUNNING;
    longjmp(scheduler_exit, 1);
}

static const struct
{
    MAX30102_Status status;
    uint8_t count, flags, lost;
} reads[] = {
    {MAX30102_OK, 2U, 0U, 0U},
    {MAX30102_OK, 8U, 0U, 0U}, {MAX30102_OK, 1U, 0U, 0U},
    {MAX30102_OK, 1U, MAX30102_INT_ALC_OVERFLOW, 0U},
    {MAX30102_OK, 8U, 0U, 0U}, {MAX30102_ERROR_FIFO_OVERFLOW, 0U, 0x80U, 3U},
    {MAX30102_OK, 1U, 0U, 0U},
    {MAX30102_ERROR_POWER_RESET, 0U, MAX30102_INT_POWER_READY, 0U}
};

MAX30102_Status MAX30102_Init(void)
{
    ++init_calls;
    return init_calls == 1U ? MAX30102_ERROR_I2C : MAX30102_OK;
}
MAX30102_Status MAX30102_ReadFIFO(MAX30102_Sample *samples, uint8_t capacity,
    MAX30102_FIFOInfo *info)
{
    assert(capacity == MAX30102_FIFO_BATCH_SIZE);
    if (read_calls >= sizeof(reads) / sizeof(reads[0]))
    {
        *info = (MAX30102_FIFOInfo){0};
        ++read_calls;
        return MAX30102_OK;
    }
    unsigned int index = read_calls++;
    *info = (MAX30102_FIFOInfo){reads[index].count, reads[index].flags, reads[index].lost};
    for (unsigned int i = 0U; i < info->count; ++i)
    {
        ++raw_sequence;
        samples[i] = (MAX30102_Sample){10000U + raw_sequence, 20000U + raw_sequence};
    }
    return reads[index].status;
}
const char *MAX30102_StatusString(MAX30102_Status status)
{
    (void)status;
    return "mock sensor error";
}

void vTaskDelay(TickType_t delay)
{
    App_MAX30102Reading reading;
    bool available = App_MAX30102_TryRead(&reading);
    assert(!App_MAX30102_TryRead(NULL));
    if (step <= 7U) { assert(available); }
    switch (step++)
    {
        case 0U:
            assert(!reading.valid && reading.status == MAX30102_ERROR_I2C);
            assert(reading.sample.red == 0U && delay == 1000U);
            break;
        case 1U:
            assert(reading.valid && reading.sample_count == 2U);
            assert(reading.sample.red == 10002U && reading.sample.ir == 20002U);
            assert(delay == 20U); break;
        case 2U:
            assert(reading.valid && reading.sample_count == 11U);
            assert(reading.sample.red == 10011U); break;
        case 3U:
            assert(!reading.valid && reading.status == MAX30102_OK);
            assert(reading.interrupt_status & MAX30102_INT_ALC_OVERFLOW); break;
        case 4U:
            assert(!reading.valid && reading.status == MAX30102_ERROR_FIFO_OVERFLOW);
            assert(reading.sample.red == 0U && reading.lost_samples == 3U);
            assert(delay == 1000U); break;
        case 5U:
            assert(reading.valid && reading.sample_count == 21U);
            assert(reading.sample.ir == 20021U && delay == 20U); break;
        case 6U:
            assert(!reading.valid && reading.status == MAX30102_ERROR_POWER_RESET);
            assert(delay == 1000U); break;
        case 7U:
            assert(!reading.valid && reading.status == MAX30102_OK);
            assert(delay == 20U); break;
        default:
            if (available)
            {
                assert(reading.status == MAX30102_ERROR_SAMPLE_TIMEOUT && !reading.valid);
                assert(delay == 1000U && reading.sample.red == 0U);
                assert(init_calls == 4U && nonzero_uart_waits == 0U);
                longjmp(sampler_exit, 1);
            }
            assert(delay == 20U && step < 40U);
            break;
    }
    assert(!App_MAX30102_TryRead(&reading)); /* Latest result consumed once. */
    tick += delay;
}

/* Other existing tasks/peripherals are linked but not run in this host test. */
HAL_StatusTypeDef BSP_I2C1_InitMutex(void) { return HAL_OK; }
SHT20_Status SHT20_Init(void) { return SHT20_OK; }
SHT20_Status SHT20_Read(SHT20_Data *data) { (void)data; return SHT20_ERROR_I2C; }
const char *SHT20_StatusString(SHT20_Status status) { (void)status; return "mock"; }
HAL_StatusTypeDef ILI9341_Init(void) { return HAL_OK; }
void FT6336_init(void) {}
void FT6336_scan_task(bool touch_event) { (void)touch_event; }
uint32_t FT6336_get_scan_delay_ms(void) { return UINT32_MAX; }
void MX_TouchGFX_Process(void) {}
void TouchGFX_TimerTick(void) {}
void Error_Handler(void) { assert(!"unexpected Error_Handler"); }
void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority)
{ (void)irq; (void)priority; (void)subpriority; }
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
void HAL_GPIO_DeInit(void *port, uint32_t pin) { (void)port; (void)pin; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
    uint16_t size, uint32_t timeout)
{ (void)uart; (void)data; (void)size; (void)timeout; return HAL_OK; }
BaseType_t xTaskDelayUntil(TickType_t *last, TickType_t period)
{ *last += period; return pdTRUE; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &tasks[0]; }
BaseType_t xTaskNotifyGive(TaskHandle_t handle) { (void)handle; return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{ (void)clear; (void)wait; return 0U; }
BaseType_t xTaskNotify(TaskHandle_t handle, uint32_t value, eNotifyAction action)
{ (void)handle; (void)value; (void)action; return pdPASS; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t handle, uint32_t value, eNotifyAction action, BaseType_t *woken)
{ (void)handle; (void)value; (void)action; *woken = pdFALSE; return pdPASS; }
BaseType_t xTaskNotifyWait(uint32_t entry, uint32_t exit, uint32_t *value, TickType_t wait)
{ (void)entry; (void)exit; (void)wait; *value = 0U; return pdFAIL; }
void MockYieldFromISR(BaseType_t woken) { (void)woken; }
TimerHandle_t xTimerCreateStatic(const char *name, TickType_t period, BaseType_t reload,
    void *id, TimerCallbackFunction_t callback, StaticTimer_t *storage)
{ (void)name; (void)period; (void)reload; (void)callback; (void)storage; return id; }
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait)
{ (void)timer; (void)wait; return pdPASS; }
void *pvTimerGetTimerID(TimerHandle_t timer) { return timer; }

static void ResetStartup(void)
{
    assert(live_tasks == 0U && live_queues == 0U);
    task_calls = queue_calls = fail_task = fail_queue = 0U;
    scheduler = taskSCHEDULER_NOT_STARTED;
    memset(tasks, 0, sizeof(tasks));
}

int main(void)
{
    App_MAX30102Reading reading;
    assert(!App_MAX30102_TryRead(&reading));
    for (unsigned int failure = 1U; failure <= 3U; ++failure)
    {
        ResetStartup(); fail_queue = failure;
        assert(App_FreeRTOS_Start() == HAL_ERROR);
        assert(live_tasks == 0U && live_queues == 0U);
    }
    for (unsigned int failure = 1U; failure <= 4U; ++failure)
    {
        ResetStartup(); fail_task = failure;
        assert(App_FreeRTOS_Start() == HAL_ERROR);
        assert(live_tasks == 0U && live_queues == 0U);
    }
    ResetStartup();
    if (setjmp(scheduler_exit) == 0) { (void)App_FreeRTOS_Start(); assert(0); }
    assert(live_tasks == 4U && live_queues == 3U);
    assert(!strcmp(tasks[0].name, "max30102"));
    assert(App_MAX30102_CreateTask() == HAL_ERROR);
    App_MAX30102_DeleteTask(); /* Must not delete a live runtime task. */
    assert(live_tasks == 4U && live_queues == 3U);
    isr = 1U; assert(!App_MAX30102_TryRead(&reading)); isr = 0U;
    assert(App_MAX30102_TryRead(&reading) && !reading.valid);
    assert(reading.status == MAX30102_ERROR_NOT_INITIALIZED);

    char text[] = "hello";
    assert(App_UART_TryWrite(text, 5) == 5);
    memset(text, 'x', 5U);
    UART_Message message;
    assert(xQueueReceive(uart_queue, &message, 0U) == pdPASS);
    assert(message.type == UART_MESSAGE_TEXT && message.payload.text.length == 5U);
    assert(memcmp(message.payload.text.data, "hello", 5U) == 0);
    assert(App_UART_TryWrite(NULL, 5) == -1);
    assert(App_UART_TryWrite(text, -1) == -1);
    assert(App_UART_TryWrite(text, 129) == -1);
    assert(App_UART_TryWrite(text, 0) == 0);
    isr = 1U; assert(App_UART_TryWrite(text, 5) == -1); isr = 0U;
    for (unsigned int i = 0U; i < UART_QUEUE_LENGTH; ++i)
    { assert(App_UART_TryWrite(text, 5) == 5); }
    assert(App_UART_TryWrite(text, 5) == -1);

    /* Run sampler with UART permanently full and no GUI widget calls.
     * Start near tick wrap; failure retries must not break elapsed-time logic. */
    tick = UINT32_MAX - 1500U;
    if (setjmp(sampler_exit) == 0) { tasks[0].function(NULL); assert(0); }
    assert(uart_queue->count == UART_QUEUE_LENGTH && nonzero_uart_waits == 0U);

    /* Slow GUI receives only the newest copy, never a stack pointer. */
    App_MAX30102Reading first = {.sample = {1U, 2U}, .valid = true};
    App_MAX30102Reading second = {.sample = {3U, 4U}, .valid = true};
    assert(xQueueOverwrite(reading_queue, &first) == pdPASS);
    assert(xQueueOverwrite(reading_queue, &second) == pdPASS);
    memset(&second, 0, sizeof(second));
    assert(App_MAX30102_TryRead(&reading) && reading.sample.red == 3U);

    scheduler = taskSCHEDULER_NOT_STARTED;
    assert(!App_MAX30102_TryRead(&reading));
    assert(App_UART_TryWrite(text, 5) == -1);
    App_MAX30102_DeleteTask();
    for (unsigned int i = 1U; i < 4U; ++i) { vTaskDelete(&tasks[i]); }
    vQueueDelete(uart_queue); uart_queue = NULL;
    vQueueDelete(temperature_queue); temperature_queue = NULL;
    assert(live_tasks == 0U && live_queues == 0U);
    puts("PASS: task startup/rollback, sensor retries, multi-batch/latest-copy queue,");
    puts("      ALC/overflow/brownout/stall invalidation, tick wrap, nonblocking UART");
    return 0;
}
