#ifndef BSP_SHT20_H
#define BSP_SHT20_H

#include <stdint.h>

#define SHT20_I2C_ADDRESS (0x40U << 1)

typedef enum
{
    SHT20_OK = 0,
    SHT20_ERROR_ARGUMENT,
    SHT20_ERROR_I2C,
    SHT20_ERROR_TIMEOUT,
    SHT20_ERROR_CRC,
    SHT20_ERROR_DATA_TYPE
} SHT20_Status;

typedef struct
{
    int32_t temperature_centi_c;   /* 2534 means 25.34 degrees Celsius. */
    uint32_t humidity_centi_pct;   /* 5678 means 56.78 %RH, clamped to 0..100%. */
} SHT20_Data;

/* I2C1 PB6/PB7. Call from one owning task after the scheduler has started.
 * Conversion waits block only that task and release the shared I2C bus. */
SHT20_Status SHT20_Init(void);
/* Publishes both values together only when both measurements pass CRC. */
SHT20_Status SHT20_Read(SHT20_Data *data);
const char *SHT20_StatusString(SHT20_Status status);

#endif
