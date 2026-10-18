/* FT6336 点击坐标与软件滑动接口。 */
#ifndef BSP_FT6336_H
#define BSP_FT6336_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FT6336_SWIPE_NONE,
    FT6336_SWIPE_LEFT,
    FT6336_SWIPE_RIGHT,
    FT6336_SWIPE_UP,
    FT6336_SWIPE_DOWN
} FT6336_Swipe;

/* 在 LCD 初始化后调用，同时配置坐标读取和软件滑动识别。 */
void FT6336_init(void);

/* PB5 中断中调用，仅通知显示触摸任务。 */
void FT6336_irq_fuc(void);

/* 显示触摸任务持续调用：快速抬起确认点击；按住移动识别一次滑动，失败无动作。 */
void FT6336_scan_task(void);

/* Read the latest scan without I2C access. Returns 1 while pressed, including
 * after a swipe was reported. Call from the same task as FT6336_scan_task(). */
uint8_t FT6336_get_touch(uint16_t *x, uint16_t *y);

/* 显示触摸任务取走一次滑动结果；无新滑动时返回 NONE。 */
FT6336_Swipe FT6336_take_swipe(void);

#ifdef __cplusplus
}
#endif

#endif
