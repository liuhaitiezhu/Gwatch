/* FT6336 单次按下坐标与软件滑动接口。 */
#ifndef BSP_FT6336_H
#define BSP_FT6336_H

#include <stdint.h>

/* 上下左右的软件滑动识别结果。 */
#define FT6336_GESTURE_NONE        0x00U
#define FT6336_GESTURE_MOVE_LEFT   0x20U
#define FT6336_GESTURE_MOVE_RIGHT  0x21U
#define FT6336_GESTURE_MOVE_UP     0x22U
#define FT6336_GESTURE_MOVE_DOWN   0x23U

void FT6336_init(void);

uint8_t FT6336_swipe_init(void);

void FT6336_irq_fuc(void);

/* 主循环持续调用：每次按下打印一次坐标，抬起时判断并打印滑动手势。 */
void FT6336_scan_task(void);

uint8_t FT6336_take_swipe(void);

#endif
