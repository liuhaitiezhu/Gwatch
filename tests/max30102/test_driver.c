#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "bsp_max30102.h"
#include "bsp_i2c_bus.h"
#include "i2c.h"
#include "task.h"

I2C_HandleTypeDef hi2c1;
static uint8_t regs[256];
static uint8_t fifo[32][6];
static uint32_t tick;
static bool locked, stuck_reset;
static unsigned int reset_reads, fifo_reads, transactions;
static HAL_StatusTypeDef lock_failure, transaction_failure;
static int failure_reg = -1;

uint32_t HAL_GetTick(void) { return tick; }
void vTaskDelay(TickType_t delay) { assert(!locked); tick += delay; }
HAL_StatusTypeDef BSP_I2C1_Lock(uint32_t timeout)
{
    assert(!locked && timeout != 0U);
    if (lock_failure != HAL_OK) { return lock_failure; }
    locked = true;
    return HAL_OK;
}
void BSP_I2C1_Unlock(void) { assert(locked); locked = false; }

static HAL_StatusTypeDef CheckTransfer(I2C_HandleTypeDef *bus, uint16_t address,
    uint16_t reg, uint16_t size, uint32_t timeout)
{
    assert(locked && bus == &hi2c1 && address == (0x57U << 1));
    assert(size == I2C_MEMADD_SIZE_8BIT && timeout != 0U);
    ++transactions;
    return failure_reg == (int)reg ? transaction_failure : HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *bus, uint16_t address,
    uint16_t reg, uint16_t size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    HAL_StatusTypeDef result = CheckTransfer(bus, address, reg, size, timeout);
    if (reg == 0x07U)
    {
        assert(length % 6U == 0U && length <= 48U);
        ++fifo_reads;
        /* Deliberately advance RD_PTR even on failure, like an interrupted read. */
        for (unsigned int i = 0; i < length / 6U; ++i)
        {
            memcpy(data + i * 6U, fifo[regs[6]], 6U);
            regs[6] = (regs[6] + 1U) & 31U;
        }
        regs[5] = 0U;
        return result;
    }
    if (result != HAL_OK) { return result; }
    if (reg == 0x09U && (regs[9] & 0x40U) != 0U && !stuck_reset)
    {
        if (++reset_reads >= 2U) { regs[9] = 0U; }
    }
    assert(reg + length <= sizeof(regs));
    memcpy(data, regs + reg, length);
    if (reg == 0U) { regs[0] = 0U; regs[1] = 0U; }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *bus, uint16_t address,
    uint16_t reg, uint16_t size, uint8_t *data, uint16_t length, uint32_t timeout)
{
    HAL_StatusTypeDef result = CheckTransfer(bus, address, reg, size, timeout);
    if (result != HAL_OK) { return result; }
    if (reg == 9U && data[0] == 0x40U)
    {
        uint8_t part = regs[255];
        memset(regs, 0, sizeof(regs));
        regs[255] = part;
        reset_reads = 0U;
    }
    memcpy(regs + reg, data, length);
    return HAL_OK;
}

static void ResetMock(void)
{
    assert(!locked);
    memset(regs, 0, sizeof(regs));
    regs[255] = 0x15U;
    tick = 0U;
    stuck_reset = false;
    lock_failure = transaction_failure = HAL_OK;
    failure_reg = -1;
    fifo_reads = transactions = 0U;
}

static void Fill(unsigned int read, unsigned int count)
{
    regs[6] = (uint8_t)read;
    regs[4] = (read + count) & 31U;
    regs[5] = 0U;
    regs[0] = count == 32U ? 0x80U : 0U;
    for (unsigned int i = 0U; i < count; ++i)
    {
        /* Known Red=0x12300+i, IR=0x23400+i, with reserved top bits set. */
        uint8_t *p = fifo[(read + i) & 31U];
        p[0] = 0xFDU; p[1] = 0x23U; p[2] = (uint8_t)i;
        p[3] = 0xFEU; p[4] = 0x34U; p[5] = (uint8_t)i;
    }
}

int main(void)
{
    MAX30102_Sample samples[8];
    MAX30102_FIFOInfo info;
    ResetMock();
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_NOT_INITIALIZED);
    assert(MAX30102_ReadFIFO(NULL, 8U, &info) == MAX30102_ERROR_ARGUMENT);
    assert(MAX30102_ReadFIFO(samples, 0U, &info) == MAX30102_ERROR_ARGUMENT);
    assert(MAX30102_ReadFIFO(samples, 9U, &info) == MAX30102_ERROR_ARGUMENT);
    assert(MAX30102_ReadFIFO(samples, 8U, NULL) == MAX30102_ERROR_ARGUMENT);
    assert(transactions == 0U);

    assert(MAX30102_Init() == MAX30102_OK);
    assert(tick >= 3U && !locked); /* Reset polling yielded without bus lock. */
    assert(regs[2] == 0xA0U && regs[3] == 0U && regs[8] == 0U);
    assert(regs[9] == 3U && regs[10] == 0x27U);
    assert(regs[12] == MAX30102_RED_LED_CURRENT && regs[13] == MAX30102_IR_LED_CURRENT);

    memset(samples, 0xA5, sizeof(samples));
    MAX30102_Sample sentinel = samples[0];
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_OK);
    assert(info.count == 0U && fifo_reads == 0U);
    assert(memcmp(&samples[0], &sentinel, sizeof(sentinel)) == 0);

    Fill(30U, 5U); /* Both pointer wrap and red/IR order. */
    assert(MAX30102_ReadFIFO(samples, 3U, &info) == MAX30102_OK);
    assert(info.count == 3U && regs[6] == 1U);
    assert(samples[0].red == 0x12300U && samples[0].ir == 0x23400U);
    assert(samples[2].red == 0x12302U && samples[2].ir == 0x23402U);
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_OK);
    assert(info.count == 2U && samples[1].red == 0x12304U);

    Fill(7U, 32U); /* Equal pointers when FULL must not be mistaken for empty. */
    for (unsigned int i = 0U; i < 4U; ++i)
    {
        assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_OK);
        assert(info.count == 8U && samples[0].red == 0x12300U + i * 8U);
    }
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_OK && info.count == 0U);

    Fill(0U, 2U); regs[0] = 0x20U;
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_OK);
    assert(info.count == 2U && (info.interrupt_status & MAX30102_INT_ALC_OVERFLOW));

    Fill(0U, 32U); regs[5] = 31U;
    unsigned int reads_before = fifo_reads;
    sentinel = samples[0];
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_FIFO_OVERFLOW);
    assert(info.count == 0U && info.lost_samples == 31U && fifo_reads == reads_before);
    assert(memcmp(&samples[0], &sentinel, sizeof(sentinel)) == 0);
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_NOT_INITIALIZED);
    assert(MAX30102_Init() == MAX30102_OK);
    regs[0] = 1U;
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_POWER_RESET && !locked);

    assert(MAX30102_Init() == MAX30102_OK);
    Fill(0U, 2U); failure_reg = 7; transaction_failure = HAL_TIMEOUT;
    sentinel = samples[0];
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_TIMEOUT);
    assert(info.count == 0U && !locked && regs[6] == 2U);
    assert(memcmp(&samples[0], &sentinel, sizeof(sentinel)) == 0);
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_NOT_INITIALIZED);

    ResetMock(); regs[255] = 0U;
    assert(MAX30102_Init() == MAX30102_ERROR_DEVICE_ID && !locked);
    ResetMock(); stuck_reset = true;
    assert(MAX30102_Init() == MAX30102_ERROR_RESET_TIMEOUT && tick >= 100U && tick < 110U);
    assert(!locked);
    ResetMock(); lock_failure = HAL_TIMEOUT;
    assert(MAX30102_Init() == MAX30102_ERROR_TIMEOUT && !locked && transactions == 0U);
    ResetMock(); failure_reg = 0x0C; transaction_failure = HAL_ERROR;
    assert(MAX30102_Init() == MAX30102_ERROR_I2C && regs[9] == 0U && !locked);
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_NOT_INITIALIZED);

    ResetMock(); assert(MAX30102_Init() == MAX30102_OK);
    failure_reg = 0; transaction_failure = HAL_BUSY;
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_I2C && !locked);
    ResetMock(); assert(MAX30102_Init() == MAX30102_OK);
    lock_failure = HAL_TIMEOUT;
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_TIMEOUT && !locked);
    lock_failure = HAL_OK;
    assert(MAX30102_Shutdown() == MAX30102_OK && regs[9] == 0x83U);
    assert(MAX30102_ReadFIFO(samples, 8U, &info) == MAX30102_ERROR_NOT_INITIALIZED);
    assert(MAX30102_Init() == MAX30102_OK);
    puts("PASS: MAX30102 ID/reset/config, FIFO empty/full/wrap/batching/18-bit order,");
    puts("      overflow/brownout/partial-read errors, lock release, shutdown/restart");
    return 0;
}
