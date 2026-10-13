/* Compile the actual driver and task module against a simulated HAL/RTOS.
 * Does not emulate Cortex-M context switches or electrical I2C timing. */
#include "bsp_sht20.h"
#include "bsp_i2c_bus.h"
#include "app_freertos.h"
#include "task.h"
#include "queue.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
static int scheduler;
static uint32_t tick_ms;
static int bus_locked;
static uint8_t last_command;
static uint8_t user_register;
static uint8_t temperature_frame[3];
static uint8_t humidity_frame[3];
static HAL_StatusTypeDef command_result;
static HAL_StatusTypeDef receive_result;
static HAL_StatusTypeDef lock_result;
static HAL_StatusTypeDef register_read_result;
static HAL_StatusTypeDef register_write_result;
static unsigned int reset_count;
static unsigned int receives;
static unsigned int register_writes;
static unsigned int application_calls;
static uint32_t ipsr;
static unsigned char items[8][256];
static unsigned int queue_count;
static unsigned int queue_size;
static unsigned int queue_capacity;
static TaskFunction_t tasks[3];
static unsigned int task_count;
static jmp_buf stop_task;
static char uart_output[2048];
static size_t uart_length;
static unsigned int recovery_cycles;

/* Independent polynomial long division for constructing test frames. */
static void Frame(uint16_t raw, uint8_t output[3])
{
    uint32_t remainder = (uint32_t)raw << 8;
    for (int bit = 23; bit >= 8; --bit)
    {
        if ((remainder & (1UL << bit)) != 0U)
        {
            remainder ^= 0x131UL << (bit - 8);
        }
    }
    output[0] = (uint8_t)(raw >> 8);
    output[1] = (uint8_t)raw;
    output[2] = (uint8_t)remainder;
}

static void ResetSensor(void)
{
    assert(!bus_locked);
    command_result = HAL_OK;
    receive_result = HAL_OK;
    lock_result = HAL_OK;
    register_read_result = HAL_OK;
    register_write_result = HAL_OK;
    user_register = 0x3EU;
    receives = 0U;
    register_writes = 0U;
    Frame(0x6838U, temperature_frame);
    Frame(0x6352U, humidity_frame);
    /* Known example from the SHT20 datasheet: 0x63 0x52 -> 0x64. */
    assert(humidity_frame[2] == 0x64U);
}

uint32_t __get_IPSR(void) { return ipsr; }
uint32_t HAL_GetTick(void) { return tick_ms; }
void HAL_Delay(uint32_t ms) { assert(!bus_locked); tick_ms += ms; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler; }
TickType_t xTaskGetTickCount(void) { return tick_ms; }
void vTaskDelay(TickType_t ticks)
{
    assert(!bus_locked); /* Measurement conversion must not monopolize I2C1. */
    tick_ms += ticks;
    if (ticks == 1U) { longjmp(stop_task, 1); } /* Stop application after one loop. */
}
BaseType_t xTaskDelayUntil(TickType_t *previous, TickType_t interval)
{
    assert(interval == 2000U);
    *previous += interval;
    tick_ms = *previous;
    /* Keep one invocation of the sensor task alive across success, CRC error,
     * and recovery, so its initialized flag must actually survive the loop. */
    if (recovery_cycles > 1U)
    {
        if (recovery_cycles == 3U) { humidity_frame[2] ^= 1U; }
        else { ResetSensor(); }
        --recovery_cycles;
        return pdTRUE;
    }
    longjmp(stop_task, 1); /* Stop sensor task after publishing one sample. */
    return pdTRUE;
}
HAL_StatusTypeDef BSP_I2C1_InitMutex(void) { return HAL_OK; }
HAL_StatusTypeDef BSP_I2C1_Lock(uint32_t timeout)
{
    assert(timeout != 0U && !bus_locked);
    if (lock_result == HAL_OK) { bus_locked = 1; }
    return lock_result;
}
void BSP_I2C1_Unlock(void) { assert(bus_locked); bus_locked = 0; }
HAL_StatusTypeDef BSP_I2C1_WriteCommand(uint16_t address, uint8_t command, uint32_t timeout)
{
    assert(address == 0x80U && timeout != 0U && !bus_locked);
    assert(command == 0xFEU || command == 0xF3U || command == 0xF5U);
    if (command == 0xFEU) { ++reset_count; }
    last_command = command;
    return command_result;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *bus, uint16_t address,
    uint16_t reg, uint16_t reg_size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(bus == &hi2c1 && address == 0x80U && reg == 0xE7U);
    assert(reg_size == 1U && length == 1U && timeout > 0U && bus_locked);
    *data = user_register;
    return register_read_result;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *bus, uint16_t address,
    uint16_t reg, uint16_t reg_size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(bus == &hi2c1 && address == 0x80U && reg == 0xE6U);
    assert(reg_size == 1U && length == 1U && timeout > 0U && bus_locked);
    ++register_writes;
    user_register = *data;
    return register_write_result;
}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *bus, uint16_t address,
    uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(bus == &hi2c1 && address == 0x80U && length == 3U && timeout > 0U);
    assert(bus_locked && (last_command == 0xF3U || last_command == 0xF5U));
    ++receives;
    memcpy(data, last_command == 0xF3U ? temperature_frame : humidity_frame, 3U);
    return receive_result;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, const uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
    assert(uart == &huart1 && timeout != 0U);
    assert(uart_length + length < sizeof(uart_output));
    memcpy(uart_output + uart_length, data, length);
    uart_length += length;
    uart_output[uart_length] = '\0';
    return HAL_OK;
}
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size)
{
    assert(length == 8U && size <= sizeof(items[0]));
    queue_size = size;
    queue_capacity = length;
    return items;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    assert(queue == items && wait > 0U);
    if (queue_count == queue_capacity) { return pdFALSE; }
    memcpy(items[queue_count++], item, queue_size);
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    assert(queue == items && wait == portMAX_DELAY);
    if (queue_count == 0U) { longjmp(stop_task, 1); }
    memcpy(item, items[0], queue_size);
    --queue_count;
    memmove(items, items + 1, queue_count * sizeof(items[0]));
    return pdPASS;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue == items); queue_count = 0U; }
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, size_t stack,
                       void *argument, UBaseType_t priority, TaskHandle_t *handle)
{
    assert(function != NULL && name != NULL && stack >= 512U && argument == NULL);
    assert(priority > 0U && task_count < 3U);
    tasks[task_count] = function;
    *handle = &tasks[task_count++];
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(task != NULL); }
void vTaskStartScheduler(void) { scheduler = taskSCHEDULER_RUNNING; longjmp(stop_task, 1); }
void Error_Handler(void) { assert(!"Unexpected FreeRTOS failure hook"); }
static void ApplicationProcess(void) { ++application_calls; }
static void RunTask(unsigned int task)
{
    if (setjmp(stop_task) == 0) { tasks[task](NULL); }
}

int main(void)
{
    SHT20_Data sample = {123, 456};
    ResetSensor();
    assert(SHT20_Read(NULL) == SHT20_ERROR_ARGUMENT);
    assert(SHT20_Init() == SHT20_OK);
    assert(user_register == 0x3AU && register_writes == 1U && !bus_locked);
    assert(SHT20_Read(&sample) == SHT20_OK);
    assert(sample.temperature_centi_c == 2469 && sample.humidity_centi_pct == 4249U);

    Frame(0x4000U, temperature_frame);
    assert(SHT20_Read(&sample) == SHT20_OK && sample.temperature_centi_c == -292);
    Frame(0x0002U, humidity_frame);
    assert(SHT20_Read(&sample) == SHT20_OK && sample.humidity_centi_pct == 0U);
    Frame(0xFFFEU, humidity_frame);
    assert(SHT20_Read(&sample) == SHT20_OK && sample.humidity_centi_pct == 10000U);

    ResetSensor();
    sample.temperature_centi_c = 123;
    sample.humidity_centi_pct = 456U;
    temperature_frame[2] ^= 1U;
    assert(SHT20_Read(&sample) == SHT20_ERROR_CRC && receives == 1U);
    assert(sample.temperature_centi_c == 123 && sample.humidity_centi_pct == 456U);
    ResetSensor();
    humidity_frame[2] ^= 1U;
    assert(SHT20_Read(&sample) == SHT20_ERROR_CRC && receives == 2U);
    assert(sample.temperature_centi_c == 123 && sample.humidity_centi_pct == 456U);
    ResetSensor();
    Frame(0x683AU, temperature_frame);
    assert(SHT20_Read(&sample) == SHT20_ERROR_DATA_TYPE);

    ResetSensor();
    command_result = HAL_ERROR;
    assert(SHT20_Init() == SHT20_ERROR_I2C);
    assert(SHT20_Read(&sample) == SHT20_ERROR_I2C && receives == 0U);
    ResetSensor();
    receive_result = HAL_TIMEOUT;
    assert(SHT20_Read(&sample) == SHT20_ERROR_TIMEOUT && !bus_locked);
    ResetSensor();
    lock_result = HAL_TIMEOUT;
    assert(SHT20_Read(&sample) == SHT20_ERROR_TIMEOUT && !bus_locked);
    assert(SHT20_Init() == SHT20_ERROR_TIMEOUT && !bus_locked);
    ResetSensor();
    register_read_result = HAL_ERROR;
    assert(SHT20_Init() == SHT20_ERROR_I2C && !bus_locked && register_writes == 0U);
    ResetSensor();
    register_write_result = HAL_TIMEOUT;
    assert(SHT20_Init() == SHT20_ERROR_TIMEOUT && !bus_locked);

    ResetSensor();
    assert(App_UART_Write("boot\r\n", 6) == 6 && uart_length == 6U);
    uart_length = 0U;
    if (setjmp(stop_task) == 0) { (void)App_FreeRTOS_Start(ApplicationProcess); }
    assert(task_count == 3U);
    RunTask(1U); /* Sensor measures and copies its stack-local message to queue. */
    assert(queue_count == 1U && uart_length == 0U);
    RunTask(0U); /* Printer receives the message after producer has returned. */
    assert(strstr(uart_output, "T=24.69 C RH=42.49 %") != NULL);
    uart_length = 0U;
    Frame(0x4000U, temperature_frame);
    RunTask(1U);
    RunTask(0U);
    assert(strstr(uart_output, "T=-2.92 C") != NULL);
    uart_length = 0U;
    humidity_frame[2] ^= 1U;
    RunTask(1U);
    RunTask(0U);
    assert(strstr(uart_output, "ERROR: CRC mismatch") != NULL);
    assert(strstr(uart_output, "T=") == NULL);
    ResetSensor();
    unsigned int resets_before = reset_count;
    uart_length = 0U;
    RunTask(1U);
    assert(reset_count == resets_before + 1U);
    RunTask(0U);
    assert(strstr(uart_output, "T=24.69 C") != NULL);

    ResetSensor();
    uart_length = 0U;
    resets_before = reset_count;
    recovery_cycles = 3U;
    RunTask(1U);
    assert(queue_count == 3U && reset_count == resets_before + 2U);
    RunTask(0U);
    assert(strstr(uart_output, "#1 ") != NULL && strstr(uart_output, "#3 ") != NULL);
    assert(strstr(uart_output, "ERROR: CRC mismatch") != NULL);
    assert(strstr(strstr(uart_output, "#3 "), "T=24.69 C") != NULL);
    recovery_cycles = 0U;

    uart_length = 0U;
    char text[301];
    memset(text, 'X', sizeof(text));
    assert(App_UART_Write(text, 300) == 300 && queue_count == 3U && uart_length == 0U);
    memset(text, 'Y', sizeof(text)); /* Verify messages own their bytes. */
    RunTask(0U);
    assert(uart_length == 300U && uart_output[0] == 'X' && uart_output[299] == 'X');
    queue_count = queue_capacity;
    assert(App_UART_Write("full", 4) == -1);
    queue_count = 0U;
    scheduler = taskSCHEDULER_SUSPENDED;
    assert(App_UART_Write("suspended", 9) == -1);
    scheduler = taskSCHEDULER_RUNNING;
    ipsr = 5U;
    assert(App_UART_Write("isr", 3) == -1);
    ipsr = 0U;
    RunTask(2U);
    assert(application_calls == 1U);
    puts("PASS: SHT20 conversion/CRC/errors, bus release, task-to-queue-to-UART, recovery, log copying and backpressure");
    return 0;
}
