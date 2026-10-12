/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "crc.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "bsp_ili9341_4line.h"
#include "bsp_ft6336.h"
#include "touchgfx_vsync.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static const uint16_t screen_colors[] = {
  0x0000U, 0xF800U, 0x07E0U, 0x001FU, 0xFFE0U, 0x07FFU, 0xF81FU, 0xFFFFU
}; /* Black, Red, Green, Blue, Yellow, Cyan, Magenta, White. */
static volatile uint8_t display_ready;
static uint8_t color_current;
static uint8_t color_target;
static uint8_t color_inflight;
static uint8_t color_rendering;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Redirect printf output to USART1.
  * @param  file Output stream descriptor
  * @param  ptr  Data buffer
  * @param  len  Data length
  * @retval Number of transmitted bytes, or -1 on error
  */
int _write(int file, char *ptr, int len)
{
  (void)file;

  if (HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len,
                        HAL_MAX_DELAY) != HAL_OK)
  {
    return -1;
  }

  return len;
}

static void Display_Task(void)
{
  FT6336_Swipe swipe = FT6336_take_swipe();
  if (!display_ready)
  {
    return;
  }
  const uint8_t count = sizeof(screen_colors) / sizeof(screen_colors[0]);
  if (swipe == FT6336_SWIPE_LEFT || swipe == FT6336_SWIPE_UP)
  {
    color_target = (uint8_t)((color_target + 1U) % count);
  }
  else if (swipe == FT6336_SWIPE_RIGHT || swipe == FT6336_SWIPE_DOWN)
  {
    color_target = (uint8_t)((color_target + count - 1U) % count);
  }

  if (!color_rendering && color_target != color_current)
  {
    HAL_StatusTypeDef status = LCD_ColorStart(screen_colors[color_target]);
    if (status != HAL_OK)
    {
      color_target = color_current;
      printf("[display] color start failed: status=%u\r\n", (unsigned int)status);
      return;
    }
    color_inflight = color_target;
    color_rendering = 1U;
  }
  if (!color_rendering)
  {
    return;
  }
  HAL_StatusTypeDef status = LCD_TransferTask();
  if (status == HAL_BUSY)
  {
    return;
  }
  color_rendering = 0U;
  if (status != HAL_OK)
  {
    color_target = color_current;
    printf("[display] color transfer failed: status=%u\r\n", (unsigned int)status);
    return;
  }
  color_current = color_inflight;
  printf("[display] color=%u/%u, RGB565=0x%04X\r\n", (unsigned int)(color_current + 1U),
         (unsigned int)count, (unsigned int)screen_colors[color_current]);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  MX_CRC_Init();
  MX_I2C1_Init();
  MX_TouchGFX_Init();
  /* USER CODE BEGIN 2 */
  /* Keep TE notifications disabled throughout the LCD reset/init sequence. */
  HAL_NVIC_DisableIRQ(EXTI11_IRQn);
  printf("\r\nFT6336 swipe colors: USART1 baud=%lu\r\n",
         (unsigned long)huart1.Init.BaudRate);
  HAL_StatusTypeDef lcd_status = ILI9341_Init();
  if (lcd_status != HAL_OK)
  {
    printf("[display] initialization failed: status=%u\r\n", (unsigned int)lcd_status);
  }
  /* Touch RESET shares PA8 with LCD: initialize after the LCD reset sequence. */
  FT6336_init();
  display_ready = lcd_status == HAL_OK; /* ILI9341_Init has already cleared the screen to black. */
  if (display_ready)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(LCD_TE_Pin);
    HAL_NVIC_ClearPendingIRQ(EXTI11_IRQn);
    HAL_NVIC_EnableIRQ(EXTI11_IRQn);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

  MX_TouchGFX_Process();
    /* USER CODE BEGIN 3 */
    FT6336_scan_task();
    Display_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCC_EnableCSS();
}

/* USER CODE BEGIN 4 */
/* PB5 notifies the touch task; PA11 notifies TouchGFX. No transfers in the ISR. */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_5)
  {
    FT6336_irq_fuc();
  }
  else if (GPIO_Pin == LCD_TE_Pin && display_ready)
  {
    TouchGFX_NotifyVSync();
  }
}

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
