/* FT6336：抬起时确认点击，按住移动时识别一次单指滑动。 */
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
#define FT6336_CLICK_MAX_DURATION    250U
#define FT6336_CLICK_MAX_MOVEMENT     10U
#define FT6336_SWIPE_MIN_DISTANCE     40U

typedef enum
{
    TOUCH_IDLE,
    TOUCH_TRACKING,
    TOUCH_REPORTED,  /* 本次滑动已经输出，等待抬起。 */
    TOUCH_IGNORED    /* 双指或数据异常，等待本次触摸结束。 */
} FT6336_TouchState;

static struct
{
    FT6336_TouchState state;
    uint8_t id;
    uint8_t click_moved;
    uint16_t start_x;
    uint16_t start_y;
    uint32_t start_tick;
} touch;

static struct
{
    uint8_t poll_started;
    uint8_t interrupt_mode_confirmed;
    uint8_t error_reported;
    uint32_t last_poll_tick;
    uint32_t last_error_tick;
} task;

static volatile uint8_t touch_irq_pending;
static uint8_t swipe_enabled;
static FT6336_Swipe pending_swipe;

static uint8_t FT6336_read(uint8_t reg, uint8_t *data, uint16_t size);
static uint8_t FT6336_write(uint8_t reg, uint8_t value);
static void FT6336_report_error(const char *operation, uint8_t reg,
                                HAL_StatusTypeDef status, uint32_t error);
static void FT6336_track_position(uint16_t x, uint16_t y, uint32_t now);
static void FT6336_finish_touch(uint32_t now);

void FT6336_init(void)
{
    touch.state = TOUCH_IDLE;
    task.poll_started = 0U;
    task.error_reported = 0U;
    touch_irq_pending = 0U;
    pending_swipe = FT6336_SWIPE_NONE;

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

    /* 普通坐标模式：用坐标位移识别滑动。 */
    mode = 0xFFU;
    swipe_enabled = FT6336_write(FT6336_REG_GESTURE_MODE, 0U) &&
        FT6336_read(FT6336_REG_GESTURE_MODE, &mode, 1U) && mode == 0U;
    if (!swipe_enabled)
    {
        printf("[touch] swipe disabled: normal touch mode not confirmed\r\n");
    }
}

void FT6336_irq_fuc(void)
{
    touch_irq_pending = 1U;
}

void FT6336_scan_task(void)
{
    uint32_t now = HAL_GetTick();

    /* 原子取走中断通知，避免清零时丢失刚到达的中断。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t pending = touch_irq_pending;
    touch_irq_pending = 0U;
    __set_PRIMASK(primask);

    if (task.poll_started && !pending)//已经采样过并且没有中断进入下列判断
    {
        if (task.interrupt_mode_confirmed && touch.state == TOUCH_IDLE)
        {
            return;
        }
        if ((uint32_t)(now - task.last_poll_tick) < FT6336_TOUCH_POLL_MS)
        {
            return;
        }
    }
    task.poll_started = 1U;
    task.last_poll_tick = now;

    /* 只需触点数和第一个触点的 X/Y，即 0x02~0x06。 */
    uint8_t data[5];
    if (!FT6336_read(FT6336_REG_TOUCH, data, sizeof(data)))
    {
        touch.state = TOUCH_IGNORED;
        return;
    }

    uint8_t count = data[0] & 0x0FU;
    if (count == 0U)
    {
        FT6336_finish_touch(now);
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
        touch.start_x = (uint16_t)(((data[1] & 0x0FU) << 8) | data[2]);
        touch.start_y = (uint16_t)(((data[3] & 0x0FU) << 8) | data[4]);
        touch.start_tick = now;
        touch.click_moved = 0U;
        touch.state = TOUCH_TRACKING;
        return;
    }
    if (id != touch.id)
    {
        touch.state = TOUCH_IGNORED;
        return;
    }

    uint16_t x = (uint16_t)(((data[1] & 0x0FU) << 8) | data[2]);
    uint16_t y = (uint16_t)(((data[3] & 0x0FU) << 8) | data[4]);
    FT6336_track_position(x, y, now);
    if (event == 1U)
    {
        FT6336_finish_touch(now);
    }
}

FT6336_Swipe FT6336_take_swipe(void)
{
    FT6336_Swipe swipe = pending_swipe;
    pending_swipe = FT6336_SWIPE_NONE;
    return swipe;
}

static void FT6336_track_position(uint16_t x, uint16_t y, uint32_t now)
{
    if (touch.state != TOUCH_TRACKING)
    {
        return;
    }

    int32_t dx = (int32_t)x - (int32_t)touch.start_x;
    int32_t dy = (int32_t)y - (int32_t)touch.start_y;
    if ((uint32_t)(dx * dx + dy * dy) >
        FT6336_CLICK_MAX_MOVEMENT * FT6336_CLICK_MAX_MOVEMENT)
    {
        touch.click_moved = 1U; /* 返回原点也不会恢复为点击。 */
    }

    uint32_t distance_x = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t distance_y = (uint32_t)(dy < 0 ? -dy : dy);
    if (!swipe_enabled ||
        (distance_x < FT6336_SWIPE_MIN_DISTANCE && distance_y < FT6336_SWIPE_MIN_DISTANCE))
    {
        return;
    }

    static const char *const names[] = {"None", "Move Left", "Move Right", "Move Up", "Move Down"};
    pending_swipe = distance_x >= distance_y ?
        (dx > 0 ? FT6336_SWIPE_RIGHT : FT6336_SWIPE_LEFT) :
        (dy > 0 ? FT6336_SWIPE_DOWN : FT6336_SWIPE_UP);
    touch.state = TOUCH_REPORTED;
    printf("[gesture-sw] %s, dx=%ld, dy=%ld, duration=%lu ms\r\n", names[pending_swipe],
           (long)dx, (long)dy, (unsigned long)(now - touch.start_tick));
}

static void FT6336_finish_touch(uint32_t now)
{
    if (touch.state == TOUCH_TRACKING && !touch.click_moved &&
        (uint32_t)(now - touch.start_tick) <= FT6336_CLICK_MAX_DURATION)
    {
        printf("[touch] Click id=%u, x=%u, y=%u\r\n", (unsigned int)touch.id,
               (unsigned int)touch.start_x, (unsigned int)touch.start_y);
    }
    touch.state = TOUCH_IDLE;
}

static uint8_t FT6336_read(uint8_t reg, uint8_t *data, uint16_t size)
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
        return 0U;
    }
    return 1U;
}

static uint8_t FT6336_write(uint8_t reg, uint8_t value)
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
        return 0U;
    }
    return 1U;
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
        task.error_reported = 1U;
    }
}
