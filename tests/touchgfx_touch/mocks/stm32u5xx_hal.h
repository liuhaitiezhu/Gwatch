#ifndef TOUCH_TEST_HAL_H
#define TOUCH_TEST_HAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t unused; } I2C_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1U
#define HAL_I2C_ERROR_NONE 0U
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t,
                                  uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t,
                                   uint16_t, uint8_t *, uint16_t, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
