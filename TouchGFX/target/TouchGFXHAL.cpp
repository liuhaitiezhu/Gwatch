/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : TouchGFXHAL.cpp
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

#include <TouchGFXHAL.hpp>

/* USER CODE BEGIN TouchGFXHAL.cpp */

#include "touchgfx_vsync.h"
#include "bsp_ili9341_4line.h"
#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <touchgfx/hal/OSWrappers.hpp>

using namespace touchgfx;

namespace
{
StaticSemaphore_t displayTeStorage;
SemaphoreHandle_t displayTeSemaphore = NULL;
const uint32_t DISPLAY_TE_TIMEOUT_MS = 100U;

bool waitForFreshDisplayTe()
{
    if (displayTeSemaphore == NULL || __get_IPSR() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return false;
    }

    // This binary semaphore is separate from the GUI's vsync_sem flag.
    // Discard any TE received before rendering finished, then block this task
    // until the next edge. An edge between these two calls remains available.
    (void)xSemaphoreTake(displayTeSemaphore, 0U);
    return xSemaphoreTake(displayTeSemaphore, pdMS_TO_TICKS(DISPLAY_TE_TIMEOUT_MS)) == pdTRUE;
}
}

extern "C" void TouchGFX_NotifyVSync(void)
{
    HAL::getInstance()->vSync();
    OSWrappers::signalVSync();

    // PA11 falling-edge ISR, priority 5: wake the sender without doing SPI here.
    if (displayTeSemaphore != NULL &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        (void)xSemaphoreGiveFromISR(displayTeSemaphore, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

void TouchGFXHAL::initialize()
{
    // Calling parent implementation of initialize().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.
    // Please note, HAL::initialize() must be called to initialize the framework.

    displayTeSemaphore = xSemaphoreCreateBinaryStatic(&displayTeStorage);
    if (displayTeSemaphore == NULL)
    {
        Error_Handler();
        return;
    }
    pendingDisplayRect = Rect();
    TouchGFXGeneratedHAL::initialize();
}

/**
 * Gets the frame buffer address used by the TFT controller.
 *
 * @return The address of the frame buffer currently being displayed on the TFT.
 */
uint16_t* TouchGFXHAL::getTFTFrameBuffer() const
{
    // Calling parent implementation of getTFTFrameBuffer().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    return TouchGFXGeneratedHAL::getTFTFrameBuffer();
}

/**
 * Sets the frame buffer address used by the TFT controller.
 *
 * @param [in] address New frame buffer address.
 */
void TouchGFXHAL::setTFTFrameBuffer(uint16_t* address)
{
    // Calling parent implementation of setTFTFrameBuffer(uint16_t* address).
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    TouchGFXGeneratedHAL::setTFTFrameBuffer(address);
}

/**
 * This function is called whenever the framework has performed a partial draw.
 *
 * @param rect The area of the screen that has been drawn, expressed in absolute coordinates.
 *
 * @see flushFrameBuffer().
 */
void TouchGFXHAL::flushFrameBuffer(const touchgfx::Rect& rect)
{
    const Rect dirty = rect & Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!dirty.isEmpty())
    {
        pendingDisplayRect.expandToFit(dirty);
    }

    // Parent records rendering progress; physical transfer happens in endFrame.
    TouchGFXGeneratedHAL::flushFrameBuffer(rect);
}

bool TouchGFXHAL::blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes)
{
    return TouchGFXGeneratedHAL::blockCopy(dest, src, numBytes);
}

/**
 * Configures the interrupts relevant for TouchGFX. This primarily entails setting
 * the interrupt priorities for the DMA and LCD interrupts.
 */
void TouchGFXHAL::configureInterrupts()
{
    // Calling parent implementation of configureInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    TouchGFXGeneratedHAL::configureInterrupts();
}

/**
 * Used for enabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::enableInterrupts()
{
    // Calling parent implementation of enableInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    TouchGFXGeneratedHAL::enableInterrupts();
}

/**
 * Used for disabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::disableInterrupts()
{
    // Calling parent implementation of disableInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    TouchGFXGeneratedHAL::disableInterrupts();
}

/**
 * Configure the LCD controller to fire interrupts at VSYNC. Called automatically
 * once TouchGFX initialization has completed.
 */
void TouchGFXHAL::enableLCDControllerInterrupt()
{
    // Calling parent implementation of enableLCDControllerInterrupt().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    TouchGFXGeneratedHAL::enableLCDControllerInterrupt();
}

bool TouchGFXHAL::beginFrame()
{
    const bool canRender = TouchGFXGeneratedHAL::beginFrame();
    if (canRender)
    {
        pendingDisplayRect = Rect();
    }
    return canRender;
}

void TouchGFXHAL::endFrame()
{
    // Finish the renderer's queued work and GUI VSYNC bookkeeping first.
    TouchGFXGeneratedHAL::endFrame();
    if (pendingDisplayRect.isEmpty())
    {
        return;
    }
    lockUnlockFrameBuffer();

    const uint16_t strideBytes = lcd().framebufferStride();
    const uint16_t* framebuffer = getClientFrameBuffer();
    if (framebuffer == NULL || (strideBytes & 1U) != 0U ||
        strideBytes / 2U < DISPLAY_WIDTH)
    {
        Error_Handler();
        return;
    }

    const Rect dirty = pendingDisplayRect;
    const uint16_t stridePixels = strideBytes / 2U;
    const uint16_t* pixels = framebuffer + (uint32_t)dirty.y * stridePixels + dirty.x;
    if (!waitForFreshDisplayTe())
    {
        // Do not silently send unsynchronized pixels if TE stops arriving.
        Error_Handler();
        return;
    }
    if (ILI9341_WriteRectRGB565(dirty.x, dirty.y, dirty.width, dirty.height,
                              pixels, stridePixels) != HAL_OK)
    {
        Error_Handler();
        return;
    }
    pendingDisplayRect = Rect();
    // Single GUI task + blocking transfer: no subsequent draw can overwrite
    // these pixels until all SPI bytes have been sent and endFrame returns.
}

/* USER CODE END TouchGFXHAL.cpp */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
