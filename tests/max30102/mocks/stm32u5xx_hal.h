#ifndef MOCK_STM32_HAL_H
#define MOCK_STM32_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { int unused; } I2C_HandleTypeDef;
typedef struct { struct { uint32_t BaudRate; } Init; } UART_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1U
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_11 (1U << 11)
#define GPIOA ((void *)1)
#define EXTI11_IRQn 11
#define EXTI5_IRQn 5
#define HAL_I2C_ERROR_NONE 0U
uint32_t __get_IPSR(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t,
    uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t,
    uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_GPIO_DeInit(void *, uint32_t);
#endif
