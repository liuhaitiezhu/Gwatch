#ifndef BSP_GC9A01_H
#define BSP_GC9A01_H

#include "stm32u5xx_hal.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC9A01_WIDTH        240U
#define GC9A01_HEIGHT       240U
#define GC9A01_COLOR_BLACK  0x0000U
#define GC9A01_COLOR_WHITE  0xFFFFU
#define GC9A01_COLOR_RED    0xF800U
#define GC9A01_COLOR_GREEN  0x07E0U
#define GC9A01_COLOR_BLUE   0x001FU

/* Call after MX_GPIO_Init and MX_SPI1_Init, from one main-loop/task owner.
 * Uses PA4 software CS, PB0 D/C, PA8 reset and PB8 on/off backlight.
 * Keeps the generated SPI baud-rate divider; no .ioc changes are needed.
 * This PCB has a 100 nF USER-key capacitor on PA12. CMake therefore
 * defaults to GC9A01_SOFTWARE_SPI=1 with a 20 us SDA settling delay.
 * Full-frame refresh is slow in this temporary mode. Hardware SPI can
 * be selected after removing C20 or physically rerouting the data line.
 * Init clears the panel to black and leaves the backlight off. */
HAL_StatusTypeDef GC9A01_Init(void);
void GC9A01_SetBacklight(bool on);

/* Optional cooperative service during pixel transfers, in the display owner's
 * task context. Called between complete pixels (software SPI) or HAL chunks.
 * Register after dependent services have initialized; NULL disables it.
 * The callback must not call any LCD/SPI1 operation or change LCD GPIOs. */
typedef void (*GC9A01_TransferCallback)(void);
void GC9A01_SetTransferCallback(GC9A01_TransferCallback callback);

/* Blocking full-screen refresh: 240*240 native uint16_t RGB565 pixels.
 * The buffer must remain valid until return. HAL_OK means transmission
 * finished; software SPI does not acknowledge reception by the panel. */
HAL_StatusTypeDef GC9A01_Refresh(const uint16_t *framebuffer);

/* Source points to the first pixel of this rectangle, not the whole frame.
 * stride is measured in pixels. Source storage must cover
 * (height-1)*stride+width pixels and be uint16_t aligned.
 * Empty/out-of-bounds areas and calls from interrupts are rejected.
 * These functions are blocking and must not run concurrently. */
HAL_StatusTypeDef GC9A01_WriteRectRGB565(uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      const uint16_t *pixels, uint16_t stride);
HAL_StatusTypeDef GC9A01_FillRect(uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height, uint16_t color);
HAL_StatusTypeDef GC9A01_Clear(uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
