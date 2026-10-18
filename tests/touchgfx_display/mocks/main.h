#ifndef DISPLAY_TEST_MAIN_H
#define DISPLAY_TEST_MAIN_H
#include "stm32u5xx_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
extern GPIO_TypeDef test_port;
#define LCD_DCX_GPIO_Port (&test_port)
#define LCD_RST_GPIO_Port (&test_port)
#define LCD_DCX_Pin 4U
#define LCD_RST_Pin 8U
void Error_Handler(void);
#ifdef __cplusplus
}
#endif
#endif
