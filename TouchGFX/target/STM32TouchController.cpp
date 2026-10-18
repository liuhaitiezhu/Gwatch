/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.26.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
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

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>
#include <touchgfx/hal/HAL.hpp>
#include "bsp_ft6336.h"

void STM32TouchController::init()
{
    // DisplayTouchTask initializes FT6336 after ILI9341: both share RESET on PA8.
    // TouchGFX initializes earlier, so do not reset or initialize FT6336 here.
}

bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    // DisplayTouchTask owns FT6336_scan_task(). Read its cached
    // state here so TouchGFX does not start a second I2C transaction.
    uint16_t touch_x;
    uint16_t touch_y;
    if (!FT6336_get_touch(&touch_x, &touch_y) ||
        touch_x >= touchgfx::HAL::DISPLAY_WIDTH ||
        touch_y >= touchgfx::HAL::DISPLAY_HEIGHT)
    {
        return false;
    }

    // The current 240x320 portrait display uses the native touch coordinates.
    x = touch_x;
    y = touch_y;
    return true;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
