/* FT6336 点击坐标与软件滑动接口。 */
#ifndef BSP_FT6336_H
#define BSP_FT6336_H

/* 在 LCD 初始化后调用，同时配置坐标读取和软件滑动识别。 */
void FT6336_init(void);

/* PB5 中断中调用，仅通知主循环。 */
void FT6336_irq_fuc(void);

/* 主循环持续调用：快速抬起确认点击；按住移动识别一次滑动，失败无动作。 */
void FT6336_scan_task(void);

#endif
