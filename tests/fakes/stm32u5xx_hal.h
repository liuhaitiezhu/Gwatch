#ifndef TEST_STM32U5XX_HAL_H
#define TEST_STM32U5XX_HAL_H

/* Host HAL boundary only; ARM firmware uses the real STM32 HAL. */
#include <stdint.h>
#include <stdbool.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t output; uint32_t mode[16]; } GPIO_TypeDef;
extern GPIO_TypeDef fake_gpio_a, fake_gpio_b;
#define GPIOA (&fake_gpio_a)
#define GPIOB (&fake_gpio_b)
#define GPIO_PIN_0  (1U << 0)
#define GPIO_PIN_1  (1U << 1)
#define GPIO_PIN_4  (1U << 4)
#define GPIO_PIN_5  (1U << 5)
#define GPIO_PIN_8  (1U << 8)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;

typedef struct { uint32_t CR1, CFG1; } SPI_TypeDef;
extern SPI_TypeDef fake_spi1;
#define SPI1 (&fake_spi1)
#define SPI_CR1_SPE 1U
#define SPI_CFG1_DSIZE 31U
#define SPI_DATASIZE_8BIT 7U
#define SPI_DATASIZE_16BIT 15U
#define SPI_NSS_SOFT 1U
#define SPI_NSS_PULSE_DISABLE 0U
#define SPI_MASTER_KEEP_IO_STATE_ENABLE 1U
#define HAL_SPI_STATE_READY 1U
typedef struct {
    uint32_t NSS, NSSPMode, MasterKeepIOState, DataSize, BaudRatePrescaler;
} SPI_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; SPI_InitTypeDef Init; uint32_t State; } SPI_HandleTypeDef;
#define MODIFY_REG(reg,mask,value) ((reg)=((reg)&~(mask))|(value))
#define __HAL_SPI_DISABLE(h) ((h)->Instance->CR1 &= ~SPI_CR1_SPE)
extern uint32_t fake_ipsr;
#define __get_IPSR() (fake_ipsr)

typedef struct { uint32_t CR1, CNT, ARR; } TIM_TypeDef;
extern TIM_TypeDef fake_tim6;
#define TIM6 (&fake_tim6)
#define TIM_CR1_CEN 1U
void fake_nop(void);
#define __NOP() fake_nop()

void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *gpio);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *spi);
uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                  uint16_t count, uint32_t timeout);

#endif
