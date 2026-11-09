/* FT6336：采集按下状态和坐标，点击及滑动识别交给 TouchGFX。 */
#include "bsp_ft6336.h"
#include "i2c.h"
#include "bsp_i2c_bus.h"
#include <stdint.h>
#include <stdio.h>

#define FT6336_I2C_ADDRESS          (0x38U << 1)
#define FT6336_REG_TOUCH            0x02U
#define FT6336_REG_INTERRUPT_MODE   0xA4U
#define FT6336_REG_GESTURE_MODE     0xD0U

#define FT6336_TOUCH_POLL_MS          20U
#define FT6336_I2C_ERROR_REPORT_MS  1000U

typedef enum
{
    TOUCH_IDLE,
    TOUCH_TRACKING,
    TOUCH_IGNORED    /* 双指或数据异常，等待本次触摸结束。 */
} FT6336_TouchState;

static struct
{
    FT6336_TouchState state;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    bool pressed;
} touch;

static struct
{
    bool poll_started;
    bool interrupt_mode_confirmed;
    bool error_reported;
    uint32_t last_poll_tick;
    uint32_t last_error_tick;
} task;

static bool FT6336_read(uint8_t reg, uint8_t *data, uint16_t size);
static bool FT6336_write(uint8_t reg, uint8_t value);
static void FT6336_report_error(const char *operation, uint8_t reg,
                                HAL_StatusTypeDef status, uint32_t error);

void FT6336_init(void)
{
    touch.state = TOUCH_IDLE;
    touch.pressed = false;
    task.poll_started = false;
    task.error_reported = false;

    /* RESET 与 LCD 共用 PA8，LCD 初始化后只等待触摸启动，不再次复位。 */
    HAL_Delay(300);

    uint8_t mode = 0xFFU;
    task.interrupt_mode_confirmed =
        FT6336_write(FT6336_REG_INTERRUPT_MODE, 1U) &&
        FT6336_read(FT6336_REG_INTERRUPT_MODE, &mode, 1U) && mode == 1U;
    if (!task.interrupt_mode_confirmed)
    {
        printf("[touch] interrupt mode not confirmed; polling enabled\r\n");
    }

    /* 配置普通坐标模式，供 TouchGFX 读取触点。 */
    mode = 0xFFU;
    if (!FT6336_write(FT6336_REG_GESTURE_MODE, 0U) ||
        !FT6336_read(FT6336_REG_GESTURE_MODE, &mode, 1U) || mode != 0U)
    {
        printf("[touch] normal touch mode not confirmed\r\n");
    }
}

uint32_t FT6336_get_scan_delay_ms(void)
{
    if (!task.poll_started)
    {
        return 0U;
    }
    if (task.interrupt_mode_confirmed && touch.state == TOUCH_IDLE)
    {
        return UINT32_MAX;
    }
    const uint32_t elapsed = (uint32_t)(HAL_GetTick() - task.last_poll_tick);
    return elapsed < FT6336_TOUCH_POLL_MS ? FT6336_TOUCH_POLL_MS - elapsed : 0U;
}

void FT6336_scan_task(bool touch_event)
{
    if (!touch_event && FT6336_get_scan_delay_ms() != 0U)
    {
        return;
    }
    task.poll_started = true;
    task.last_poll_tick = HAL_GetTick();

    /* 只需触点数和第一个触点的 X/Y，即 0x02~0x06。 */
    uint8_t data[5];
    touch.pressed = false; /* Failed, released, or invalid frames release the GUI. */
    if (!FT6336_read(FT6336_REG_TOUCH, data, sizeof(data)))
    {
        touch.state = TOUCH_IGNORED;
        return;
    }

    uint8_t count = data[0] & 0x0FU;
    if (count == 0U)
    {
        touch.state = TOUCH_IDLE;
        return;
    }
    if (count > 1U)
    {
        touch.state = TOUCH_IGNORED;
        return;
    }

    uint8_t event = data[1] >> 6;
    uint8_t id = data[3] >> 4;
    if (event == 3U || id == 0x0FU)
    {
        touch.state = TOUCH_IGNORED;
        return;
    }
    if (touch.state == TOUCH_IGNORED)
    {
        if (event == 1U)
        {
            touch.state = TOUCH_IDLE;
        }
        return;
    }
    if (touch.state == TOUCH_IDLE)
    {
        if (event == 1U)
        {
            return;
        }
        touch.id = id;
        touch.state = TOUCH_TRACKING;
    }
    else if (id != touch.id)
    {
        touch.state = TOUCH_IGNORED;
        return;
    }

    touch.x = (uint16_t)(((data[1] & 0x0FU) << 8) | data[2]);
    touch.y = (uint16_t)(((data[3] & 0x0FU) << 8) | data[4]);
    touch.pressed = event != 1U;
    if (event == 1U)
    {
        touch.state = TOUCH_IDLE;
    }
}

bool FT6336_get_touch(uint16_t *x, uint16_t *y)
{
    if (x == NULL || y == NULL || !touch.pressed)
    {
        return false;
    }
    *x = touch.x;
    *y = touch.y;
    return true;
}

static bool FT6336_read(uint8_t reg, uint8_t *data, uint16_t size)
{
    uint32_t error = HAL_I2C_ERROR_NONE;
    HAL_StatusTypeDef status = BSP_I2C1_Lock(100U);
    if (status == HAL_OK)
    {
        status = HAL_I2C_Mem_Read(
            &hi2c1, FT6336_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, data, size, 100);
        error = HAL_I2C_GetError(&hi2c1);
        BSP_I2C1_Unlock();
    }
    if (status != HAL_OK)
    {
        FT6336_report_error("read", reg, status, error);
        return false;
    }
    return true;
}

static bool FT6336_write(uint8_t reg, uint8_t value)
{
    uint32_t error = HAL_I2C_ERROR_NONE;
    HAL_StatusTypeDef status = BSP_I2C1_Lock(100U);
    if (status == HAL_OK)
    {
        status = HAL_I2C_Mem_Write(
            &hi2c1, FT6336_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, &value, 1U, 100);
        error = HAL_I2C_GetError(&hi2c1);
        BSP_I2C1_Unlock();
    }
    if (status != HAL_OK)
    {
        FT6336_report_error("write", reg, status, error);
        return false;
    }
    return true;
}

static void FT6336_report_error(const char *operation, uint8_t reg,
                                HAL_StatusTypeDef status, uint32_t error)
{
    uint32_t now = HAL_GetTick();
    if (!task.error_reported ||
        (uint32_t)(now - task.last_error_tick) >= FT6336_I2C_ERROR_REPORT_MS)
    {
        printf("[touch] I2C %s failed: reg=0x%02X, status=%u, error=0x%08lX\r\n",
               operation, (unsigned int)reg, (unsigned int)status,
               (unsigned long)error);
        task.last_error_tick = now;
        task.error_reported = true;
    }
}
