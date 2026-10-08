/**
  ******************************************************************************
  * @file   bsp_ft6336.c
  * @brief  2.8寸屏ft6336驱动文件,i2c接口
  *
  ******************************************************************************
  */
#include "bsp_ft6336.h"
#include "i2c.h"
#include "gpio.h"

#define FT6336_GESTURE_POLL_MS         10U
#define FT6336_I2C_ERROR_REPORT_MS   1000U
#define FT6336_GESTURE_STATE_MS      2000U
#define FT6336_GESTURE_ENABLE_TRIES     3U
#define FT6336_COORDINATE_REPORT_MS    30U
#define FT6336_SWIPE_MIN_DISTANCE      40U
#define FT6336_SWIPE_MAX_DURATION    1000U

typedef struct
{
    uint8_t enabled;
    uint8_t poll_started;
    uint8_t sample_valid;
    uint8_t d3_failed;
    uint8_t error_reported;
    uint8_t last_gesture;
    uint32_t last_poll_tick;
    uint32_t last_error_tick;
    uint32_t last_state_tick;
} FT6336_GestureMonitorState;

static FT6336_GestureMonitorState gesture_monitor;

typedef struct
{
    uint8_t active;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint32_t last_print_tick;
    uint16_t start_x;
    uint16_t start_y;
    uint32_t start_tick;
    uint8_t swipe_valid;
} FT6336_TrackedTouch;

typedef struct
{
    uint8_t read_failed;
    uint8_t touch_count;
    FT6336_TrackedTouch points[2];
} FT6336_CoordinateMonitorState;

static FT6336_CoordinateMonitorState coordinate_monitor;
static uint8_t software_swipes_enabled;
static uint8_t pending_swipe;

static void FT6336_cancel_swipes(void)
{
    coordinate_monitor.points[0].swipe_valid = 0U;
    coordinate_monitor.points[1].swipe_valid = 0U;
}

/* 只在同一根手指的有效抬起时生成一次事件，连续 Contact/INT 不会重复切图。 */
static void FT6336_finish_swipe(FT6336_TrackedTouch *point, uint32_t now)
{
    if (!software_swipes_enabled || !point->swipe_valid)
    {
        return;
    }
    point->swipe_valid = 0U;
    uint32_t duration = (uint32_t)(now - point->start_tick);
    int32_t dx = (int32_t)point->x - (int32_t)point->start_x;
    int32_t dy = (int32_t)point->y - (int32_t)point->start_y;
    uint32_t distance_x = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t distance_y = (uint32_t)(dy < 0 ? -dy : dy);
    if (duration == 0U || duration > FT6336_SWIPE_MAX_DURATION ||
        (distance_x < FT6336_SWIPE_MIN_DISTANCE && distance_y < FT6336_SWIPE_MIN_DISTANCE))
    {
        return;
    }
    uint8_t gesture = distance_x >= distance_y ?
        (dx > 0 ? FT6336_GESTURE_MOVE_RIGHT : FT6336_GESTURE_MOVE_LEFT) :
        (dy > 0 ? FT6336_GESTURE_MOVE_DOWN : FT6336_GESTURE_MOVE_UP);
    if (pending_swipe == FT6336_GESTURE_NONE)
    {
        pending_swipe = gesture;
        printf("[gesture-sw] 0x%02X %s, dx=%ld, dy=%ld, duration=%lu ms\r\n",
               (unsigned int)gesture, FT6336_basic_gesture_name(gesture),
               (long)dx, (long)dy, (unsigned long)duration);
    }
}

/* 手势路径的通信错误保留寄存器地址，避免把失败读出的零当作正常数据。 */
static void FT6336_report_i2c_error(const char *operation, uint8_t addr,
                                  HAL_StatusTypeDef status)
{
    uint32_t now = HAL_GetTick();
    if (!gesture_monitor.error_reported ||
        ((uint32_t)(now - gesture_monitor.last_error_tick) >= FT6336_I2C_ERROR_REPORT_MS))
    {
        printf("[gesture-hw] I2C %s failed: reg=0x%02X, status=%u, error=0x%08lX\r\n",
               operation, (unsigned int)addr, (unsigned int)status,
               (unsigned long)HAL_I2C_GetError(&hi2c1));
        gesture_monitor.last_error_tick = now;
        gesture_monitor.error_reported = 1U;
    }
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

static void FT6336_print_point(const char *event, const FT6336_TrackedTouch *point)
{
    printf("[touch] %s id=%u, x=%u, y=%u\r\n", event,
           (unsigned int)point->id, (unsigned int)point->x, (unsigned int)point->y);
}

/* 只处理成功读到的有效帧。Touch ID 是标识，不用它直接索引两元素数组。 */
static uint8_t FT6336_monitor_coordinates(uint32_t now)
{
    uint8_t data[13] = {0U};
    if (!FT6336_read_checked(FT6336_ADDR_TD_STATUS, data, sizeof(data)))
    {
        FT6336_cancel_swipes();
        coordinate_monitor.read_failed = 1U;
        return 0U; /* 读失败不生成按下或抬起。 */
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
        FT6336_cancel_swipes();
        return 0U;
    }
    for (uint32_t i = 0U; i < count; ++i)
    {
        uint32_t offset = 1U + 6U * i;
        ids[i] = data[offset + 2U] >> 4;
        events[i] = data[offset] >> 6;
        if (ids[i] == 0x0FU || events[i] == 3U ||
            (i == 1U && ids[0] == ids[1]))
        {
            FT6336_cancel_swipes();
            return 0U; /* 无事件、无效 ID 或重复 ID 不当作新的有效帧。 */
        }
        xs[i] = (uint16_t)(((uint16_t)(data[offset] & 0x0FU) << 8) | data[offset + 1U]);
        ys[i] = (uint16_t)(((uint16_t)(data[offset + 2U] & 0x0FU) << 8) | data[offset + 3U]);
    }
    coordinate_monitor.touch_count = count;
    if (count > 1U)
    {
        FT6336_cancel_swipes(); /* 双指动作不能触发单指切图，剩下一指后也不恢复本次。 */
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
            FT6336_finish_swipe(point, now);
            FT6336_print_point("Up", point);
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
            point->last_print_tick = now;
            point->start_x = xs[i];
            point->start_y = ys[i];
            point->start_tick = now;
            point->swipe_valid = software_swipes_enabled && count == 1U;
            FT6336_print_point("Down", point);
        }
        else
        {
            /* last_print_tick 只限速日志，不阻止保存最新有效坐标。 */
            if ((point->x != xs[i] || point->y != ys[i]) &&
                ((uint32_t)(now - point->last_print_tick) >= FT6336_COORDINATE_REPORT_MS))
            {
                point->x = xs[i];
                point->y = ys[i];
                point->last_print_tick = now;
                FT6336_print_point("Move", point);
            }
            point->x = xs[i];
            point->y = ys[i];
        }
    }
    return 1U;
}

//
volatile uint32_t ft6336_on_touch_count = 0;

//event_notify_cb touch_notify;
FT6336_TouchPointType tp;

/*
**********************************************************************
* @fun     :FT6336_readByte
* @brief   :读取从机数据
* @param   :addr:寄存器地址
* @return  :data:返回的数据
**********************************************************************
*/
uint8_t FT6336_readByte(uint8_t addr)
{
    /* HAL_I2C_Mem_Read：一次事务完成"设寄存器地址 + 读数据"，
       内部用 repeated START（写完地址不发 STOP 直接重启读），
       所以不需要旧实现里那 1ms 的等待延时。 */
    uint8_t data = 0U;
    HAL_I2C_Mem_Read(&hi2c1, FT6336_ADDR_WRITE, addr, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
    return data;
}
/*
**********************************************************************
* @fun     :FT6336_writeByte
* @brief   :对从机写数据
* @param   :addr:寄存器地址，data:写入的数据
* @return  :None
**********************************************************************
*/
void FT6336_writeByte(uint8_t addr, uint8_t data)
{
    HAL_I2C_Mem_Write(&hi2c1, FT6336_ADDR_WRITE, addr, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}
/*
**********************************************************************
* @fun     :FT6336_irq_fuc
* @brief   :中断函数调用，生产按压事件
* @param   :None
* @return  :None
**********************************************************************
*/
void FT6336_irq_fuc(void)
{
    ft6336_on_touch_count = 1;
}
/*
**********************************************************************
* @fun     :FT6336_scan_task
* @brief   :读取触点坐标和 D3 硬件手势并输出到串口，由主循环反复调用
* @param   :None
* @return  :None
**********************************************************************
*/
void FT6336_scan_task(void)
{
    uint32_t now = HAL_GetTick();

    /* 原子取走中断通知，保留调用前的中断屏蔽状态。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t touch_pending = ft6336_on_touch_count;
    ft6336_on_touch_count = 0U;
    __set_PRIMASK(primask);

    /* 中断到来时立即读；周期读取也能观察到无新下降沿时的寄存器变化。 */
    if (gesture_monitor.poll_started && !touch_pending &&
        ((uint32_t)(now - gesture_monitor.last_poll_tick) < FT6336_GESTURE_POLL_MS))
    {
        return;
    }
    gesture_monitor.poll_started = 1U;
    gesture_monitor.last_poll_tick = now;

    uint8_t coordinates_valid = FT6336_monitor_coordinates(now);
    if (!gesture_monitor.enabled)
    {
        return; /* 手势配置失败时，坐标读取和输出仍然运行。 */
    }

    /* D3 是只读结果，不能写零清除；不根据坐标或触点数生成手势。 */
    uint8_t gesture = 0U;
    if (!FT6336_read_checked(FT6336_ADDR_SPECIAL_GEST_ID, &gesture, 1))
    {
        gesture_monitor.d3_failed = 1U;
        return;
    }

    if (gesture_monitor.d3_failed)
    {
        printf("[gesture-hw] D3 read recovered\r\n");
        gesture_monitor.sample_valid = 0U;
        gesture_monitor.d3_failed = 0U;
    }

    uint8_t report_state = !gesture_monitor.sample_valid ||
        ((uint32_t)(now - gesture_monitor.last_state_tick) >= FT6336_GESTURE_STATE_MS);

    /* 持续轮询不重复刷屏；相同非零 ID 收到新 INT 时仍输出，便于重复划线测试。
       source 表示本次读数由 INT 还是轮询触发，不推断固件结果的清除时机。 */
    if (!gesture_monitor.sample_valid || (gesture != gesture_monitor.last_gesture) ||
        (touch_pending && (gesture != FT6336_GESTURE_NONE)))
    {
        printf("[gesture-hw] D3=0x%02X %s, source=%s\r\n",
               (unsigned int)gesture, FT6336_basic_gesture_name(gesture),
               touch_pending ? "INT" : "poll");
    }
    gesture_monitor.last_gesture = gesture;
    gesture_monitor.sample_valid = 1U;

    if (report_state)
    {
        gesture_monitor.last_state_tick = now;
        uint8_t config[2] = {0U};
        if (!FT6336_read_checked(FT6336_ADDR_GESTURE_MODE, config, sizeof(config)))
        {
            return;
        }
        const char *config_state = (config[0] == 1U &&
            config[1] == FT6336_SPECIAL_GEST_ALL) ? "OK" : "CONFIG MISMATCH";

        if (coordinates_valid)
        {
            printf("[gesture-hw] state: D0=0x%02X, D1=0x%02X, D3=0x%02X, touches=%u, %s\r\n",
                   (unsigned int)config[0], (unsigned int)config[1],
                   (unsigned int)gesture, (unsigned int)coordinate_monitor.touch_count, config_state);
        }
        else
        {
            printf("[gesture-hw] state: D0=0x%02X, D1=0x%02X, D3=0x%02X, touches=unavailable, %s\r\n",
                   (unsigned int)config[0], (unsigned int)config[1],
                   (unsigned int)gesture, config_state);
        }
    }
}
/*
**********************************************************************
* @fun     :FT6336_init
* @brief   :等待共用 LCD 复位后的启动，配置中断模式并输出芯片信息
* @param   :None
* @return  :None
**********************************************************************
*/
void FT6336_init(void)
{
    gesture_monitor = (FT6336_GestureMonitorState){0};
    coordinate_monitor = (FT6336_CoordinateMonitorState){0};
    software_swipes_enabled = 0U;
    pending_swipe = FT6336_GESTURE_NONE;
    /* 本工程触摸 RESET 和 LCD RESET 共用 PA8。
       必须在 ILI9341_Init() 后调用：该函数已完成共用复位脉冲。
       此处不再拉低 PA8，否则会清除 LCD 已完成的初始化。
       等待触摸固件启动和校准；后续如复位 LCD，需重新初始化触摸配置。 */
    HAL_Delay(300);

    /* 0xA4 只控制 INT 通知方式；手势模式由 FT6336_gesture_init 配置。 */
    uint8_t g_mode = (uint8_t)triggerMode;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        &hi2c1, FT6336_ADDR_WRITE, FT6336_ADDR_G_MODE,
        I2C_MEMADD_SIZE_8BIT, &g_mode, 1, 100);
    if (status != HAL_OK)
    {
        printf("[FT6336] G_MODE write failed: status=%u, error=0x%08lX\r\n",
               (unsigned int)status, (unsigned long)HAL_I2C_GetError(&hi2c1));
        return;
    }

    /* 连读 0xA0~0xA6：代号低字节、库版本、高字节、INT、电源、固件。 */
    uint8_t info[7] = {0U};
    status = HAL_I2C_Mem_Read(
        &hi2c1, FT6336_ADDR_WRITE, FT6336_ADDR_CHIP_ID_LOW,
        I2C_MEMADD_SIZE_8BIT, info, sizeof(info), 100);
    if (status != HAL_OK)
    {
        printf("[FT6336] chip info read failed: status=%u, error=0x%08lX\r\n",
               (unsigned int)status, (unsigned long)HAL_I2C_GetError(&hi2c1));
        return;
    }

    const char *chip_name = info[0] == 0x01U ? "FT6336G" :
                           info[0] == 0x02U ? "FT6336U" : "Unknown";
    printf("[FT6336] CHIP_ID_HIGH=0x%02X, CHIP_ID_LOW=0x%02X (%s), FIRMWARE_ID=0x%02X, G_MODE=0x%02X\r\n",
           (unsigned int)info[3], (unsigned int)info[0], chip_name,
           (unsigned int)info[6], (unsigned int)info[4]);
    if (info[4] != (uint8_t)triggerMode)
    {
        printf("[FT6336] G_MODE readback mismatch; periodic polling remains active\r\n");
    }
}
/*
**********************************************************************
* @fun     :FT6336状态读取函数
**********************************************************************
*/
uint8_t FT6336_read_device_mode(void)
{
    return (FT6336_readByte(FT6336_ADDR_DEVICE_MODE) & 0x70) >> 4;
}
//
void FT6336_write_device_mode(DEVICE_MODE_Enum mode)
{
    FT6336_writeByte(FT6336_ADDR_DEVICE_MODE, (mode & 0x07) << 4);
}
//
uint8_t FT6336_read_gesture_id(void)
{
    return FT6336_readByte(FT6336_ADDR_GESTURE_ID);
}
//
uint8_t FT6336_read_td_status(void)
{
    return FT6336_readByte(FT6336_ADDR_TD_STATUS);
}
//
uint8_t FT6336_read_touch_number(void)
{
    return FT6336_readByte(FT6336_ADDR_TD_STATUS) & 0x0F;
}
/*
**********************************************************************
* @fun     :FT6336_read_touch1_x
* @brief   :读取touch1的数据
* @param   :None
* @return  :None
**********************************************************************
*/
uint16_t FT6336_read_touch1_x(void)
{
    uint8_t read_buf[2];
    read_buf[0] = FT6336_readByte(FT6336_ADDR_TOUCH1_X);
    read_buf[1] = FT6336_readByte(FT6336_ADDR_TOUCH1_X + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
//
uint16_t FT6336_read_touch1_y(void)
{
    uint8_t read_buf[2];
    read_buf[0] = FT6336_readByte(FT6336_ADDR_TOUCH1_Y);
    read_buf[1] = FT6336_readByte(FT6336_ADDR_TOUCH1_Y + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
//
uint8_t FT6336_read_touch1_event(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH1_EVENT) >> 6;
}
//
uint8_t FT6336_read_touch1_id(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH1_ID) >> 4;
}
//
uint8_t FT6336_read_touch1_weight(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH1_WEIGHT);
}
//
uint8_t FT6336_read_touch1_misc(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH1_MISC) >> 4;
}
/*
**********************************************************************
* @fun     :FT6336_read_touch2_x
* @brief   :读取touch2的数据
* @param   :None
* @return  :None
**********************************************************************
*/
uint16_t FT6336_read_touch2_x(void)
{
    uint8_t read_buf[2];
    read_buf[0] = FT6336_readByte(FT6336_ADDR_TOUCH2_X);
    read_buf[1] = FT6336_readByte(FT6336_ADDR_TOUCH2_X + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
//
uint16_t FT6336_read_touch2_y(void)
{
    uint8_t read_buf[2];
    read_buf[0] = FT6336_readByte(FT6336_ADDR_TOUCH2_Y);
    read_buf[1] = FT6336_readByte(FT6336_ADDR_TOUCH2_Y + 1);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
//
uint8_t FT6336_read_touch2_event(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH2_EVENT) >> 6;
}
//
uint8_t FT6336_read_touch2_id(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH2_ID) >> 4;
}
//
uint8_t FT6336_read_touch2_weight(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH2_WEIGHT);
}
//
uint8_t FT6336_read_touch2_misc(void)
{
    return FT6336_readByte(FT6336_ADDR_TOUCH2_MISC) >> 4;
}
/*
**********************************************************************
* @fun     :Mode Parameter Register
**********************************************************************
*/
uint8_t FT6336_read_touch_threshold(void)
{
    return FT6336_readByte(FT6336_ADDR_THRESHOLD);
}
//
uint8_t FT6336_read_filter_coefficient(void)
{
    return FT6336_readByte(FT6336_ADDR_FILTER_COE);
}
//
uint8_t FT6336_read_ctrl_mode(void)
{
    return FT6336_readByte(FT6336_ADDR_CTRL);
}
//
void FT6336_write_ctrl_mode(CTRL_MODE_Enum mode)
{
    FT6336_writeByte(FT6336_ADDR_CTRL, mode);
}
//
uint8_t FT6336_read_time_period_enter_monitor(void)
{
    return FT6336_readByte(FT6336_ADDR_TIME_ENTER_MONITOR);
}
//
uint8_t FT6336_read_active_rate(void)
{
    return FT6336_readByte(FT6336_ADDR_ACTIVE_MODE_RATE);
}
//
uint8_t FT6336_read_monitor_rate(void)
{
    return FT6336_readByte(FT6336_ADDR_MONITOR_MODE_RATE);
}
/*
**********************************************************************
* @fun     :Gesture Parameters
**********************************************************************
*/
uint8_t FT6336_read_radian_value(void)
{
    return FT6336_readByte(FT6336_ADDR_RADIAN_VALUE);
}
//
void FT6336_write_radian_value(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_RADIAN_VALUE, val);
}
//
uint8_t FT6336_read_offset_left_right(void)
{
    return FT6336_readByte(FT6336_ADDR_OFFSET_LEFT_RIGHT);
}
//
void FT6336_write_offset_left_right(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_OFFSET_LEFT_RIGHT, val);
}
//
uint8_t FT6336_read_offset_up_down(void)
{
    return FT6336_readByte(FT6336_ADDR_OFFSET_UP_DOWN);
}
//
void FT6336_write_offset_up_down(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_OFFSET_UP_DOWN, val);
}
//
uint8_t FT6336_read_distance_left_right(void)
{
    return FT6336_readByte(FT6336_ADDR_DISTANCE_LEFT_RIGHT);
}
//
void FT6336_write_distance_left_right(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_DISTANCE_LEFT_RIGHT, val);
}
//
uint8_t FT6336_read_distance_up_down(void)
{
    return FT6336_readByte(FT6336_ADDR_DISTANCE_UP_DOWN);
}
//
void FT6336_write_distance_up_down(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_DISTANCE_UP_DOWN, val);
}
//
uint8_t FT6336_read_distance_zoom(void)
{
    return FT6336_readByte(FT6336_ADDR_DISTANCE_ZOOM);
}
//
void FT6336_write_distance_zoom(uint8_t val)
{
    FT6336_writeByte(FT6336_ADDR_DISTANCE_ZOOM, val);
}
/*
**********************************************************************
* @fun     :FT6336_gesture_init
* @brief   :配置 D1 并开启 D0，读回确认后允许主循环读取 D3
* @param   :None
* @return  :1 配置确认成功，0 通信失败或读回不一致
* @note    :AN v0.1 第 10 页 D0 总开关、第 12 页 D1 类型掩码。
**********************************************************************
*/
uint8_t FT6336_gesture_init(void)
{
    software_swipes_enabled = 0U;
    pending_swipe = FT6336_GESTURE_NONE;
    FT6336_cancel_swipes();
    gesture_monitor = (FT6336_GestureMonitorState){0};

    /* 先关闭总开关，配置四方向划线和双击；D1 保留位保持零。 */
    if (!FT6336_write_checked(FT6336_ADDR_GESTURE_MODE, 0U) ||
        !FT6336_write_checked(FT6336_ADDR_SPECIAL_GEST_ENABLE, FT6336_SPECIAL_GEST_ALL))
    {
        return 0U;
    }

    /* 本监控只识别基础动作，关闭 AN 中定义的字符类型选择，不写保留寄存器。 */
    const uint8_t char_registers[] = {
        FT6336_ADDR_CHAR_ENABLE_D2, FT6336_ADDR_CHAR_ENABLE_D5,
        FT6336_ADDR_CHAR_ENABLE_D6, FT6336_ADDR_CHAR_ENABLE_D7,
        FT6336_ADDR_CHAR_ENABLE_D8
    };
    for (uint32_t i = 0U; i < sizeof(char_registers) / sizeof(char_registers[0]); ++i)
    {
        if (!FT6336_write_checked(char_registers[i], 0U))
        {
            return 0U;
        }
    }

    uint8_t config[2] = {0U};
    for (uint32_t attempt = 0U; attempt < FT6336_GESTURE_ENABLE_TRIES; ++attempt)
    {
        if (!FT6336_write_checked(FT6336_ADDR_GESTURE_MODE, 1U))
        {
            return 0U;
        }
        HAL_Delay(10); /* 给固件处理模式切换的时间，最多尝试三次。 */
        if (!FT6336_read_checked(FT6336_ADDR_GESTURE_MODE, config, sizeof(config)))
        {
            return 0U;
        }
        if (config[0] == 1U && config[1] == FT6336_SPECIAL_GEST_ALL)
        {
            gesture_monitor.enabled = 1U;
            printf("[gesture-hw] config OK: D0=0x%02X, D1=0x%02X, result=D3\r\n",
                   (unsigned int)config[0], (unsigned int)config[1]);
            return 1U;
        }
    }

    printf("[gesture-hw] config mismatch: D0=0x%02X (expect 0x01), D1=0x%02X (expect 0x%02X)\r\n",
           (unsigned int)config[0], (unsigned int)config[1],
           (unsigned int)FT6336_SPECIAL_GEST_ALL);
    (void)FT6336_write_checked(FT6336_ADDR_GESTURE_MODE, 0U);
    return 0U; /* 读回不一致只说明配置未确认，不能据此断言所有固件均不支持。 */
}

uint8_t FT6336_swipe_init(void)
{
    gesture_monitor.enabled = 0U;
    software_swipes_enabled = 0U;
    pending_swipe = FT6336_GESTURE_NONE;
    FT6336_cancel_swipes();
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

uint8_t FT6336_take_swipe(void)
{
    uint8_t gesture = pending_swipe;
    pending_swipe = FT6336_GESTURE_NONE;
    return gesture;
}
/*
**********************************************************************
* @fun     :FT6336_gesture_enable_chars
* @brief   :使能手写字符识别（0xD2 / 0xD5-0xD8）
* @param   :None
* @return  :None
* @note    :每个 bit 对应 AN §3.1.9 表格里一个字符；保留位保持 0。
**********************************************************************
*/
void FT6336_gesture_enable_chars(void)
{
    FT6336_writeByte(FT6336_ADDR_CHAR_ENABLE_D2, FT6336_CHAR_MASK_D2);  /* d a g c e m w o */
    FT6336_writeByte(FT6336_ADDR_CHAR_ENABLE_D5, FT6336_CHAR_MASK_D5);  /* u s p l q b   n */
    FT6336_writeByte(FT6336_ADDR_CHAR_ENABLE_D6, FT6336_CHAR_MASK_D6);  /* △ v ^ >   @   */
    FT6336_writeByte(FT6336_ADDR_CHAR_ENABLE_D7, FT6336_CHAR_MASK_D7);  /* 2 8 7 9 6 3   */
    FT6336_writeByte(FT6336_ADDR_CHAR_ENABLE_D8, FT6336_CHAR_MASK_D8);  /* r y k h       */
}
/*
**********************************************************************
* @fun     :FT6336_basic_gesture_name
* @brief   :按 FocalTech FT6336 驱动的 D3 编码打印名称，原始 ID 始终保留
* @param   :gesture_id: FT6336_read_gesture_id() 的返回值
* @return  :手势名；未知值返回 "Unknown"
**********************************************************************
*/
const char *FT6336_basic_gesture_name(uint8_t gesture_id)
{
    switch (gesture_id)
    {
        case FT6336_GESTURE_NONE:       return "No Gesture";
        case FT6336_GESTURE_MOVE_UP:    return "Move Up";
        case FT6336_GESTURE_MOVE_RIGHT: return "Move Right";
        case FT6336_GESTURE_MOVE_DOWN:  return "Move Down";
        case FT6336_GESTURE_MOVE_LEFT:  return "Move Left";
        case FT6336_GESTURE_DOUBLE_TAP: return "Double Tap";
        default:                        return "Unknown";
    }
}
/*
**********************************************************************
* @fun     :System Information
**********************************************************************
*/
uint16_t FT6336_read_library_version(void)
{
    uint8_t read_buf[2];
    read_buf[0] = FT6336_readByte(FT6336_ADDR_LIBRARY_VERSION_H);
    read_buf[1] = FT6336_readByte(FT6336_ADDR_LIBRARY_VERSION_L);
    return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
//
uint8_t FT6336_read_chip_id(void)
{
    return FT6336_readByte(FT6336_ADDR_CHIP_ID);
}
//
uint8_t FT6336_read_g_mode(void)
{
    return FT6336_readByte(FT6336_ADDR_G_MODE);
}
//
void FT6336_write_g_mode(G_MODE_Enum mode)
{
    FT6336_writeByte(FT6336_ADDR_G_MODE, mode);
}
//
uint8_t FT6336_read_pwrmode(void)
{
    return FT6336_readByte(FT6336_ADDR_POWER_MODE);
}
//
uint8_t FT6336_read_firmware_id(void)
{
    return FT6336_readByte(FT6336_ADDR_FIRMARE_ID);
}
//
uint8_t FT6336_read_focaltech_id(void)
{
    return FT6336_readByte(FT6336_ADDR_FOCALTECH_ID);
}
//
uint8_t FT6336_read_release_code_id(void)
{
    return FT6336_readByte(FT6336_ADDR_RELEASE_CODE_ID);
}
//
uint8_t FT6336_read_state(void)
{
    return FT6336_readByte(FT6336_ADDR_STATE);
}
/*
**********************************************************************
* @fun     :FT6336_scan
* @brief   :FT6336触摸点读取
* @param   :None
* @return  :触摸点信息
**********************************************************************
*/
/*
**********************************************************************
* @fun     :FT6336_scan
* @brief   :FT6336触摸点读取（一次 I2C 事务连读 0x02~0x0E 共 13 字节）
* @param   :None
* @return  :触摸点信息
* @note    :FT6336 寄存器地址自增，HAL_I2C_Mem_Read 内部用 repeated START
*           一次读完 TD_STATUS + 两个触点的全部字段。
*           旧版逐字节读取（7 次 readByte，每次含 1ms 延时）约 8.4ms，
*           现在一次事务只需约 0.3ms，且顺带补上了第二触点的读取。
**********************************************************************
*/
FT6336_TouchPointType FT6336_scan(void)
{
    /* 一次读 0x02~0x0E：
       buf[0]      = 0x02 TD_STATUS   [3:0]触点数
       buf[1..4]   = 0x03~0x06 P1_XH / P1_XL / P1_YH / P1_YL
       buf[5..6]   = 0x07~0x08 P1_WEIGHT / P1_MISC（暂不解析）
       buf[7..10]  = 0x09~0x0C P2_XH / P2_XL / P2_YH / P2_YL
       buf[11..12] = 0x0D~0x0E P2_WEIGHT / P2_MISC（暂不解析）*/
    uint8_t buf[13];
    FT6336_TouchPointType touchPoint = {0};

    if (HAL_I2C_Mem_Read(&hi2c1, FT6336_ADDR_WRITE, FT6336_ADDR_TD_STATUS,
                         I2C_MEMADD_SIZE_8BIT, buf, 13, 100) != HAL_OK)
    {
        return tp;   /* I2C 失败：维持上次的全局状态，绝不用垃圾数据做判定 */
    }

    /* TD_STATUS[3:0] 才是触点数，高 4 位是保留位，必须屏蔽 */
    touchPoint.touch_count = buf[0] & 0x0FU;

    if (touchPoint.touch_count == 0U)
    {
        /* 一根手指都没有了：本次就是"抬起"。
           抬起瞬间坐标寄存器可能返回默认值 0xFF（=4095），会算出虚假的
           巨大位移，因此终点坐标沿用全局 tp 里上一次移动到的位置。 */
        touchPoint.tp[0].status = release;
        touchPoint.tp[1].status = release;
        touchPoint.tp[0].x = tp.tp[0].x;
        touchPoint.tp[0].y = tp.tp[0].y;
        touchPoint.tp[1].x = tp.tp[1].x;
        touchPoint.tp[1].y = tp.tp[1].y;
        return touchPoint;
    }

    /* ---- 触点 1 ----
       事件标志在 P1_XH[7:6]：00=按下 01=抬起 10=按住 11=无事件
       Touch ID 在 P1_YH[7:4]，无效时为 0x0F，必须防数组越界 */
    uint8_t id1 = (uint8_t)(buf[3] >> 4);
    if (id1 > 1U)
    {
        id1 = 0U;
    }

    switch ((buf[1] >> 6) & 0x03U)
    {
        case 0:  touchPoint.tp[id1].status = touch;   break;  /* Press Down */
        case 1:  touchPoint.tp[id1].status = release; break;  /* Lift Up（可能另一指还按着）*/
        default: touchPoint.tp[id1].status = stream;  break;  /* Contact / No event */
    }
    touchPoint.tp[id1].x = (uint16_t)(((uint16_t)(buf[1] & 0x0FU) << 8) | buf[2]);
    touchPoint.tp[id1].y = (uint16_t)(((uint16_t)(buf[3] & 0x0FU) << 8) | buf[4]);

    /* 本次没被报到的另一路槽位，先视为无触点（若确有第二指，下面会覆盖）*/
    touchPoint.tp[(uint8_t)(id1 ^ 0x01U)].status = release;

    /* ---- 触点 2：仅 touch_count >= 2 时这些字段才有效 ---- */
    if (touchPoint.touch_count >= 2U)
    {
        uint8_t id2 = (uint8_t)(buf[9] >> 4);
        if ((id2 > 1U) || (id2 == id1))
        {
            id2 = (uint8_t)(id1 ^ 0x01U);   /* ID 无效或撞号：放进另一槽 */
        }

        switch ((buf[7] >> 6) & 0x03U)
        {
            case 0:  touchPoint.tp[id2].status = touch;   break;
            case 1:  touchPoint.tp[id2].status = release; break;
            default: touchPoint.tp[id2].status = stream;  break;
        }
        touchPoint.tp[id2].x = (uint16_t)(((uint16_t)(buf[7] & 0x0FU) << 8) | buf[8]);
        touchPoint.tp[id2].y = (uint16_t)(((uint16_t)(buf[9] & 0x0FU) << 8) | buf[10]);
    }

    return touchPoint;
}
