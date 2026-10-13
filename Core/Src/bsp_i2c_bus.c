#include "bsp_i2c_bus.h"
#include "i2c.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "stm32u5xx_ll_i2c.h"

static SemaphoreHandle_t i2c1_mutex;

HAL_StatusTypeDef BSP_I2C1_InitMutex(void)
{
    if (i2c1_mutex == NULL)
    {
        i2c1_mutex = xSemaphoreCreateMutex();
    }
    return i2c1_mutex != NULL ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef BSP_I2C1_Lock(uint32_t timeout_ms)
{
    if (__get_IPSR() != 0U)
    {
        return HAL_ERROR;
    }
    /* Peripheral initialization is single-threaded before the scheduler. */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        return HAL_OK;
    }
    if (i2c1_mutex == NULL || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return HAL_ERROR;
    }
    return xSemaphoreTake(i2c1_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE
               ? HAL_OK : HAL_TIMEOUT;
}

void BSP_I2C1_Unlock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        (void)xSemaphoreGive(i2c1_mutex);
    }
}

static HAL_StatusTypeDef WaitForFlag(uint32_t flag, uint32_t start,
                                    uint32_t timeout_ms)
{
    for (;;)
    {
        uint32_t flags = hi2c1.Instance->ISR;
        if ((flags & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) != 0U)
        {
            return HAL_ERROR;
        }
        if ((flags & flag) != 0U)
        {
            return HAL_OK;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
        {
            return HAL_TIMEOUT;
        }
    }
}

HAL_StatusTypeDef BSP_I2C1_WriteCommand(uint16_t address, uint8_t command,
                                     uint32_t timeout_ms)
{
    HAL_StatusTypeDef status = BSP_I2C1_Lock(timeout_ms);
    if (status != HAL_OK)
    {
        return status;
    }
    if (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY ||
        LL_I2C_IsActiveFlag_BUSY(hi2c1.Instance))
    {
        BSP_I2C1_Unlock();
        return HAL_BUSY;
    }

    LL_I2C_ClearFlag_STOP(hi2c1.Instance);
    LL_I2C_ClearFlag_NACK(hi2c1.Instance);
    LL_I2C_ClearFlag_BERR(hi2c1.Instance);
    LL_I2C_ClearFlag_ARLO(hi2c1.Instance);
    LL_I2C_ClearFlag_OVR(hi2c1.Instance);

    /* HAL's blocking transmit uses AUTOEND (immediate STOP). SOFTEND lets
     * us honor the SHT20 datasheet's >=20 us command-ACK-to-STOP delay.
     * All I2C1 users take the same mutex; no I2C interrupts/DMA are enabled. */
    uint32_t start = HAL_GetTick();
    LL_I2C_HandleTransfer(hi2c1.Instance, address, LL_I2C_ADDRSLAVE_7BIT, 1U,
                         LL_I2C_MODE_SOFTEND, LL_I2C_GENERATE_START_WRITE);
    status = WaitForFlag(I2C_ISR_TXIS, start, timeout_ms);
    if (status == HAL_OK)
    {
        LL_I2C_TransmitData8(hi2c1.Instance, command);
        status = WaitForFlag(I2C_ISR_TC, start, timeout_ms);
    }
    if (status == HAL_OK)
    {
        /* TIM6 supplies HAL time. A 1 ms wait safely exceeds 20 us without
         * reserving another timer or changing the board clock configuration. */
        HAL_Delay(1U);
    }

    /* NACK may already have generated STOP. Never force STOP after losing
     * arbitration. Bound cleanup even when a disconnected device holds SDA. */
    if (!LL_I2C_IsActiveFlag_ARLO(hi2c1.Instance) &&
        !LL_I2C_IsActiveFlag_STOP(hi2c1.Instance))
    {
        LL_I2C_GenerateStopCondition(hi2c1.Instance);
        start = HAL_GetTick();
        while (!LL_I2C_IsActiveFlag_STOP(hi2c1.Instance))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
            {
                status = HAL_TIMEOUT;
                break;
            }
        }
    }
    LL_I2C_ClearFlag_STOP(hi2c1.Instance);
    LL_I2C_ClearFlag_NACK(hi2c1.Instance);
    LL_I2C_ClearFlag_BERR(hi2c1.Instance);
    LL_I2C_ClearFlag_ARLO(hi2c1.Instance);
    LL_I2C_ClearFlag_OVR(hi2c1.Instance);
    I2C_RESET_CR2(&hi2c1);
    SET_BIT(hi2c1.Instance->ISR, I2C_ISR_TXE); /* Flush an unsent command. */
    BSP_I2C1_Unlock();
    return status;
}
