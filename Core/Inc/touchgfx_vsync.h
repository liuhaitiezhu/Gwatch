#ifndef SPI_DEMO_TOUCHGFX_VSYNC_H
#define SPI_DEMO_TOUCHGFX_VSYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call from the LCD TE interrupt after TouchGFX initialization. */
void TouchGFX_NotifyVSync(void);

#ifdef __cplusplus
}
#endif

#endif
