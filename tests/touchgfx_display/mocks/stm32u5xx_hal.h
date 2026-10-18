#ifndef DISPLAY_TEST_HAL_H
#define DISPLAY_TEST_HAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef struct { uint32_t unused; } SPI_HandleTypeDef;
void HAL_Delay(uint32_t ms);
uint32_t __get_IPSR(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
