#include "bsp_gc9a01.h"
#include "main.h"
#include "spi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef fake_gpio_a, fake_gpio_b;
SPI_TypeDef fake_spi1;
SPI_HandleTypeDef hspi1;
TIM_TypeDef fake_tim6 = {TIM_CR1_CEN, 995U, 999U};
uint32_t fake_ipsr;

static uint32_t fake_us, changed_at, cs_epoch, ramwr_epoch, ramwr_count;
static uint8_t command, shift_byte, bit_count, parameter_count, parameters[4], colmod;
static uint16_t x0, x1, y0, y1;
static uint32_t wire_size;
static uint8_t wire[GC9A01_WIDTH * GC9A01_HEIGHT * 2U];
static uint16_t frame[GC9A01_WIDTH * GC9A01_HEIGHT];
static bool fail_init;
static uint32_t transfer_polls, last_polled_size;

void fake_nop(void)
{
    ++fake_us;
    TIM6->CNT = (TIM6->CNT + 1U) % (TIM6->ARR + 1U);
}

void HAL_Delay(uint32_t ms)
{
    fake_us += ms * 1000U;
    TIM6->CNT = (TIM6->CNT + ms * 1000U) % (TIM6->ARR + 1U);
}

static bool cs_high(void) { return (GPIOA->output & GPIO_PIN_4) != 0U; }

static void service_during_transfer(void)
{
    assert(!cs_high() && command == 0x2CU && cs_epoch == ramwr_epoch);
    assert(bit_count == 0U && (GPIOA->output & GPIO_PIN_5) == 0U);
    assert(wire_size - last_polled_size == 2U && (wire_size & 1U) == 0U);
    last_polled_size = wire_size;
    ++transfer_polls;
}

/* Independent receiver: decode the GPIO waveform on each rising SCK edge. */
static void receive_byte(uint8_t value)
{
    if ((GPIOB->output & LCD_DCX_Pin) == 0U)
    {
        command = value;
        parameter_count = 0U;
        if (value == 0x2C) { ramwr_epoch = cs_epoch; ++ramwr_count; }
    }
    else if (command == 0x2C)
    {
        assert(cs_epoch == ramwr_epoch);
        assert(wire_size < sizeof(wire));
        wire[wire_size++] = value;
    }
    else if (command == 0x2A || command == 0x2B)
    {
        assert(parameter_count < 4U);
        parameters[parameter_count++] = value;
        if (parameter_count == 4U)
        {
            const uint16_t start = ((uint16_t)parameters[0] << 8) | parameters[1];
            const uint16_t end = ((uint16_t)parameters[2] << 8) | parameters[3];
            if (command == 0x2A) { x0 = start; x1 = end; }
            else { y0 = start; y1 = end; }
        }
    }
    else if (command == 0x3A) { colmod = value; }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    const uint32_t before = port->output;
    if (state == GPIO_PIN_SET) { port->output |= pin; }
    else { port->output &= ~pin; }
    if (port == GPIOA && (pin & GPIO_PIN_12) != 0U &&
        ((before ^ port->output) & GPIO_PIN_12) != 0U) { changed_at = fake_us; }
    if (port == GPIOA && pin == GPIO_PIN_4)
    {
        assert(bit_count == 0U);
        if (state == GPIO_PIN_RESET && (before & pin) != 0U) { ++cs_epoch; }
    }
    if (port == GPIOA && pin == GPIO_PIN_5 && state == GPIO_PIN_SET &&
        (before & GPIO_PIN_5) == 0U && !cs_high())
    {
        assert((uint32_t)(fake_us - changed_at) >= 20U);
        shift_byte = (uint8_t)((shift_byte << 1) | ((GPIOA->output & GPIO_PIN_12) != 0U));
        if (++bit_count == 8U)
        {
            receive_byte(shift_byte);
            bit_count = 0U;
            shift_byte = 0U;
        }
    }
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
    (void)spi; (void)data; (void)count; (void)timeout;
    assert(!"Software SPI must use GPIO, not the hardware transmitter");
    return HAL_ERROR;
}

static void reset_capture(void) { wire_size = ramwr_count = 0U; }

static void check_released(void)
{
    assert(cs_high() && bit_count == 0U);
    assert((GPIOA->output & GPIO_PIN_5) == 0U);
    assert(hspi1.Init.DataSize == SPI_DATASIZE_8BIT);
    assert((SPI1->CFG1 & SPI_CFG1_DSIZE) == SPI_DATASIZE_8BIT);
    assert((SPI1->CR1 & SPI_CR1_SPE) == 0U);
}

int main(void)
{
    hspi1.Instance = SPI1;
    hspi1.State = HAL_SPI_STATE_READY;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.BaudRatePrescaler = 4U;
    assert(GC9A01_Init() == HAL_OK);
    assert(colmod == 0x05U);
    assert(GPIOA->mode[5] == GPIO_MODE_OUTPUT_PP && GPIOA->mode[12] == GPIO_MODE_OUTPUT_PP);
    assert(hspi1.Init.BaudRatePrescaler == 4U);
    assert(wire_size == sizeof(wire) && ramwr_count == 1U);
    for (uint32_t i = 0U; i < wire_size; ++i) { assert(wire[i] == 0U); }
    check_released();

    const uint16_t pixels[] = {0xF800, 0x07E0, 0xDEAD, 0x001F, 0xFFFF};
    const uint8_t expected[] = {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF};
    reset_capture();
    assert(GC9A01_WriteRectRGB565(17U, 21U, 2U, 2U, pixels, 3U) == HAL_OK);
    assert(x0 == 17U && x1 == 18U && y0 == 21U && y1 == 22U);
    assert(wire_size == sizeof(expected) && memcmp(wire, expected, sizeof(expected)) == 0);
    check_released();

    reset_capture();
    for (uint32_t i = 0U; i < GC9A01_WIDTH * GC9A01_HEIGHT; ++i) { frame[i] = (uint16_t)(i * 37U); }
    GC9A01_SetTransferCallback(service_during_transfer);
    assert(GC9A01_Refresh(frame) == HAL_OK);
    GC9A01_SetTransferCallback(NULL);
    assert(transfer_polls == GC9A01_WIDTH * GC9A01_HEIGHT && last_polled_size == wire_size);
    assert(wire_size == sizeof(wire) && ramwr_count == 1U);
    assert(x0 == 0U && y0 == 0U && x1 == 239U && y1 == 239U);
    for (uint32_t i = 0U; i < GC9A01_WIDTH * GC9A01_HEIGHT; ++i)
    {
        assert(wire[2U * i] == (uint8_t)(frame[i] >> 8));
        assert(wire[2U * i + 1U] == (uint8_t)frame[i]);
    }
    check_released();

    reset_capture();
    assert(GC9A01_FillRect(3U, 5U, 17U, 19U, 0xA53CU) == HAL_OK);
    assert(wire_size == 17U * 19U * 2U && ramwr_count == 1U);
    for (uint32_t i = 0U; i < wire_size; i += 2U) { assert(wire[i] == 0xA5U && wire[i + 1U] == 0x3CU); }
    check_released();

    reset_capture();
    assert(GC9A01_Refresh(NULL) == HAL_ERROR);
    assert(GC9A01_WriteRectRGB565(239U, 0U, 2U, 1U, pixels, 2U) == HAL_ERROR);
    fake_ipsr = 5U;
    assert(GC9A01_Clear(0U) == HAL_ERROR);
    fake_ipsr = 0U;
    hspi1.State = 2U;
    assert(GC9A01_Clear(0U) == HAL_BUSY);
    hspi1.State = HAL_SPI_STATE_READY;
    assert(wire_size == 0U && ramwr_count == 0U);

    TIM6->CR1 = 0U;
    assert(GC9A01_Init() == HAL_ERROR);
    TIM6->CR1 = TIM_CR1_CEN;
    fail_init = true;
    assert(GC9A01_Init() == HAL_ERROR);
    fail_init = false;
    assert(GC9A01_Init() == HAL_OK);
    check_released();
    puts("PASS: GPIO SPI RGB565, full frame/stride/fill, cooperative service, SDA settling across timer wrap, bounds/state");
    return 0;
}
