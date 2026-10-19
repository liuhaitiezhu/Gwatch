/* ILI9341 display API. Functions return HAL_OK, HAL_BUSY or a transfer/argument error. */
#ifndef BSP_ILI9341_4LINE_H
#define BSP_ILI9341_4LINE_H

#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCAN_Vertical = 0U,
    SCAN_Horizontal
} Screen_ShowDIR;

#define BLACK       0x0000

HAL_StatusTypeDef ILI9341_Init(void);
HAL_StatusTypeDef ILI9341_Display_Dir(Screen_ShowDIR direction);
HAL_StatusTypeDef ILI9341_Clear(uint16_t color);
/* Single-byte serial status commands: 0x0A..0x0F and 0xDA..0xDC.
 * No dummy byte is discarded for these 8-bit SPI reads (datasheet p.38).
 * Temporarily uses /32 (5 MHz at current SYSCLK) for this read transaction,
 * then restores the original prescaler on success or error; writes stay 40 MHz.
 * Display task only, with SDO/MISO connected; HAL_OK means transfer completed,
 * not that the external device or its returned data has been validated. */
HAL_StatusTypeDef ILI9341_ReadRegister8(uint8_t command, uint8_t *value);
HAL_StatusTypeDef LCD_ColorStart(uint16_t color); /* Starts a full-screen RGB565 color fill. */

/* Coordinates are inclusive and must be inside the current screen. */
HAL_StatusTypeDef ILI9341_SetArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* Blocking RGB565 transfer from native uint16_t pixels. The source starts at
 * the rectangle's first pixel; stride is in pixels, including row padding.
 * Source storage must cover (height - 1) * stride + width pixels.
 * Call from the single display-owning task; HAL_OK means SPI has completed. */
HAL_StatusTypeDef ILI9341_WriteRectRGB565(uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height,
                                        const uint16_t *pixels, uint16_t stride);

HAL_StatusTypeDef LCD_TransferTask(void); /* Sends one color block; HAL_BUSY means more remain. */

#ifdef __cplusplus
}
#endif

#endif
