#ifndef BSP_MAX30102_H
#define BSP_MAX30102_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX30102_I2C_ADDRESS_7BIT  0x57U
#define MAX30102_I2C_ADDRESS       (MAX30102_I2C_ADDRESS_7BIT << 1)
#define MAX30102_SAMPLE_RATE_HZ    100U
#define MAX30102_FIFO_DEPTH        32U
#define MAX30102_FIFO_BATCH_SIZE   8U

/* LED current codes, nominally 0.2 mA/LSB. Tune on the actual optical assembly. */
#ifndef MAX30102_RED_LED_CURRENT
#define MAX30102_RED_LED_CURRENT   0x24U
#endif
#ifndef MAX30102_IR_LED_CURRENT
#define MAX30102_IR_LED_CURRENT    0x24U
#endif

#define MAX30102_INT_FIFO_FULL     0x80U
#define MAX30102_INT_DATA_READY    0x40U
#define MAX30102_INT_ALC_OVERFLOW  0x20U
#define MAX30102_INT_POWER_READY   0x01U

typedef enum
{
    MAX30102_OK = 0,
    MAX30102_ERROR_ARGUMENT,
    MAX30102_ERROR_NOT_INITIALIZED,
    MAX30102_ERROR_I2C,
    MAX30102_ERROR_TIMEOUT,
    MAX30102_ERROR_DEVICE_ID,
    MAX30102_ERROR_RESET_TIMEOUT,
    MAX30102_ERROR_FIFO_OVERFLOW,
    MAX30102_ERROR_POWER_RESET,
    MAX30102_ERROR_SAMPLE_TIMEOUT
} MAX30102_Status;

typedef struct
{
    uint32_t red;  /* Raw 18-bit ADC counts, NOT bpm or oxygen saturation. */
    uint32_t ir;
} MAX30102_Sample;

typedef struct
{
    uint8_t count;
    uint8_t interrupt_status;
    uint8_t lost_samples; /* Hardware counter saturates at 31. */
} MAX30102_FIFOInfo;

/* One owning task only, after the scheduler starts. Uses shared I2C1 PB6/PB7.
 * Init verifies part ID 0x15, resets the device and starts Red+IR acquisition.
 * Fixed: 100 Hz, no FIFO averaging/rollover, 411 us pulses, 4096 nA ADC range.
 * No MCU interrupt pin is needed. Reset waits release the shared bus mutex. */
MAX30102_Status MAX30102_Init(void);

/* capacity: 1..MAX30102_FIFO_BATCH_SIZE. OK + count == 0 is an empty FIFO.
 * On failure count stays zero and samples are untouched. Bus/FIFO/reset errors
 * require Init before reading again, since sample continuity may be lost.
 * ALC_OVERFLOW reports compromised optical data even if the I2C read succeeds. */
MAX30102_Status MAX30102_ReadFIFO(MAX30102_Sample *samples, uint8_t capacity,
                                  MAX30102_FIFOInfo *info);
MAX30102_Status MAX30102_Shutdown(void); /* Restart with Init. */
const char *MAX30102_StatusString(MAX30102_Status status);

#ifdef __cplusplus
}
#endif

#endif
