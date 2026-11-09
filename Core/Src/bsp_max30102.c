#include "bsp_max30102.h"
#include "bsp_i2c_bus.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>

/* Analog Devices MAX30102 data sheet, Rev 1, register descriptions pp. 10-20:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf */
#define REG_INT_STATUS_1      0x00U
#define REG_INT_ENABLE_1      0x02U
#define REG_FIFO_WR_PTR       0x04U
#define REG_FIFO_DATA         0x07U
#define REG_FIFO_CONFIG       0x08U
#define REG_MODE_CONFIG       0x09U
#define REG_SPO2_CONFIG       0x0AU
#define REG_LED1_PA           0x0CU
#define REG_PART_ID           0xFFU
#define EXPECTED_PART_ID      0x15U
#define MODE_RESET            0x40U
#define MODE_SHUTDOWN         0x80U
#define MODE_RED_IR           0x03U
#define SPO2_100HZ_18BIT       0x27U
#define I2C_TIMEOUT_MS        20U
#define RESET_TIMEOUT_MS      100U

static bool initialized;

static MAX30102_Status ConvertStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return MAX30102_OK;
    }
    return status == HAL_TIMEOUT ? MAX30102_ERROR_TIMEOUT : MAX30102_ERROR_I2C;
}

static MAX30102_Status ReadRegisters(uint8_t reg, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status = BSP_I2C1_Lock(I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    status = HAL_I2C_Mem_Read(&hi2c1, MAX30102_I2C_ADDRESS, reg,
                               I2C_MEMADD_SIZE_8BIT, data, length, I2C_TIMEOUT_MS);
    BSP_I2C1_Unlock();
    return ConvertStatus(status);
}

static MAX30102_Status WriteRegisters(uint8_t reg, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status = BSP_I2C1_Lock(I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    status = HAL_I2C_Mem_Write(&hi2c1, MAX30102_I2C_ADDRESS, reg,
                                I2C_MEMADD_SIZE_8BIT, data, length, I2C_TIMEOUT_MS);
    BSP_I2C1_Unlock();
    return ConvertStatus(status);
}

static MAX30102_Status WriteRegister(uint8_t reg, uint8_t value)
{
    return WriteRegisters(reg, &value, 1U);
}

MAX30102_Status MAX30102_Init(void)
{
    initialized = false;
    uint8_t value = 0U;
    MAX30102_Status status = ReadRegisters(REG_PART_ID, &value, 1U);
    if (status != MAX30102_OK)
    {
        return status;
    }
    if (value != EXPECTED_PART_ID)
    {
        return MAX30102_ERROR_DEVICE_ID;
    }
    status = WriteRegister(REG_MODE_CONFIG, MODE_RESET);
    if (status != MAX30102_OK)
    {
        return status;
    }
    uint32_t start = HAL_GetTick();
    for (;;)
    {
        status = ReadRegisters(REG_MODE_CONFIG, &value, 1U);
        if (status != MAX30102_OK)
        {
            return status;
        }
        if ((value & MODE_RESET) == 0U)
        {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= RESET_TIMEOUT_MS)
        {
            return MAX30102_ERROR_RESET_TIMEOUT;
        }
        /* Never hold I2C1 while waiting for the chip's self-clearing reset. */
        vTaskDelay(pdMS_TO_TICKS(2U) + 1U);
    }

    /* Configure while stopped. No writes to reserved registers. A_FULL is set
     * at 32 samples so equal pointers can be distinguished from an empty FIFO.
     * The open-drain INT line is unused by the MCU in this polling version. */
    uint8_t enables[2] = {MAX30102_INT_FIFO_FULL | MAX30102_INT_ALC_OVERFLOW, 0U};
    uint8_t pointers[3] = {0U, 0U, 0U};
    uint8_t currents[2] = {MAX30102_RED_LED_CURRENT, MAX30102_IR_LED_CURRENT};
    uint8_t pending[2];
    status = WriteRegisters(REG_INT_ENABLE_1, enables, sizeof(enables));
    if (status == MAX30102_OK)
    {
        status = WriteRegisters(REG_FIFO_WR_PTR, pointers, sizeof(pointers));
    }
    if (status == MAX30102_OK)
    {
        status = WriteRegister(REG_FIFO_CONFIG, 0x00U);
    }
    if (status == MAX30102_OK)
    {
        status = WriteRegister(REG_SPO2_CONFIG, SPO2_100HZ_18BIT);
    }
    if (status == MAX30102_OK)
    {
        status = WriteRegisters(REG_LED1_PA, currents, sizeof(currents));
    }
    if (status == MAX30102_OK)
    {
        status = ReadRegisters(REG_INT_STATUS_1, pending, sizeof(pending));
    }
    if (status == MAX30102_OK)
    {
        status = WriteRegister(REG_MODE_CONFIG, MODE_RED_IR);
    }
    initialized = status == MAX30102_OK;
    return status;
}

MAX30102_Status MAX30102_ReadFIFO(MAX30102_Sample *samples, uint8_t capacity,
                                  MAX30102_FIFOInfo *info)
{
    if (info != NULL)
    {
        *info = (MAX30102_FIFOInfo){0};
    }
    if (samples == NULL || info == NULL || capacity == 0U ||
        capacity > MAX30102_FIFO_BATCH_SIZE)
    {
        return MAX30102_ERROR_ARGUMENT;
    }
    if (!initialized)
    {
        return MAX30102_ERROR_NOT_INITIALIZED;
    }
    HAL_StatusTypeDef hal_status = BSP_I2C1_Lock(I2C_TIMEOUT_MS);
    if (hal_status != HAL_OK)
    {
        initialized = false;
        return ConvertStatus(hal_status);
    }

    /* Read status before popping any data: FIFO reads clear the overflow
     * counter. Registers 0x00..0x06 include status, enables and all pointers.
     * The sensor keeps sampling; a snapshot may conservatively undercount. */
    uint8_t snapshot[7];
    uint8_t bytes[MAX30102_FIFO_BATCH_SIZE * 6U];
    uint8_t count = 0U;
    MAX30102_Status status;
    hal_status = HAL_I2C_Mem_Read(&hi2c1, MAX30102_I2C_ADDRESS, REG_INT_STATUS_1,
        I2C_MEMADD_SIZE_8BIT, snapshot, sizeof(snapshot), I2C_TIMEOUT_MS);
    status = ConvertStatus(hal_status);
    if (status == MAX30102_OK)
    {
        info->interrupt_status = snapshot[0];
        info->lost_samples = snapshot[5] & 0x1FU;
        if ((snapshot[0] & MAX30102_INT_POWER_READY) != 0U)
        {
            status = MAX30102_ERROR_POWER_RESET;
        }
        else if (info->lost_samples != 0U)
        {
            /* Discard discontinuous data by requiring a fresh Init. */
            status = MAX30102_ERROR_FIFO_OVERFLOW;
        }
        else
        {
            count = (uint8_t)((snapshot[4] - snapshot[6]) & 0x1FU);
            if (count == 0U && (snapshot[0] & MAX30102_INT_FIFO_FULL) != 0U)
            {
                count = MAX30102_FIFO_DEPTH;
            }
            if (count > capacity)
            {
                count = capacity;
            }
            if (count != 0U)
            {
                hal_status = HAL_I2C_Mem_Read(&hi2c1, MAX30102_I2C_ADDRESS,
                    REG_FIFO_DATA, I2C_MEMADD_SIZE_8BIT, bytes,
                    (uint16_t)(count * 6U), I2C_TIMEOUT_MS);
                status = ConvertStatus(hal_status);
            }
        }
    }
    BSP_I2C1_Unlock();
    if (status != MAX30102_OK)
    {
        /* A partial FIFO read may have advanced RD_PTR. Do not expose a
         * partial batch or retry it as though sample continuity survived. */
        initialized = false;
        return status;
    }
    for (uint8_t i = 0U; i < count; ++i)
    {
        const uint8_t *p = &bytes[i * 6U];
        samples[i].red = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & 0x3FFFFU;
        samples[i].ir = (((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & 0x3FFFFU;
    }
    info->count = count;
    return MAX30102_OK;
}

MAX30102_Status MAX30102_Shutdown(void)
{
    /* Also permits stopping a device after a failed FIFO transaction. */
    initialized = false;
    return WriteRegister(REG_MODE_CONFIG, MODE_SHUTDOWN | MODE_RED_IR);
}

const char *MAX30102_StatusString(MAX30102_Status status)
{
    switch (status)
    {
        case MAX30102_OK:                    return "OK";
        case MAX30102_ERROR_ARGUMENT:        return "invalid argument";
        case MAX30102_ERROR_NOT_INITIALIZED: return "not initialized";
        case MAX30102_ERROR_I2C:             return "I2C error";
        case MAX30102_ERROR_TIMEOUT:         return "I2C timeout";
        case MAX30102_ERROR_DEVICE_ID:       return "unexpected part ID";
        case MAX30102_ERROR_RESET_TIMEOUT:   return "reset timeout";
        case MAX30102_ERROR_FIFO_OVERFLOW:   return "FIFO overflow (samples lost)";
        case MAX30102_ERROR_POWER_RESET:     return "power reset detected";
        case MAX30102_ERROR_SAMPLE_TIMEOUT:  return "no samples for 500 ms";
        default:                            return "unknown error";
    }
}
