/* ILI9341 4-wire SPI driver. CS is tied to ground; DCX selects command/data. */
#include "bsp_ili9341_4line.h"
#include "main.h"
#include "spi.h"
#include <stddef.h>

#define LCD_TRANSFER_BYTES 1024U
#define LCD_TIMEOUT_MS 20U
#define LCD_FRAME_BYTES (240U * 320U * 2U)

static uint16_t lcd_width;
static uint16_t lcd_height;
static uint8_t fill_buffer[LCD_TRANSFER_BYTES];
static const uint8_t *transfer_data;
static uint32_t transfer_offset;
static uint8_t transfer_repeat;

extern const unsigned char gImage_yuexinmao1[LCD_FRAME_BYTES];
extern const unsigned char gImage_yuexinmao2[LCD_FRAME_BYTES];
extern const unsigned char gImage_yuexinmao3[LCD_FRAME_BYTES];
extern const unsigned char gImage_yuexinmao4[LCD_FRAME_BYTES];
static const uint8_t *const lcd_pictures[LCD_PICTURE_COUNT] = {
    gImage_yuexinmao1, gImage_yuexinmao2, gImage_yuexinmao3, gImage_yuexinmao4
};

static HAL_StatusTypeDef lcd_send(const uint8_t *data, uint16_t size)
{
    return HAL_SPI_Transmit(&hspi1, data, size, LCD_TIMEOUT_MS);
}

static HAL_StatusTypeDef lcd_command(uint8_t command, const uint8_t *data, uint16_t size)
{
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = lcd_send(&command, 1U);
    HAL_GPIO_WritePin(LCD_DCX_GPIO_Port, LCD_DCX_Pin, GPIO_PIN_SET);
    if (status == HAL_OK && size != 0U)
    {
        status = lcd_send(data, size);
    }
    return status;
}

HAL_StatusTypeDef ILI9341_SetArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (transfer_data != NULL)
    {
        return HAL_BUSY;
    }
    if (x0 > x1 || y0 > y1 || x1 >= lcd_width || y1 >= lcd_height)
    {
        return HAL_ERROR;
    }

    /* Always set the complete window, so no software/hardware cache can diverge. */
    const uint8_t columns[4] = {x0 >> 8, x0 & 0xFFU, x1 >> 8, x1 & 0xFFU};
    const uint8_t rows[4] = {y0 >> 8, y0 & 0xFFU, y1 >> 8, y1 & 0xFFU};
    HAL_StatusTypeDef status = lcd_command(0x2A, columns, sizeof(columns));
    if (status == HAL_OK)
    {
        status = lcd_command(0x2B, rows, sizeof(rows));
    }
    return status;
}

static HAL_StatusTypeDef lcd_begin_write(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    HAL_StatusTypeDef status = ILI9341_SetArea(x0, y0, x1, y1);
    if (status == HAL_OK)
    {
        status = lcd_command(0x2C, NULL, 0U);
    }
    return status;
}

HAL_StatusTypeDef ILI9341_Display_Dir(Screen_ShowDIR direction)
{
    if (transfer_data != NULL)
    {
        return HAL_BUSY;
    }
    if (direction != SCAN_Vertical && direction != SCAN_Horizontal)
    {
        return HAL_ERROR;
    }

    const uint8_t madctl = direction == SCAN_Vertical ? 0x08U : 0xA8U;
    HAL_StatusTypeDef status = lcd_command(0x36, &madctl, 1U);
    if (status != HAL_OK)
    {
        return status;
    }
    lcd_width = direction == SCAN_Vertical ? 240U : 320U;
    lcd_height = direction == SCAN_Vertical ? 320U : 240U;
    return ILI9341_SetArea(0U, 0U, lcd_width - 1U, lcd_height - 1U);
}

HAL_StatusTypeDef LCD_ColorStart(uint16_t color)
{
    if (lcd_width == 0U || lcd_height == 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = lcd_begin_write(0U, 0U, lcd_width - 1U, lcd_height - 1U);
    if (status != HAL_OK)
    {
        return status;
    }
    for (uint16_t i = 0U; i < LCD_TRANSFER_BYTES; i += 2U)
    {
        fill_buffer[i] = (uint8_t)(color >> 8);
        fill_buffer[i + 1U] = (uint8_t)color;
    }
    transfer_data = fill_buffer;
    transfer_offset = 0U;
    transfer_repeat = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef ILI9341_Clear(uint16_t color)
{
    HAL_StatusTypeDef status = LCD_ColorStart(color);
    if (status != HAL_OK)
    {
        return status;
    }
    do {
        status = LCD_TransferTask();
    } while (status == HAL_BUSY);
    return status;
}

HAL_StatusTypeDef ILI9341_Init(void)
{
    static const struct {
        uint8_t command;
        uint8_t size;
        uint8_t data[15];
    } commands[] = {
        {0xCF, 3, {0x00, 0xC1, 0x30}},
        {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
        {0xE8, 3, {0x85, 0x10, 0x7A}},
        {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
        {0xF7, 1, {0x20}},
        {0xEA, 2, {0x00, 0x00}},
        {0xC0, 1, {0x1B}},
        {0xC1, 1, {0x01}},
        {0xC5, 2, {0x30, 0x30}},
        {0xC7, 1, {0xB7}},
        {0x3A, 1, {0x55}},
        {0xB1, 2, {0x00, 0x10}},
        {0xB6, 2, {0x0A, 0xA2}},
        {0xF2, 1, {0x00}},
        {0x26, 1, {0x01}},
        {0xE0, 15, {0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00}},
        {0xE1, 15, {0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F}}
    };

    lcd_width = lcd_height = 0U;
    transfer_data = NULL;
    transfer_offset = 0U;
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(80);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);

    for (uint32_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i)
    {
        HAL_StatusTypeDef status = lcd_command(commands[i].command, commands[i].data, commands[i].size);
        if (status != HAL_OK)
        {
            return status;
        }
    }
    HAL_StatusTypeDef status = lcd_command(0x11, NULL, 0U);
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_Delay(120); /* Sleep Out must settle before any subsequent command. */
    status = lcd_command(0x21, NULL, 0U);
    if (status == HAL_OK)
    {
        status = ILI9341_Display_Dir(SCAN_Vertical);
    }
    if (status == HAL_OK)
    {
        status = lcd_command(0x29, NULL, 0U);
    }
    if (status == HAL_OK)
    {
        status = ILI9341_Clear(BLACK);
    }
    return status;
}

HAL_StatusTypeDef LCD_PictureStart(uint8_t index)
{
    if (index >= LCD_PICTURE_COUNT || lcd_width != 240U || lcd_height != 320U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = lcd_begin_write(0U, 0U, lcd_width - 1U, lcd_height - 1U);
    if (status == HAL_OK)
    {
        transfer_data = lcd_pictures[index];
        transfer_offset = 0U;
        transfer_repeat = 0U;
    }
    return status;
}

HAL_StatusTypeDef LCD_TransferTask(void)
{
    if (transfer_data == NULL)
    {
        return HAL_OK;
    }
    uint32_t remaining = LCD_FRAME_BYTES - transfer_offset;
    uint16_t size = remaining > LCD_TRANSFER_BYTES ? LCD_TRANSFER_BYTES : (uint16_t)remaining;
    const uint8_t *data = transfer_repeat ? transfer_data : transfer_data + transfer_offset;
    HAL_StatusTypeDef status = lcd_send(data, size);
    if (status != HAL_OK)
    {
        transfer_data = NULL;
        return status == HAL_BUSY ? HAL_ERROR : status;
    }
    transfer_offset += size;
    if (transfer_offset == LCD_FRAME_BYTES)
    {
        transfer_data = NULL;
        return HAL_OK;
    }
    return HAL_BUSY;
}
