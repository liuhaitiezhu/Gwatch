#include "bsp_sht20.h"
#include "bsp_i2c_bus.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"

#define SHT20_CMD_RESET          0xFEU
#define SHT20_CMD_READ_USER      0xE7U
#define SHT20_CMD_WRITE_USER     0xE6U
#define SHT20_CMD_TEMPERATURE    0xF3U
#define SHT20_CMD_HUMIDITY       0xF5U
#define SHT20_I2C_TIMEOUT_MS     20U

static void DelayMs(uint32_t milliseconds)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        /* Add a tick so a call just before the next tick cannot wait too little. */
        vTaskDelay(pdMS_TO_TICKS(milliseconds) + 1U);
    }
    else
    {
        HAL_Delay(milliseconds);
    }
}

static SHT20_Status ConvertStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return SHT20_OK;
    }
    return status == HAL_TIMEOUT ? SHT20_ERROR_TIMEOUT : SHT20_ERROR_I2C;
}

/* SHT2x CRC-8: polynomial x^8+x^5+x^4+1, initial value 0, no final XOR. */
static uint8_t CalculateCRC(const uint8_t bytes[2])
{
    uint8_t crc = 0U;
    for (uint32_t i = 0U; i < 2U; ++i)
    {
        crc ^= bytes[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
        {
            uint8_t shifted = (uint8_t)(crc << 1);
            crc = (uint8_t)(shifted ^ ((crc & 0x80U) != 0U ? 0x31U : 0U));
        }
    }
    return crc;
}

SHT20_Status SHT20_Init(void)
{
    DelayMs(15U); /* Also supports initialization immediately after power-up. */
    HAL_StatusTypeDef status = BSP_I2C1_WriteCommand(
        SHT20_I2C_ADDRESS, SHT20_CMD_RESET, SHT20_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    DelayMs(15U);

    status = BSP_I2C1_Lock(SHT20_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    uint8_t user_register = 0U;
    status = HAL_I2C_Mem_Read(&hi2c1, SHT20_I2C_ADDRESS, SHT20_CMD_READ_USER,
                              I2C_MEMADD_SIZE_8BIT, &user_register, 1U,
                              SHT20_I2C_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        /* Preserve reserved bits, select 14-bit T / 12-bit RH, disable heater.
         * Soft reset alone does not clear the heater bit. Keep OTP reload off. */
        user_register = (uint8_t)((user_register & ~0x85U) | 0x02U);
        status = HAL_I2C_Mem_Write(&hi2c1, SHT20_I2C_ADDRESS, SHT20_CMD_WRITE_USER,
                                   I2C_MEMADD_SIZE_8BIT, &user_register, 1U,
                                   SHT20_I2C_TIMEOUT_MS);
    }
    BSP_I2C1_Unlock();
    return ConvertStatus(status);
}

static SHT20_Status ReadRaw(uint8_t command, uint32_t conversion_ms, uint16_t *raw)
{
    HAL_StatusTypeDef status = BSP_I2C1_WriteCommand(
        SHT20_I2C_ADDRESS, command, SHT20_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }

    /* No-hold measurement: the touch controller can use I2C1 during this wait.
     * 90/30 ms exceed datasheet maxima of 85 ms (T) and 29 ms (RH). */
    DelayMs(conversion_ms);
    status = BSP_I2C1_Lock(SHT20_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    uint8_t bytes[3];
    status = HAL_I2C_Master_Receive(&hi2c1, SHT20_I2C_ADDRESS, bytes,
                                    sizeof(bytes), SHT20_I2C_TIMEOUT_MS);
    BSP_I2C1_Unlock();
    if (status != HAL_OK)
    {
        return ConvertStatus(status);
    }
    /* Check the received status bits as part of CRC before masking them out. */
    if (CalculateCRC(bytes) != bytes[2])
    {
        return SHT20_ERROR_CRC;
    }
    uint8_t expected_type = command == SHT20_CMD_HUMIDITY ? 0x02U : 0x00U;
    if ((bytes[1] & 0x02U) != expected_type)
    {
        return SHT20_ERROR_DATA_TYPE;
    }
    *raw = (uint16_t)(((uint16_t)bytes[0] << 8 | bytes[1]) & 0xFFFCU);
    return SHT20_OK;
}

SHT20_Status SHT20_Read(SHT20_Data *data)
{
    if (data == NULL)
    {
        return SHT20_ERROR_ARGUMENT;
    }
    uint16_t temperature_raw;
    uint16_t humidity_raw;
    SHT20_Status status = ReadRaw(SHT20_CMD_TEMPERATURE, 90U, &temperature_raw);
    if (status != SHT20_OK)
    {
        return status;
    }
    status = ReadRaw(SHT20_CMD_HUMIDITY, 30U, &humidity_raw);
    if (status != SHT20_OK)
    {
        return status;
    }

    /* Datasheet equations in hundredths, rounded to the nearest 0.01 unit.
     * Maximum intermediate value fits uint32_t; printf needs no float support. */
    data->temperature_centi_c =
        (int32_t)((17572UL * temperature_raw + 32768UL) / 65536UL) - 4685;
    int32_t humidity = (int32_t)((12500UL * humidity_raw + 32768UL) / 65536UL) - 600;
    if (humidity < 0)
    {
        humidity = 0;
    }
    else if (humidity > 10000)
    {
        humidity = 10000;
    }
    data->humidity_centi_pct = (uint32_t)humidity;
    return SHT20_OK;
}

const char *SHT20_StatusString(SHT20_Status status)
{
    switch (status)
    {
        case SHT20_OK:              return "OK";
        case SHT20_ERROR_ARGUMENT:  return "invalid argument";
        case SHT20_ERROR_I2C:       return "I2C error (check wiring/address/power)";
        case SHT20_ERROR_TIMEOUT:   return "I2C timeout";
        case SHT20_ERROR_CRC:       return "CRC mismatch";
        case SHT20_ERROR_DATA_TYPE: return "unexpected measurement type";
        default:                   return "unknown error";
    }
}
