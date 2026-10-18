#ifndef DISPLAY_TEST_GENERATED_HAL_HPP
#define DISPLAY_TEST_GENERATED_HAL_HPP
#include <cstring>
#include <touchgfx/hal/HAL.hpp>
void testNotifyDrawRecorded(const touchgfx::Rect &rect);
void testNotifyRenderingDone();
class TouchGFXGeneratedHAL : public touchgfx::HAL
{
public:
    TouchGFXGeneratedHAL(touchgfx::DMA_Interface &, touchgfx::LCD &display,
                        touchgfx::TouchController &, uint16_t width, uint16_t height)
        : touchgfx::HAL(display, width, height) {}
    virtual void initialize() {}
    virtual void configureInterrupts() {}
    virtual void enableInterrupts() {}
    virtual void disableInterrupts() {}
    virtual void enableLCDControllerInterrupt() {}
    virtual bool beginFrame() { return true; }
    virtual void endFrame() { testNotifyRenderingDone(); }
    virtual uint16_t *getTFTFrameBuffer() const { return front_buffer; }
    virtual void setTFTFrameBuffer(uint16_t *address) { front_buffer = address; }
    virtual void flushFrameBuffer()
    {
        flushFrameBuffer(touchgfx::Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT));
    }
    virtual void flushFrameBuffer(const touchgfx::Rect &rect)
    {
        testNotifyDrawRecorded(rect);
    }
    virtual bool blockCopy(void *dest, const void *src, uint32_t size)
    {
        std::memcpy(dest, src, size);
        return true;
    }
};
#endif
