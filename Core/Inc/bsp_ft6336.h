/**
  ******************************************************************************
  * @file   bsp_ft6336.h
  * @brief  2.8寸屏ft6336驱动文件,i2c接口
  *
  ******************************************************************************
  */
#ifndef __BSP_FT6336_H__
#define __BSP_FT6336_H__
//
#include "main.h"
//
#include <stdio.h>
#include <stdlib.h>
//
#define I2C_ADDR_FT6336 0x38
//
#define FT6336_ADDR_READ  0x71
#define FT6336_ADDR_WRITE 0x70

// Touch Parameter
#define FT6336_PRES_DOWN 0x2
#define FT6336_COORD_UD 0x1

// Registers
#define FT6336_ADDR_DEVICE_MODE 0x00

typedef enum
{
    working_mode = 0b000,
    factory_mode = 0b100,
} DEVICE_MODE_Enum;

#define FT6336_ADDR_TD_STATUS 0x02

#define FT6336_ADDR_TOUCH1_EVENT 0x03
#define FT6336_ADDR_TOUCH1_ID 0x05
#define FT6336_ADDR_TOUCH1_X 0x03
#define FT6336_ADDR_TOUCH1_Y 0x05
#define FT6336_ADDR_TOUCH1_WEIGHT 0x07
#define FT6336_ADDR_TOUCH1_MISC 0x08

#define FT6336_ADDR_TOUCH2_EVENT 0x09
#define FT6336_ADDR_TOUCH2_ID 0x0B
#define FT6336_ADDR_TOUCH2_X 0x09
#define FT6336_ADDR_TOUCH2_Y 0x0B
#define FT6336_ADDR_TOUCH2_WEIGHT 0x0D
#define FT6336_ADDR_TOUCH2_MISC 0x0E

#define FT6336_ADDR_THRESHOLD 0x80
#define FT6336_ADDR_FILTER_COE 0x85
#define FT6336_ADDR_CTRL 0x86

typedef enum
{
    keep_active_mode = 0,
    switch_to_monitor_mode = 1,
} CTRL_MODE_Enum;

#define FT6336_ADDR_TIME_ENTER_MONITOR 0x87
#define FT6336_ADDR_ACTIVE_MODE_RATE 0x88
#define FT6336_ADDR_MONITOR_MODE_RATE 0x89

#define FT6336_ADDR_RADIAN_VALUE 0x91
#define FT6336_ADDR_OFFSET_LEFT_RIGHT 0x92
#define FT6336_ADDR_OFFSET_UP_DOWN 0x93
#define FT6336_ADDR_DISTANCE_LEFT_RIGHT 0x94
#define FT6336_ADDR_DISTANCE_UP_DOWN 0x95
#define FT6336_ADDR_DISTANCE_ZOOM 0x96

#define FT6336_ADDR_CHIP_ID_MID 0x9F
#define FT6336_ADDR_CHIP_ID_LOW 0xA0
#define FT6336_ADDR_LIBRARY_VERSION_H 0xA1
#define FT6336_ADDR_LIBRARY_VERSION_L 0xA2
#define FT6336_ADDR_CHIP_ID 0xA3  /* Chip Selecting High，型号还需读取 0xA0 */
#define FT6336_ADDR_G_MODE 0xA4

typedef enum
{
    pollingMode = 0,
    triggerMode = 1,
} G_MODE_Enum;
#define FT6336_ADDR_POWER_MODE 0xA5
#define FT6336_ADDR_FIRMARE_ID 0xA6
#define FT6336_ADDR_FOCALTECH_ID 0xA8
#define FT6336_ADDR_RELEASE_CODE_ID 0xAF
#define FT6336_ADDR_STATE 0xBC

/*
 * ---------------- 手势识别 ----------------
 * FT6336G/U AN v0.1 第 10、12 页：0xD0 总开关，0xD1 类型选择，0xD3 结果。
 * 本型号 AN 没有将 0x01 定义为 GEST_ID，不能套用 FT6x06 的基础手势表。
 * 返回 ID 的名称参考 FocalTech FT6336 Android 驱动 focaltech_gesture.c；
 * https://android.googlesource.com/kernel/mediatek/+/8293d077bc2e6552aba4a7da832936cb1055a8f0/drivers/input/touchscreen/mediatek/FT6336/focaltech_gesture.c
 * AN 未列出 ID 编码，因此串口始终保留原始值供实际固件核对。
 */
#define FT6336_GESTURE_NONE        0x00
#define FT6336_GESTURE_MOVE_LEFT   0x20
#define FT6336_GESTURE_MOVE_RIGHT  0x21
#define FT6336_GESTURE_MOVE_UP     0x22
#define FT6336_GESTURE_MOVE_DOWN   0x23
#define FT6336_GESTURE_DOUBLE_TAP  0x24

#define FT6336_ADDR_GESTURE_MODE       0xD0  /* 0 关闭，1 开启 */

/* 特殊手势使能寄存器 0xD1（bit7-5 保留） */
#define FT6336_ADDR_SPECIAL_GEST_ENABLE 0xD1
#define FT6336_SPECIAL_GEST_R2L_LINE    0x01  /* bit0 划线：右→左 */
#define FT6336_SPECIAL_GEST_L2R_LINE    0x02  /* bit1 划线：左→右 */
#define FT6336_SPECIAL_GEST_D2U_LINE    0x04  /* bit2 划线：下→上 */
#define FT6336_SPECIAL_GEST_U2D_LINE    0x08  /* bit3 划线：上→下 */
#define FT6336_SPECIAL_GEST_DOUBLE_TAP  0x10  /* bit4 双击 */
#define FT6336_SPECIAL_GEST_ALL         0x1F  /* 以上全部 */

/* 特殊手势结果寄存器（只读） */
#define FT6336_ADDR_SPECIAL_GEST_ID     0xD3
#define FT6336_ADDR_GESTURE_ID          FT6336_ADDR_SPECIAL_GEST_ID

/* 手写字符使能寄存器（bit7→bit0 与 AN P12 表格列一一对应，保留位保持 0） */
#define FT6336_ADDR_CHAR_ENABLE_D2      0xD2  /* d a g c e m w o（8 位全定义）*/
#define FT6336_ADDR_CHAR_ENABLE_D5      0xD5  /* u s p l q b   n（bit1 保留）*/
#define FT6336_ADDR_CHAR_ENABLE_D6      0xD6  /* △ v ^ >   @（bit7-6/bit1 保留）*/
#define FT6336_ADDR_CHAR_ENABLE_D7      0xD7  /* 2 8 7 9 6 3（bit7-6 保留）*/
#define FT6336_ADDR_CHAR_ENABLE_D8      0xD8  /* r y k h（bit7-4 保留）*/

#define FT6336_CHAR_MASK_D2             0xFF
#define FT6336_CHAR_MASK_D5             0xFD
#define FT6336_CHAR_MASK_D6             0x3D
#define FT6336_CHAR_MASK_D7             0x3F
#define FT6336_CHAR_MASK_D8             0x0F

// Function Specific Type
typedef enum
{
    touch = 0,
    stream,
    release,
} TouchStatusEnum;

typedef struct
{
    TouchStatusEnum status;
    uint16_t x;
    uint16_t y;
} TouchPointType;

typedef struct
{
    uint8_t touch_count;
    TouchPointType tp[2];
} FT6336_TouchPointType;

/**************************************************************************/
/*!
    @brief  FT6336 I2C CTP controller driver
*/
/**************************************************************************/

/* RESET 与 LCD 共用 PA8，须在 ILI9341_Init() 后调用；本函数不再次复位 LCD。 */
void FT6336_init(void);

uint8_t FT6336_read_device_mode(void);
void FT6336_write_device_mode(DEVICE_MODE_Enum);
uint8_t FT6336_read_gesture_id(void);
uint8_t FT6336_read_td_status(void);
uint8_t FT6336_read_touch_number(void);
uint16_t FT6336_read_touch1_x(void);
uint16_t FT6336_read_touch1_y(void);
uint8_t FT6336_read_touch1_event(void);
uint8_t FT6336_read_touch1_id(void);
uint8_t FT6336_read_touch1_weight(void);
uint8_t FT6336_read_touch1_misc(void);
uint16_t FT6336_read_touch2_x(void);
uint16_t FT6336_read_touch2_y(void);
uint8_t FT6336_read_touch2_event(void);
uint8_t FT6336_read_touch2_id(void);
uint8_t FT6336_read_touch2_weight(void);
uint8_t FT6336_read_touch2_misc(void);

// Mode Parameter Register
uint8_t FT6336_read_touch_threshold(void);
uint8_t FT6336_read_filter_coefficient(void);
uint8_t FT6336_read_ctrl_mode(void);
void FT6336_write_ctrl_mode(CTRL_MODE_Enum mode);
uint8_t FT6336_read_time_period_enter_monitor(void);
uint8_t FT6336_read_active_rate(void);
uint8_t FT6336_read_monitor_rate(void);

// Gestrue Parameter Register
uint8_t FT6336_read_radian_value(void);
void FT6336_write_radian_value(uint8_t val);
uint8_t FT6336_read_offset_left_right(void);
void FT6336_write_offset_left_right(uint8_t val);
uint8_t FT6336_read_offset_up_down(void);
void FT6336_write_offset_up_down(uint8_t val);
uint8_t FT6336_read_distance_left_right(void);
void FT6336_write_distance_left_right(uint8_t val);
uint8_t FT6336_read_distance_up_down(void);
void FT6336_write_distance_up_down(uint8_t val);
uint8_t FT6336_read_distance_zoom(void);
void FT6336_write_distance_zoom(uint8_t val);

// System Information
uint16_t FT6336_read_library_version(void);
uint8_t FT6336_read_chip_id(void);
uint8_t FT6336_read_g_mode(void);
void FT6336_write_g_mode(G_MODE_Enum mode);
uint8_t FT6336_read_pwrmode(void);
uint8_t FT6336_read_firmware_id(void);
uint8_t FT6336_read_focaltech_id(void);
uint8_t FT6336_read_release_code_id(void);
uint8_t FT6336_read_state(void);

// Gesture Function
uint8_t FT6336_gesture_init(void);                  /* 配置 D0/D1；读回确认成功返回 1 */
uint8_t FT6336_swipe_init(void);                    /* D0=0，启用单指坐标滑动识别 */
uint8_t FT6336_take_swipe(void);                    /* 取走一次抬起确认的滑动，0 表示无事件 */
void FT6336_gesture_enable_chars(void);             /* 使能手写字符识别（0xD2/0xD5-0xD8） */
const char *FT6336_basic_gesture_name(uint8_t gesture_id);  /* D3 值转可读名字 */

// Scan Function
FT6336_TouchPointType FT6336_scan(void);
//
void FT6336_irq_fuc(void);
/* 主循环任务：输出按下/移动/抬起坐标，并独立读取 D3 硬件手势。 */
void FT6336_scan_task(void);
#endif
