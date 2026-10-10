/* ILI9341 display API. Functions return HAL_OK, HAL_BUSY or a transfer/argument error. */
#ifndef BSP_ILI9341_4LINE_H
#define BSP_ILI9341_4LINE_H

#include "stm32u5xx_hal.h"

typedef enum {
    SCAN_Vertical = 0U,
    SCAN_Horizontal
} Screen_ShowDIR;

#define BLACK       0x0000

HAL_StatusTypeDef ILI9341_Init(void);
HAL_StatusTypeDef ILI9341_Display_Dir(Screen_ShowDIR direction);
HAL_StatusTypeDef ILI9341_Clear(uint16_t color);
HAL_StatusTypeDef LCD_ColorStart(uint16_t color); /* Starts a full-screen RGB565 color fill. */

/* Coordinates are inclusive and must be inside the current screen. */
HAL_StatusTypeDef ILI9341_SetArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

#define LCD_PICTURE_COUNT 4U
HAL_StatusTypeDef LCD_PictureStart(uint8_t index); /* Starts one 240x320 RGB565 picture. */
HAL_StatusTypeDef LCD_TransferTask(void);         /* Sends one color/image block; HAL_BUSY means more remain. */

#endif
