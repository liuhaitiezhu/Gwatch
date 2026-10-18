#ifndef DISPLAY_TEST_TGFX_HAL_HPP
#define DISPLAY_TEST_TGFX_HAL_HPP
#include <touchgfx/hal/Types.hpp>
void testWaitForRenderComplete();
namespace touchgfx
{
class DMA_Interface {};
class TouchController {};
class LCD
{
public:
    uint16_t stride = 480;
    uint16_t framebufferStride() const { return stride; }
};
// Mock the renderer only. Tests compile the real adapter and ILI9341 driver.
class HAL
{
public:
    static uint16_t DISPLAY_WIDTH;
    static uint16_t DISPLAY_HEIGHT;
    static HAL *instance;
    HAL(LCD &display, uint16_t width, uint16_t height) : display(display)
    {
        instance = this;
        DISPLAY_WIDTH = width;
        DISPLAY_HEIGHT = height;
    }
    static HAL *getInstance() { return instance; }
    void vSync() {}
    void lockUnlockFrameBuffer() { testWaitForRenderComplete(); }
    void setTestFramebuffers(uint16_t *client, uint16_t *front)
    {
        client_buffer = client;
        front_buffer = front;
    }
protected:
    LCD &lcd() { return display; }
    uint16_t *getClientFrameBuffer() { return client_buffer; }
    LCD &display;
    uint16_t *client_buffer = 0;
    uint16_t *front_buffer = 0;
};
}
#endif
