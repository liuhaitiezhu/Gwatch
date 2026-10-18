#ifndef MOCK_HAL_H
#define MOCK_HAL_H
#include <stddef.h>
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t unused; } I2C_HandleTypeDef;
typedef struct { struct { uint32_t BaudRate; } Init; } UART_HandleTypeDef;
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_11 (1U << 11)
#define EXTI11_IRQn 11
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void Mock_ClearExti(uint16_t pin);
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) Mock_ClearExti(pin)
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin);
#define I2C_MEMADD_SIZE_8BIT 1U
uint32_t __get_IPSR(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t,
                                  uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t,
                                   uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *, uint16_t,
                                        uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
#endif
