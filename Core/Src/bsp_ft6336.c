/* FT6336：每次按下打印一次坐标，并在抬起时识别单指滑动。 */
#include "bsp_ft6336.h"
#include "i2c.h"
#include <stdio.h>

/* HAL 使用左移一位后的 I2C 地址。 */
#define FT6336_ADDR_WRITE         (0x38U << 1)
#define FT6336_ADDR_TD_STATUS     0x02U
#define FT6336_ADDR_CHIP_ID_LOW   0xA0U
#define FT6336_ADDR_G_MODE        0xA4U
#define FT6336_ADDR_GESTURE_MODE  0xD0U

#define FT6336_TOUCH_POLL_MS          20U
#define FT6336_I2C_ERROR_REPORT_MS  1000U
#define FT6336_SWIPE_MIN_DISTANCE     40U
#define FT6336_SWIPE_MAX_DURATION   1000U

typedef struct
{
    uint8_t poll_started;
    uint8_t error_reported;
    uint8_t interrupt_mode_confirmed;
    uint8_t retry_pending;
    uint32_t last_poll_tick;
    uint32_t last_error_tick;
} FT6336_TaskState;

typedef struct
{
    uint8_t active;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t start_x;
    uint16_t start_y;
    uint32_t start_tick;
    uint8_t swipe_valid;
} FT6336_TrackedTouch;

typedef struct
{
    uint8_t read_failed;
    FT6336_TrackedTouch points[2];
} FT6336_CoordinateMonitorState;

static FT6336_TaskState touch_task;
static FT6336_CoordinateMonitorState coordinate_monitor;
static volatile uint8_t touch_irq_pending;
static uint8_t software_swipes_enabled;
static uint8_t pending_swipe;

static const char *FT6336_swipe_name(uint8_t gesture_id);
static void FT6336_print_point(const char *event, const FT6336_TrackedTouch *point);
static uint8_t FT6336_read_checked(uint8_t addr, uint8_t *data, uint16_t size);
static uint8_t FT6336_write_checked(uint8_t addr, uint8_t value);
static void FT6336_report_i2c_error(const char *operation, uint8_t addr,
                                  HAL_StatusTypeDef status);

void FT6336_init(void)
{
    touch_task = (FT6336_TaskState){0};
    touch_irq_pending = 0U;
    coordinate_monitor = (FT6336_CoordinateMonitorState){0};
    software_swipes_enabled = 0U;
    pending_swipe = FT6336_GESTURE_NONE;
    /* RESET 与 LCD 共用 PA8，LCD 初始化后只等待触摸启动，不再次复位。 */
    HAL_Delay(300);

    /* A4=1：有效触摸数据更新时发出 INT 脉冲。 */
    uint8_t info[7] = {0U};
    if (!FT6336_write_checked(FT6336_ADDR_G_MODE, 1U) ||
        !FT6336_read_checked(FT6336_ADDR_CHIP_ID_LOW, info, sizeof(info)))
    {
        return;
    }

    const char *chip_name = info[0] == 0x01U ? "FT6336G" :
                           info[0] == 0x02U ? "FT6336U" : "Unknown";
    printf("[FT6336] CHIP_ID_HIGH=0x%02X, CHIP_ID_LOW=0x%02X (%s), FIRMWARE_ID=0x%02X, G_MODE=0x%02X\r\n",
           (unsigned int)info[3], (unsigned int)info[0], chip_name,
           (unsigned int)info[6], (unsigned int)info[4]);
    touch_task.interrupt_mode_confirmed = info[4] == 1U;
    if (!touch_task.interrupt_mode_confirmed)
    {
        printf("[FT6336] G_MODE readback mismatch; periodic polling remains active\r\n");
    }
}

uint8_t FT6336_swipe_init(void)
{
    software_swipes_enabled = 0U;
    pending_swipe = FT6336_GESTURE_NONE;
    coordinate_monitor.points[0].swipe_valid = 0U;
    coordinate_monitor.points[1].swipe_valid = 0U;
    /* 只确认普通坐标模式，不开启或读取硬件手势。 */
    uint8_t mode = 0xFFU;
    if (!FT6336_write_checked(FT6336_ADDR_GESTURE_MODE, 0U) ||
        !FT6336_read_checked(FT6336_ADDR_GESTURE_MODE, &mode, 1))
    {
        return 0U;
    }
    if (mode != 0U)
    {
        printf("[gesture-sw] normal touch mode not confirmed: D0=0x%02X\r\n",
               (unsigned int)mode);
        return 0U;
    }
    software_swipes_enabled = 1U;
    printf("[gesture-sw] ready: D0=0, distance>=%u pixels, duration<=%u ms\r\n",
           (unsigned int)FT6336_SWIPE_MIN_DISTANCE, (unsigned int)FT6336_SWIPE_MAX_DURATION);
    return 1U;
}

void FT6336_irq_fuc(void)
{
    touch_irq_pending = 1U;
}

void FT6336_scan_task(void)
{
    uint32_t now = HAL_GetTick();

    /* 原子取走中断通知，保留调用前的中断屏蔽状态。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t touch_pending = touch_irq_pending;
    touch_irq_pending = 0U;
    __set_PRIMASK(primask);
    //因为中断和这个函数共用一个变量，这个函数是清0的，不管这个全局变量
    //的值是什么都会被去清除，如果读取之后发生中断，置1之后，会被这个重置为0，所以防止竞态条件
    if (touch_task.poll_started && !touch_pending)
    {
        /* 空闲时停止 I2C 查询，直接返回，让主循环继续执行其他任务。
         * 首次扫描、读取失败或中断模式未确认时，仍允许查询。 */
        if (touch_task.interrupt_mode_confirmed && !touch_task.retry_pending &&
            !coordinate_monitor.points[0].active && !coordinate_monitor.points[1].active)
        {
            return;
        }
        if ((uint32_t)(now - touch_task.last_poll_tick) < FT6336_TOUCH_POLL_MS)
        {
            return;
        }
    }
    touch_task.poll_started = 1U;
    touch_task.last_poll_tick = now;

    touch_task.retry_pending = 1U;

    uint8_t data[13] = {0U};
    if (!FT6336_read_checked(FT6336_ADDR_TD_STATUS, data, sizeof(data)))
    {
        coordinate_monitor.points[0].swipe_valid = 0U;
        coordinate_monitor.points[1].swipe_valid = 0U;
        coordinate_monitor.read_failed = 1U;
        return; /* 读失败不生成按下或抬起。 */
    }
    if (coordinate_monitor.read_failed)
    {
        printf("[touch] coordinate read recovered\r\n");
        coordinate_monitor.read_failed = 0U;
    }

    uint8_t count = data[0] & 0x0FU;
    uint8_t ids[2] = {0U};
    uint8_t events[2] = {0U};
    uint16_t xs[2] = {0U};
    uint16_t ys[2] = {0U};
    if (count > 2U)
    {
        coordinate_monitor.points[0].swipe_valid = 0U;
        coordinate_monitor.points[1].swipe_valid = 0U;
        return;
    }
    for (uint32_t i = 0U; i < count; ++i)
    {
        uint32_t offset = 1U + 6U * i;
        ids[i] = data[offset + 2U] >> 4;
        events[i] = data[offset] >> 6;
        if (ids[i] == 0x0FU || events[i] == 3U ||
            (i == 1U && ids[0] == ids[1]))
        {
            coordinate_monitor.points[0].swipe_valid = 0U;
            coordinate_monitor.points[1].swipe_valid = 0U;
            return; /* 无事件、无效 ID 或重复 ID 不当作新的有效帧。 */
        }
        xs[i] = (uint16_t)(((uint16_t)(data[offset] & 0x0FU) << 8) | data[offset + 1U]);
        ys[i] = (uint16_t)(((uint16_t)(data[offset + 2U] & 0x0FU) << 8) | data[offset + 3U]);
    }
    if (count > 1U)
    {
        coordinate_monitor.points[0].swipe_valid = 0U;
        coordinate_monitor.points[1].swipe_valid = 0U; /* 双指动作不能触发单指切图，剩下一指后也不恢复本次。 */
    }

    /* 先释放本帧不再存在的 ID；若有有效 Lift Up 记录则使用它的终点坐标。 */
    for (uint32_t slot = 0U; slot < 2U; ++slot)
    {
        FT6336_TrackedTouch *point = &coordinate_monitor.points[slot];
        if (!point->active)
        {
            continue;
        }
        uint8_t still_active = 0U;
        uint8_t found = 0U;
        for (uint32_t i = 0U; i < count; ++i)
        {
            if (ids[i] == point->id)
            {
                found = 1U;
                still_active = events[i] != 1U;
                if (!still_active)
                {
                    point->x = xs[i];
                    point->y = ys[i];
                }
                break;
            }
        }
        if (!still_active)
        {
            if (count != 0U && !found)
            {
                point->swipe_valid = 0U; /* ID 突变不作为同一根手指的有效抬起。 */
            }
            if (software_swipes_enabled && point->swipe_valid)
            {
                point->swipe_valid = 0U;
                uint32_t duration = (uint32_t)(now - point->start_tick);
                int32_t dx = (int32_t)point->x - (int32_t)point->start_x;
                int32_t dy = (int32_t)point->y - (int32_t)point->start_y;
                uint32_t distance_x = (uint32_t)(dx < 0 ? -dx : dx);
                uint32_t distance_y = (uint32_t)(dy < 0 ? -dy : dy);

                if (duration > 0U && duration <= FT6336_SWIPE_MAX_DURATION &&
                    (distance_x >= FT6336_SWIPE_MIN_DISTANCE || distance_y >= FT6336_SWIPE_MIN_DISTANCE))
                {
                    uint8_t gesture = distance_x >= distance_y ?
                        (dx > 0 ? FT6336_GESTURE_MOVE_RIGHT : FT6336_GESTURE_MOVE_LEFT) :
                        (dy > 0 ? FT6336_GESTURE_MOVE_DOWN : FT6336_GESTURE_MOVE_UP);
                    if (pending_swipe == FT6336_GESTURE_NONE)
                    {
                        pending_swipe = gesture;
                    }
                    printf("[gesture-sw] 0x%02X %s, dx=%ld, dy=%ld, duration=%lu ms\r\n",
                           (unsigned int)gesture, FT6336_swipe_name(gesture),
                           (long)dx, (long)dy, (unsigned long)duration);
                }
            }
            point->active = 0U;
        }
    }

    for (uint32_t i = 0U; i < count; ++i)
    {
        if (events[i] == 1U)
        {
            continue;
        }
        FT6336_TrackedTouch *point = NULL;
        for (uint32_t slot = 0U; slot < 2U; ++slot)
        {
            if (coordinate_monitor.points[slot].active &&
                coordinate_monitor.points[slot].id == ids[i])
            {
                point = &coordinate_monitor.points[slot];
                break;
            }
        }
        if (point == NULL)
        {
            for (uint32_t slot = 0U; slot < 2U; ++slot)
            {
                if (!coordinate_monitor.points[slot].active)
                {
                    point = &coordinate_monitor.points[slot];
                    break;
                }
            }
            if (point == NULL)
            {
                continue;
            }
            point->active = 1U;
            point->id = ids[i];
            point->x = xs[i];
            point->y = ys[i];
            point->start_x = xs[i];
            point->start_y = ys[i];
            point->start_tick = now;
            point->swipe_valid = software_swipes_enabled && count == 1U;
            FT6336_print_point("Down", point);
        }
        else
        {
            /* 按住和移动时只更新手势终点，不重复打印本次触摸的坐标。 */
            point->x = xs[i];
            point->y = ys[i];
        }
    }
    touch_task.retry_pending = 0U;
}

uint8_t FT6336_take_swipe(void)
{
    uint8_t gesture = pending_swipe;
    pending_swipe = FT6336_GESTURE_NONE;
    return gesture;
}

static const char *FT6336_swipe_name(uint8_t gesture_id)
{
    switch (gesture_id)
    {
        case FT6336_GESTURE_NONE:       return "No Gesture";
        case FT6336_GESTURE_MOVE_UP:    return "Move Up";
        case FT6336_GESTURE_MOVE_RIGHT: return "Move Right";
        case FT6336_GESTURE_MOVE_DOWN:  return "Move Down";
        case FT6336_GESTURE_MOVE_LEFT:  return "Move Left";
        default:                        return "Unknown";
    }
}

static void FT6336_print_point(const char *event, const FT6336_TrackedTouch *point)
{
    printf("[touch] %s id=%u, x=%u, y=%u\r\n", event,
           (unsigned int)point->id, (unsigned int)point->x, (unsigned int)point->y);
}

static uint8_t FT6336_read_checked(uint8_t addr, uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        &hi2c1, FT6336_ADDR_WRITE, addr, I2C_MEMADD_SIZE_8BIT, data, size, 100);
    if (status != HAL_OK)
    {
        FT6336_report_i2c_error("read", addr, status);
        return 0U;
    }
    return 1U;
}

static uint8_t FT6336_write_checked(uint8_t addr, uint8_t value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        &hi2c1, FT6336_ADDR_WRITE, addr, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
    if (status != HAL_OK)
    {
        FT6336_report_i2c_error("write", addr, status);
        return 0U;
    }
    return 1U;
}

static void FT6336_report_i2c_error(const char *operation, uint8_t addr,
                                  HAL_StatusTypeDef status)
{
    uint32_t now = HAL_GetTick();
    if (!touch_task.error_reported ||
        ((uint32_t)(now - touch_task.last_error_tick) >= FT6336_I2C_ERROR_REPORT_MS))
    {
        printf("[touch] I2C %s failed: reg=0x%02X, status=%u, error=0x%08lX\r\n",
               operation, (unsigned int)addr, (unsigned int)status,
               (unsigned long)HAL_I2C_GetError(&hi2c1));
        touch_task.last_error_tick = now;
        touch_task.error_reported = 1U;
    }
}
