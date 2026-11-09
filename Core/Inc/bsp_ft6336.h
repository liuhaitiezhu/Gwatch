/* FT6336 按下状态与坐标采集接口。 */
#ifndef BSP_FT6336_H
#define BSP_FT6336_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 在 LCD 初始化后调用，配置中断与普通坐标模式。 */
void FT6336_init(void);

/* 显示触摸任务调用；touch_event 为 true 时立即读取，否则仅在补扫到期时读取。 */
void FT6336_scan_task(bool touch_event);

/* 距下次必要补扫的毫秒数；0 表示到期，UINT32_MAX 表示空闲无需补扫。
 * 与扫描及缓存读取在同一任务中调用。 */
uint32_t FT6336_get_scan_delay_ms(void);

/* 读取缓存，不访问 I2C；按下时返回 true 并输出坐标。
 * 与 FT6336_scan_task() 在同一任务中调用。 */
bool FT6336_get_touch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif
