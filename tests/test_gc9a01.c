#include "bsp_gc9a01.h"
#include "main.h"
#include "spi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef fake_gpio_a, fake_gpio_b;
SPI_TypeDef fake_spi1;
SPI_HandleTypeDef hspi1;
uint32_t fake_ipsr;

static uint8_t wire_pixels[GC9A01_WIDTH * GC9A01_HEIGHT * 2U];
static uint16_t framebuffer[GC9A01_WIDTH * GC9A01_HEIGHT];
static uint32_t wire_size, calls, fail_at, cs_epoch, ramwr_epoch, ramwr_count;
static uint32_t pixel_calls, largest_pixel_call, elapsed_ms, reset_at, reset_delay;
static uint16_t x0, x1, y0, y1;
static uint8_t command, colmod;
static bool fail_init;
static uint32_t transfer_polls, last_polled_size;

static bool cs_high(void) { return (GPIOA->output & GPIO_PIN_4) != 0U; }

static void service_during_transfer(void)
{
    /* Cooperative work must run inside RAMWR without splitting a pixel. */
    assert(!cs_high() && command == 0x2CU && cs_epoch == ramwr_epoch);
    assert(wire_size > last_polled_size && (wire_size & 1U) == 0U);
    assert(wire_size - last_polled_size <= 4096U * 2U);
    last_polled_size = wire_size;
    ++transfer_polls;
}

void HAL_Delay(uint32_t ms) { elapsed_ms += ms; }

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port == GPIOA && pin == GPIO_PIN_4 && state == GPIO_PIN_RESET && cs_high())
    {
        ++cs_epoch;
    }
    if (port == LCD_RST_GPIO_Port && pin == LCD_RST_Pin)
    {
        if (state == GPIO_PIN_RESET) { reset_at = elapsed_ms; }
        else { reset_delay = elapsed_ms - reset_at; }
    }
    if (state == GPIO_PIN_SET) { port->output |= pin; }
    else { port->output &= ~pin; }
}

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *gpio)
{
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        if ((gpio->Pin & (1UL << i)) != 0U) { port->mode[i] = gpio->Mode; }
    }
}

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *spi)
{
    if (fail_init) { return HAL_ERROR; }
    spi->State = HAL_SPI_STATE_READY;
    spi->Instance->CFG1 = spi->Init.DataSize;
    spi->Instance->CR1 = 0U;
    return HAL_OK;
}

uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *spi) { return spi->State; }

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                  uint16_t count, uint32_t timeout)
{
    assert(spi == &hspi1 && data != NULL && count != 0U && timeout != 0U);
    assert(!cs_high());
    assert((spi->Instance->CFG1 & SPI_CFG1_DSIZE) == spi->Init.DataSize);
    ++calls;
    /* Model the blocking HAL's close procedure, including on timeout. */
    spi->Instance->CR1 &= ~SPI_CR1_SPE;
    if (fail_at == calls) { return HAL_TIMEOUT; }
    if ((GPIOB->output & LCD_DCX_Pin) == 0U)
    {
        assert(spi->Init.DataSize == SPI_DATASIZE_8BIT && count == 1U);
        command = *data;
        if (command == 0x2C)
        {
            ramwr_epoch = cs_epoch;
            ++ramwr_count;
        }
    }
    else if (command == 0x2C)
    {
        assert(spi->Init.DataSize == SPI_DATASIZE_16BIT);
        assert(cs_epoch == ramwr_epoch); /* No CS edge from RAMWR to the last pixel. */
        const uint16_t *pixels = (const uint16_t *)data;
        ++pixel_calls;
        if (count > largest_pixel_call) { largest_pixel_call = count; }
        assert(wire_size + 2U * count <= sizeof(wire_pixels));
        for (uint32_t i = 0U; i < count; ++i)
        {
            wire_pixels[wire_size++] = (uint8_t)(pixels[i] >> 8);
            wire_pixels[wire_size++] = (uint8_t)pixels[i];
        }
    }
    else
    {
        assert(spi->Init.DataSize == SPI_DATASIZE_8BIT);
        if (command == 0x2A || command == 0x2B)
        {
            assert(count == 4U);
            const uint16_t start = ((uint16_t)data[0] << 8) | data[1];
            const uint16_t end = ((uint16_t)data[2] << 8) | data[3];
            if (command == 0x2A) { x0 = start; x1 = end; }
            else { y0 = start; y1 = end; }
        }
        if (command == 0x3A) { assert(count == 1U); colmod = data[0]; }
    }
    return HAL_OK;
}

static void capture_reset(void)
{
    wire_size = calls = fail_at = ramwr_count = pixel_calls = largest_pixel_call = 0U;
}

static void assert_released(void)
{
    assert(cs_high());
    assert(hspi1.Init.DataSize == SPI_DATASIZE_8BIT);
    assert((SPI1->CFG1 & SPI_CFG1_DSIZE) == SPI_DATASIZE_8BIT);
    assert((SPI1->CR1 & SPI_CR1_SPE) == 0U);
}

static void test_init(void)
{
    hspi1.Instance = SPI1;
    hspi1.State = HAL_SPI_STATE_READY;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.BaudRatePrescaler = 4U;
    assert(GC9A01_Refresh(framebuffer) == HAL_ERROR);
    assert(GC9A01_Init() == HAL_OK);
    assert(reset_delay >= 20U && elapsed_ms >= 280U);
    assert(hspi1.Init.BaudRatePrescaler == 4U);
    assert(hspi1.Init.NSS == SPI_NSS_SOFT);
    assert(hspi1.Init.NSSPMode == SPI_NSS_PULSE_DISABLE);
    assert(GPIOA->mode[4] == GPIO_MODE_OUTPUT_PP);
    assert(GPIOB->mode[0] == GPIO_MODE_OUTPUT_PP);
    assert((GPIOB->output & LCD_BL_Pin) == 0U);
    assert(colmod == 0x05U);
    assert(wire_size == sizeof(wire_pixels) && ramwr_count == 1U);
    for (uint32_t i = 0U; i < wire_size; ++i) { assert(wire_pixels[i] == 0U); }
    assert_released();
    GC9A01_SetBacklight(true);
    assert((GPIOB->output & LCD_BL_Pin) != 0U);
    GC9A01_SetBacklight(false);
    assert((GPIOB->output & LCD_BL_Pin) == 0U);
}

static void test_rect_and_stride(void)
{
    static const uint16_t pixels[] = {0xF800, 0x07E0, 0xDEAD, 0x001F, 0xFFFF};
    static const uint8_t expected[] = {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF};
    capture_reset();
    assert(GC9A01_WriteRectRGB565(17U, 21U, 2U, 2U, pixels, 3U) == HAL_OK);
    assert(x0 == 17U && x1 == 18U && y0 == 21U && y1 == 22U);
    assert(wire_size == sizeof(expected) && memcmp(wire_pixels, expected, sizeof(expected)) == 0);
    assert(ramwr_count == 1U && pixel_calls == 2U);
    assert_released();
    capture_reset();
    assert(GC9A01_WriteRectRGB565(239U, 239U, 1U, 1U, pixels, 1U) == HAL_OK);
    assert(x0 == 239U && x1 == 239U && y0 == 239U && y1 == 239U);
    assert_released();
}

static void test_full_frame(void)
{
    for (uint32_t i = 0U; i < GC9A01_WIDTH * GC9A01_HEIGHT; ++i)
    {
        framebuffer[i] = (uint16_t)(i * 37U);
    }
    capture_reset();
    GC9A01_SetTransferCallback(service_during_transfer);
    assert(GC9A01_Refresh(framebuffer) == HAL_OK);
    GC9A01_SetTransferCallback(NULL);
    assert(transfer_polls > 1U && last_polled_size == wire_size);
    assert(wire_size == sizeof(wire_pixels) && wire_size > 65535U);
    assert(x0 == 0U && y0 == 0U && x1 == 239U && y1 == 239U);
    assert(ramwr_count == 1U && pixel_calls > 1U && largest_pixel_call <= 4096U);
    for (uint32_t i = 0U; i < GC9A01_WIDTH * GC9A01_HEIGHT; ++i)
    {
        assert(wire_pixels[2U * i] == (uint8_t)(framebuffer[i] >> 8));
        assert(wire_pixels[2U * i + 1U] == (uint8_t)framebuffer[i]);
    }
    assert_released();
}

static void test_fill(void)
{
    capture_reset();
    assert(GC9A01_FillRect(3U, 5U, 17U, 19U, 0xA53CU) == HAL_OK);
    assert(x0 == 3U && x1 == 19U && y0 == 5U && y1 == 23U);
    assert(wire_size == 17U * 19U * 2U && ramwr_count == 1U);
    for (uint32_t i = 0U; i < wire_size; i += 2U)
    {
        assert(wire_pixels[i] == 0xA5U && wire_pixels[i + 1U] == 0x3CU);
    }
    assert_released();
    capture_reset();
    assert(GC9A01_Clear(0x07E0U) == HAL_OK);
    assert(wire_size == sizeof(wire_pixels) && ramwr_count == 1U);
    for (uint32_t i = 0U; i < wire_size; i += 2U)
    {
        assert(wire_pixels[i] == 0x07U && wire_pixels[i + 1U] == 0xE0U);
    }
    assert_released();
}

static void test_rejected_calls(void)
{
    capture_reset();
    assert(GC9A01_Refresh(NULL) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(240U, 0U, 1U, 1U, framebuffer, 1U) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(0U, 239U, 1U, 2U, framebuffer, 1U) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(0U, 0U, 0U, 1U, framebuffer, 1U) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(0U, 0U, 2U, 1U, framebuffer, 1U) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(0U, 0U, 1U, 1U,
           (const uint16_t *)((const uint8_t *)framebuffer + 1U), 1U) == HAL_ERROR);
    assert(GC9A01_FillRect(0U, 0U, 1U, 0U, 0U) == HAL_ERROR);
    hspi1.State = 2U;
    assert(GC9A01_Clear(0U) == HAL_BUSY);
    hspi1.State = HAL_SPI_STATE_READY;
    SPI1->CR1 |= SPI_CR1_SPE;
    assert(GC9A01_Refresh(framebuffer) == HAL_BUSY);
    SPI1->CR1 = 0U;
    fake_ipsr = 5U;
    assert(GC9A01_Refresh(framebuffer) == HAL_ERROR);
    assert(GC9A01_Init() == HAL_ERROR);
    fake_ipsr = 0U;
    assert(calls == 0U);
    assert_released();
}

static void test_error_cleanup(void)
{
    /* Fail each window/command/pixel stage, then succeed with the same driver. */
    for (uint32_t stage = 1U; stage <= 7U; ++stage)
    {
        capture_reset();
        fail_at = stage;
        assert(GC9A01_Refresh(framebuffer) == HAL_TIMEOUT);
        assert_released();
        capture_reset();
        assert(GC9A01_WriteRectRGB565(0U, 0U, 1U, 1U, framebuffer, 1U) == HAL_OK);
        assert_released();
    }
    capture_reset();
    fail_at = 7U;
    assert(GC9A01_Clear(0xF800U) == HAL_TIMEOUT);
    assert_released();
    capture_reset();
    fail_at = 3U;
    assert(GC9A01_Init() == HAL_TIMEOUT);
    assert_released();
    capture_reset();
    assert(GC9A01_Refresh(framebuffer) == HAL_ERROR && calls == 0U);
    fail_init = true;
    assert(GC9A01_Init() == HAL_ERROR);
    fail_init = false;
    assert(GC9A01_Init() == HAL_OK);
    assert_released();
}

int main(void)
{
    test_init();
    test_rect_and_stride();
    test_full_frame();
    test_fill();
    test_rejected_calls();
    test_error_cleanup();
    puts("PASS: GC9A01 init, RGB565/stride, full frame, cooperative service, fill, bounds/busy/ISR, failure recovery");
    return 0;
}
