#ifndef SPI_DEMO_TOUCHGFX_VSYNC_H
#define SPI_DEMO_TOUCHGFX_VSYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call from the LCD TE falling-edge ISR after TouchGFX initialization.
 * The IRQ must have a FreeRTOS-safe priority (PA11 uses priority 5).
 * Notifies the GUI and releases the separate pixel-transfer TE semaphore. */
void TouchGFX_NotifyVSync(void);

#ifdef __cplusplus
}
#endif

#endif
