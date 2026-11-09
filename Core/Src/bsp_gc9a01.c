#include "bsp_gc9a01.h"
#include "main.h"
#include "spi.h"
#include <stddef.h>

#define LCD_CS_GPIO_Port   GPIOA
#define LCD_CS_Pin         GPIO_PIN_4
#define LCD_TIMEOUT_MS     100U
#define LCD_PIXEL_CHUNK    4096U
#define LCD_FILL_CHUNK     256U

#ifndef GC9A01_SOFTWARE_SPI
#define GC9A01_SOFTWARE_SPI 0
#endif

#if GC9A01_SOFTWARE_SPI
/* FS-STM32U5-v2.0 has C20=100 nF on PA12. This temporary backend
 * lets SDA settle after each transition. TIM6 is the existing 1 MHz
 * HAL timebase; no timer/IRQ configuration or .ioc change is made. */
#define LCD_SDA_SETTLE_US  20U
static bool software_data_high;

static void lcd_wait_data_settle(void)
{
    const uint32_t period = TIM6->ARR + 1U;
    uint32_t last = TIM6->CNT;
    uint32_t elapsed = 0U;
    while (elapsed < LCD_SDA_SETTLE_US)
    {
        const uint32_t now = TIM6->CNT;
        elapsed += now >= last ? now - last : now + period - last;
        last = now;
        __NOP();
    }
}

static void lcd_write_software_byte(uint8_t value)
{
    for (uint32_t bit = 0U; bit < 8U; ++bit)
    {
        const bool high = (value & (0x80U >> bit)) != 0U;
        if (high != software_data_high)
        {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
            software_data_high = high;
            lcd_wait_data_settle();
        }
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        __NOP(); __NOP(); __NOP(); __NOP();
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        __NOP(); __NOP(); __NOP(); __NOP();
    }
}
#endif

static HAL_StatusTypeDef lcd_transmit_bytes(const uint8_t *data, uint16_t size)
{
#if GC9A01_SOFTWARE_SPI
    for (uint16_t i = 0U; i < size; ++i) { lcd_write_software_byte(data[i]); }
    return HAL_OK;
#else
    return HAL_SPI_Transmit(&hspi1, data, size, LCD_TIMEOUT_MS);
#endif
}

typedef struct
{
    uint8_t command;
    uint8_t length;
    uint8_t data[12];
} LCD_InitCommand;

typedef struct
{
    uint32_t init_size;
    uint32_t register_size;
} LCD_PixelMode;

/* Adapted from ../YWatch/Core/Src/bsp_gc9a01.c. The panel power/gamma
 * values originate from the Waveshare 1.28-inch reference and still require
 * validation on the actual panel. Standard commands follow GC9A01A.pdf.
 * Gwatch uses different pins and GPIO backlight rather than TIM8 PWM. */
static const LCD_InitCommand init_commands[] =
{
    {0xEF, 0, {0}}, {0xEB, 1, {0x14}},
    {0xFE, 0, {0}}, {0xEF, 0, {0}}, {0xEB, 1, {0x14}},
    {0x84, 1, {0x40}}, {0x85, 1, {0xFF}}, {0x86, 1, {0xFF}},
    {0x87, 1, {0xFF}}, {0x88, 1, {0x0A}}, {0x89, 1, {0x21}},
    {0x8A, 1, {0x00}}, {0x8B, 1, {0x80}}, {0x8C, 1, {0x01}},
    {0x8D, 1, {0x01}}, {0x8E, 1, {0xFF}}, {0x8F, 1, {0xFF}},
    {0xB6, 2, {0x00, 0x20}},
    {0x36, 1, {0x08}}, /* Native orientation, BGR panel order. */
    {0x3A, 1, {0x05}}, /* DBI=101: RGB565 on the MCU serial interface. */
    {0x90, 4, {0x08, 0x08, 0x08, 0x08}},
    {0xBD, 1, {0x06}}, {0xBC, 1, {0x00}},
    {0xFF, 3, {0x60, 0x01, 0x04}},
    {0xC3, 1, {0x13}}, {0xC4, 1, {0x13}}, {0xC9, 1, {0x22}},
    {0xBE, 1, {0x11}}, {0xE1, 2, {0x10, 0x0E}},
    {0xDF, 3, {0x21, 0x0C, 0x02}},
    {0xF0, 6, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}},
    {0xF1, 6, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}},
    {0xF2, 6, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}},
    {0xF3, 6, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}},
    {0xED, 2, {0x1B, 0x0B}}, {0xAE, 1, {0x77}}, {0xCD, 1, {0x63}},
    {0x70, 9, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}},
    {0xE8, 1, {0x34}},
    {0x62, 12, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70,
                0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}},
    {0x63, 12, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70,
                0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}},
    {0x64, 7, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}},
    {0x66, 10, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}},
    {0x67, 10, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}},
    {0x74, 7, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}},
    {0x98, 2, {0x3E, 0x07}},
    {0x34, 0, {0}}, /* Refresh is task/timer driven, without TE synchronization. */
    {0x21, 0, {0}}  /* Inversion used by the reference IPS panel. */
};

static bool lcd_ready;
static uint16_t fill_pixels[LCD_FILL_CHUNK];
static GC9A01_TransferCallback transfer_callback;

void GC9A01_SetTransferCallback(GC9A01_TransferCallback callback)
{
    transfer_callback = callback;
}

static HAL_StatusTypeDef lcd_check_ready(void)
{
    if (__get_IPSR() != 0U || !lcd_ready) { return HAL_ERROR; }
    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY ||
        (hspi1.Instance->CR1 & SPI_CR1_SPE) != 0U) { return HAL_BUSY; }
    return HAL_OK;
}

static bool lcd_valid_area(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    return width != 0U && height != 0U &&
           (uint32_t)x + width <= GC9A01_WIDTH &&
           (uint32_t)y + height <= GC9A01_HEIGHT;
}

static HAL_StatusTypeDef lcd_command(uint8_t command, const uint8_t *data, uint16_t length)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = lcd_transmit_bytes(&command, 1U);
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_SET);
    if (status == HAL_OK && length != 0U)
    {
        status = lcd_transmit_bytes(data, length);
    }
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    return status;
}

static HAL_StatusTypeDef lcd_begin_pixels(uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height,
                                        LCD_PixelMode *saved)
{
    const uint16_t x1 = x + width - 1U;
    const uint16_t y1 = y + height - 1U;
    const uint8_t columns[] = {(uint8_t)(x >> 8), (uint8_t)x,
                               (uint8_t)(x1 >> 8), (uint8_t)x1};
    const uint8_t rows[] = {(uint8_t)(y >> 8), (uint8_t)y,
                            (uint8_t)(y1 >> 8), (uint8_t)y1};
    HAL_StatusTypeDef status = lcd_command(0x2A, columns, sizeof(columns));
    if (status == HAL_OK) { status = lcd_command(0x2B, rows, sizeof(rows)); }
    if (status != HAL_OK) { return status; }

    uint8_t command = 0x2C;
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_RESET);
    status = lcd_transmit_bytes(&command, 1U);
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_SET);
    if (status != HAL_OK)
    {
        HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
        return status;
    }

    /* Blocking HAL leaves SPE clear. Sixteen-bit MSB-first frames send
     * native RGB565 uint16_t pixels high byte first without a swap buffer. */
    saved->init_size = hspi1.Init.DataSize;
    saved->register_size = hspi1.Instance->CFG1 & SPI_CFG1_DSIZE;
#if !GC9A01_SOFTWARE_SPI
    hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
    MODIFY_REG(hspi1.Instance->CFG1, SPI_CFG1_DSIZE, SPI_DATASIZE_16BIT);
#endif
    return HAL_OK;
}

static void lcd_end_pixels(const LCD_PixelMode *saved)
{
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CFG1, SPI_CFG1_DSIZE, saved->register_size);
    hspi1.Init.DataSize = saved->init_size;
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef lcd_send_pixels(const uint16_t *pixels, uint32_t count)
{
#if GC9A01_SOFTWARE_SPI
    for (uint32_t i = 0U; i < count; ++i)
    {
        lcd_write_software_byte((uint8_t)(pixels[i] >> 8));
        lcd_write_software_byte((uint8_t)pixels[i]);
        if (transfer_callback != NULL) { transfer_callback(); }
    }
    return HAL_OK;
#else
    while (count != 0U)
    {
        const uint16_t chunk = count > LCD_PIXEL_CHUNK ? LCD_PIXEL_CHUNK : (uint16_t)count;
        HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, (const uint8_t *)pixels,
                                                  chunk, LCD_TIMEOUT_MS);
        if (status != HAL_OK) { return status; }
        pixels += chunk;
        count -= chunk;
        if (transfer_callback != NULL) { transfer_callback(); }
    }
    return HAL_OK;
#endif
}

HAL_StatusTypeDef GC9A01_WriteRectRGB565(uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      const uint16_t *pixels, uint16_t stride)
{
    if (!lcd_valid_area(x, y, width, height) || pixels == NULL ||
        ((uintptr_t)pixels & 1U) != 0U || stride < width) { return HAL_ERROR; }
    HAL_StatusTypeDef status = lcd_check_ready();
    if (status != HAL_OK) { return status; }
    LCD_PixelMode saved;
    status = lcd_begin_pixels(x, y, width, height, &saved);
    if (status != HAL_OK) { return status; }

    /* CS remains low across RAMWR, every row, and every HAL chunk. */
    if (stride == width) { status = lcd_send_pixels(pixels, (uint32_t)width * height); }
    else
    {
        for (uint16_t row = 0U; row < height && status == HAL_OK; ++row)
        {
            status = lcd_send_pixels(pixels + (uint32_t)row * stride, width);
        }
    }
    lcd_end_pixels(&saved);
    return status;
}

HAL_StatusTypeDef GC9A01_Refresh(const uint16_t *framebuffer)
{
    return GC9A01_WriteRectRGB565(0U, 0U, GC9A01_WIDTH, GC9A01_HEIGHT,
                                 framebuffer, GC9A01_WIDTH);
}

HAL_StatusTypeDef GC9A01_FillRect(uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height, uint16_t color)
{
    if (!lcd_valid_area(x, y, width, height)) { return HAL_ERROR; }
    HAL_StatusTypeDef status = lcd_check_ready();
    if (status != HAL_OK) { return status; }
    for (uint32_t i = 0U; i < LCD_FILL_CHUNK; ++i) { fill_pixels[i] = color; }
    LCD_PixelMode saved;
    status = lcd_begin_pixels(x, y, width, height, &saved);
    if (status != HAL_OK) { return status; }
    uint32_t remaining = (uint32_t)width * height;
    while (remaining != 0U && status == HAL_OK)
    {
        const uint16_t chunk = remaining > LCD_FILL_CHUNK ? LCD_FILL_CHUNK : (uint16_t)remaining;
        status = lcd_send_pixels(fill_pixels, chunk);
        remaining -= chunk;
    }
    lcd_end_pixels(&saved);
    return status;
}

HAL_StatusTypeDef GC9A01_Clear(uint16_t color)
{
    return GC9A01_FillRect(0U, 0U, GC9A01_WIDTH, GC9A01_HEIGHT, color);
}

void GC9A01_SetBacklight(bool on)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

HAL_StatusTypeDef GC9A01_Init(void)
{
    if (__get_IPSR() != 0U || hspi1.Instance != SPI1) { return HAL_ERROR; }
    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY ||
        (hspi1.Instance->CR1 & SPI_CR1_SPE) != 0U) { return HAL_BUSY; }
    lcd_ready = false;
    GC9A01_SetBacklight(false);
#if GC9A01_SOFTWARE_SPI
    if ((TIM6->CR1 & TIM_CR1_CEN) == 0U || TIM6->ARR < LCD_SDA_SETTLE_US)
    {
        return HAL_ERROR;
    }
#endif

    /* Application override: preserve CubeMX output and the 40 MHz divider. */
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    HAL_StatusTypeDef status = HAL_SPI_Init(&hspi1);
    if (status != HAL_OK) { return status; }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LCD_CS_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LCD_CS_GPIO_Port, &gpio);
    gpio.Pin = LCD_DCX_Pin;
    HAL_GPIO_Init(LCD_DCX_GPIO_Port, &gpio);
#if GC9A01_SOFTWARE_SPI
    /* Bypass the SPI alternate function, keeping SCK idle low. The unused
     * hardware SPI configuration still retains its original /4 divider. */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_12;
    HAL_GPIO_WritePin(GPIOA, gpio.Pin, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOA, &gpio);
    software_data_high = false;
#endif

    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120U);
    for (uint32_t i = 0U; i < sizeof(init_commands) / sizeof(init_commands[0]); ++i)
    {
        status = lcd_command(init_commands[i].command, init_commands[i].data,
                             init_commands[i].length);
        if (status != HAL_OK) { return status; }
    }
    status = lcd_command(0x11, NULL, 0U);
    if (status != HAL_OK) { return status; }
    HAL_Delay(120U);
    status = lcd_command(0x29, NULL, 0U);
    if (status != HAL_OK) { return status; }
    HAL_Delay(20U);
    lcd_ready = true;
    status = GC9A01_Clear(GC9A01_COLOR_BLACK);
    if (status != HAL_OK) { lcd_ready = false; }
    return status;
}
