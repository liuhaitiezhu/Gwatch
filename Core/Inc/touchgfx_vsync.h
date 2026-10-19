#ifndef SPI_DEMO_TOUCHGFX_VSYNC_H
#define SPI_DEMO_TOUCHGFX_VSYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* GUI task only. Call after consuming a periodic software-timer notification,
 * before MX_TouchGFX_Process(). This schedules a frame without sending pixels. */
void TouchGFX_TimerTick(void);

#ifdef __cplusplus
}
#endif

#endif
