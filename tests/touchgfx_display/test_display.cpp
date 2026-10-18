#include <cassert>
#include <cstdio>
#include <vector>
#include <TouchGFXHAL.hpp>
#include "bsp_ili9341_4line.h"
#include "main.h"
#include "spi.h"
#include "touchgfx_vsync.h"
#include "semphr.h"
#include "task.h"
#include <touchgfx/hal/OSWrappers.hpp>

uint16_t touchgfx::HAL::DISPLAY_WIDTH;
uint16_t touchgfx::HAL::DISPLAY_HEIGHT;
touchgfx::HAL *touchgfx::HAL::instance;
GPIO_TypeDef test_port = {};
SPI_HandleTypeDef hspi1 = {};
extern "C" const unsigned char gImage_yuexinmao1[240 * 320 * 2] = {};
extern "C" const unsigned char gImage_yuexinmao2[240 * 320 * 2] = {};
extern "C" const unsigned char gImage_yuexinmao3[240 * 320 * 2] = {};
extern "C" const unsigned char gImage_yuexinmao4[240 * 320 * 2] = {};

struct Packet { bool data; std::vector<uint8_t> bytes; };
struct DisplayFailure {};
static std::vector<Packet> packets;
static bool dc;
static bool transmitting;
static unsigned spi_calls;
static unsigned fail_at;
static unsigned notifications;
static size_t expected_bytes;
static SemaphoreHandle_t te_semaphore;
static BaseType_t scheduler_state = taskSCHEDULER_RUNNING;
static uint32_t isr_depth;
static unsigned te_gives;
static unsigned te_waits;
static unsigned render_ends;
static bool fail_semaphore_creation;
static bool rendering_done;
static bool framebuffer_ready;
static bool require_te;
static bool transfer_allowed;
static bool gui_vsync;
static bool inject_te_during_send;
enum TeArrival { DuringWait, BetweenDrainAndWait, NoPulse };
static TeArrival te_arrival = DuringWait;

static void fireTe()
{
    const unsigned calls = spi_calls;
    assert(isr_depth == 0);
    isr_depth = 1;
    TouchGFX_NotifyVSync();
    isr_depth = 0;
    assert(spi_calls == calls); // TE ISR must never send pixels.
}

extern "C" uint32_t __get_IPSR(void) { return isr_depth; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler_state; }
void testYieldFromISR(BaseType_t) { assert(isr_depth != 0); }
void testSignalVSync() { gui_vsync = true; }

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage)
{
    if (fail_semaphore_creation) { return NULL; }
    storage->available = false;
    te_semaphore = storage;
    return storage;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken)
{
    assert(isr_depth != 0 && scheduler_state == taskSCHEDULER_RUNNING);
    assert(semaphore == te_semaphore);
    ++te_gives;
    if (semaphore->available) { return pdFALSE; }
    semaphore->available = true;
    *woken = pdTRUE;
    return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    assert(isr_depth == 0 && scheduler_state == taskSCHEDULER_RUNNING);
    assert(semaphore == te_semaphore && rendering_done && framebuffer_ready);
    assert(packets.empty()); // Even address commands must wait for the fresh TE.
    if (timeout == 0)
    {
        const bool available = semaphore->available;
        semaphore->available = false;
        if (te_arrival == BetweenDrainAndWait) { fireTe(); }
        return available ? pdTRUE : pdFALSE;
    }
    assert(timeout == pdMS_TO_TICKS(100));
    ++te_waits;
    if (te_arrival == DuringWait) { fireTe(); }
    if (!semaphore->available) { return pdFALSE; }
    semaphore->available = false;
    transfer_allowed = true;
    return pdTRUE;
}

void testNotifyRenderingDone()
{
    ++render_ends;
    rendering_done = true;
    gui_vsync = false; // Same bookkeeping as the generated endFrame implementation.
}

void testWaitForRenderComplete()
{
    assert(rendering_done && packets.empty());
    framebuffer_ready = true;
}

extern "C" void HAL_Delay(uint32_t) {}
extern "C" void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert(port == &test_port && !transmitting);
    if (pin == LCD_DCX_Pin) { dc = state == GPIO_PIN_SET; }
    else { assert(pin == LCD_RST_Pin); }
}
extern "C" HAL_StatusTypeDef HAL_SPI_Transmit(
    SPI_HandleTypeDef *handle, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(handle == &hspi1 && !transmitting && data != NULL && size > 0);
    assert(size <= 1024 && timeout == 20);
    assert(isr_depth == 0);
    if (require_te) { assert(transfer_allowed && framebuffer_ready); }
    ++spi_calls;
    if (inject_te_during_send && spi_calls == 6) { fireTe(); }
    if (spi_calls == fail_at) { return HAL_TIMEOUT; }
    transmitting = true;
    packets.push_back(Packet{dc, std::vector<uint8_t>(data, data + size)});
    transmitting = false;
    return HAL_OK;
}
extern "C" void Error_Handler(void) { throw DisplayFailure(); }

static std::vector<uint8_t> pixelBytes()
{
    std::vector<uint8_t> result;
    for (size_t i = 5; i < packets.size(); ++i)
    {
        assert(packets[i].data && packets[i].bytes.size() % 2 == 0);
        result.insert(result.end(), packets[i].bytes.begin(), packets[i].bytes.end());
    }
    return result;
}

void testNotifyDrawRecorded(const touchgfx::Rect &)
{
    assert(!transmitting);
    assert(packets.empty()); // Drawing notifications only collect areas now.
    ++notifications;
}

static void reset(size_t bytes = 0)
{
    packets.clear();
    spi_calls = fail_at = notifications = 0;
    expected_bytes = bytes;
    te_waits = te_gives = render_ends = 0;
    rendering_done = framebuffer_ready = require_te = transfer_allowed = false;
    gui_vsync = inject_te_during_send = false;
    te_arrival = DuringWait;
}

static void expectWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    assert(packets.size() >= 6);
    assert(!packets[0].data && packets[0].bytes == std::vector<uint8_t>{0x2A});
    assert(packets[1].data);
    assert((packets[1].bytes == std::vector<uint8_t>{
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
        static_cast<uint8_t>((x + width - 1) >> 8), static_cast<uint8_t>(x + width - 1)}));
    assert(!packets[2].data && packets[2].bytes == std::vector<uint8_t>{0x2B});
    assert(packets[3].data);
    assert((packets[3].bytes == std::vector<uint8_t>{
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
        static_cast<uint8_t>((y + height - 1) >> 8), static_cast<uint8_t>(y + height - 1)}));
    assert(!packets[4].data && packets[4].bytes == std::vector<uint8_t>{0x2C});
}

static std::vector<uint8_t> expectedPixels(const std::vector<uint16_t> &buffer,
                                         uint16_t stride, const touchgfx::Rect &rect)
{
    std::vector<uint8_t> result;
    for (int row = rect.y; row < rect.bottom(); ++row)
    {
        for (int column = rect.x; column < rect.right(); ++column)
        {
            const uint16_t color = buffer[row * stride + column];
            result.push_back(static_cast<uint8_t>(color >> 8));
            result.push_back(static_cast<uint8_t>(color));
        }
    }
    return result;
}

static void expectFlush(TouchGFXHAL &hal, const std::vector<uint16_t> &buffer,
                        uint16_t stride, const touchgfx::Rect &request,
                        const touchgfx::Rect &clipped)
{
    const std::vector<uint16_t> original = buffer;
    const std::vector<uint8_t> expected = expectedPixels(buffer, stride, clipped);
    reset(expected.size());
    assert(hal.beginFrame());
    hal.flushFrameBuffer(request);
    assert(notifications == 1 && buffer == original);
    assert(packets.empty() && te_waits == 0);
    require_te = true;
    hal.endFrame();
    assert(render_ends == 1 && buffer == original);
    assert(pixelBytes().size() == expected_bytes);
    if (!clipped.isEmpty())
    {
        assert(te_waits == 1 && gui_vsync);
        expectWindow(clipped.x, clipped.y, clipped.width, clipped.height);
        assert(pixelBytes() == expected);
    }
    else { assert(packets.empty() && te_waits == 0); }
}

int main()
{
    // Uninitialized geometry and invalid arguments must not send commands.
    const uint16_t colors[] = {0xF800, 0x07E0, 0xDEAD, 0xBEEF,
                               0x001F, 0xFFFF, 0xCAFE, 0xBABE};
    assert(ILI9341_WriteRectRGB565(0, 0, 2, 2, colors, 4) == HAL_ERROR);
    assert(packets.empty());
    assert(ILI9341_Init() == HAL_OK);
    bool found_frame_rate = false;
    for (size_t i = 0; i + 1 < packets.size(); ++i)
    {
        if (!packets[i].data && packets[i].bytes == std::vector<uint8_t>{0xB1})
        {
            assert(packets[i + 1].data);
            assert((packets[i + 1].bytes == std::vector<uint8_t>{0x00, 0x1F}));
            found_frame_rate = true;
        }
    }
    assert(found_frame_rate);
    reset();
    assert(ILI9341_WriteRectRGB565(10, 20, 2, 2, colors, 4) == HAL_OK);
    expectWindow(10, 20, 2, 2);
    assert((pixelBytes() == std::vector<uint8_t>{0xF8, 0, 0x07, 0xE0, 0, 0x1F, 0xFF, 0xFF}));
    reset();
    assert(ILI9341_WriteRectRGB565(0, 0, 0, 1, colors, 4) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(0, 0, 1, 0, colors, 4) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(0, 0, 2, 1, colors, 1) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(0, 0, 1, 1, NULL, 1) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(239, 319, 2, 1, colors, 4) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(239, 319, 1, 2, colors, 4) == HAL_ERROR);
    assert(ILI9341_WriteRectRGB565(65535, 0, 2, 1, colors, 4) == HAL_ERROR);
    assert(packets.empty());

    touchgfx::DMA_Interface dma;
    touchgfx::LCD display;
    touchgfx::TouchController touch;
    TouchGFXHAL hal(dma, display, touch, 240, 320);
    reset();
    fail_semaphore_creation = true;
    try { hal.initialize(); assert(false); }
    catch (const DisplayFailure &) {}
    fail_semaphore_creation = false;
    hal.initialize();
    scheduler_state = taskSCHEDULER_NOT_STARTED;
    fireTe(); // Before scheduler start, GUI notification only; no RTOS ISR call.
    assert(gui_vsync && te_gives == 0);
    scheduler_state = taskSCHEDULER_RUNNING;
    std::vector<uint16_t> buffer(244 * 320, 0xDEAD);
    std::vector<uint16_t> front(244 * 320, 0xBEEF);
    for (int row = 0; row < 320; ++row)
    {
        for (int column = 0; column < 240; ++column)
        { buffer[row * 244 + column] = static_cast<uint16_t>(row * 37 + column * 13); }
    }
    display.stride = 244 * 2; // Detect code that assumes tightly packed rows.
    hal.setTestFramebuffers(buffer.data(), front.data());
    expectFlush(hal, buffer, 244, touchgfx::Rect(10, 20, 3, 2), touchgfx::Rect(10, 20, 3, 2));
    expectFlush(hal, buffer, 244, touchgfx::Rect(-2, -1, 4, 3), touchgfx::Rect(0, 0, 2, 2));
    expectFlush(hal, buffer, 244, touchgfx::Rect(239, 319, 5, 5), touchgfx::Rect(239, 319, 1, 1));
    expectFlush(hal, buffer, 244, touchgfx::Rect(300, 100, 10, 10), touchgfx::Rect());
    expectFlush(hal, buffer, 244, touchgfx::Rect(0, 0, 0, 0), touchgfx::Rect());
    expectFlush(hal, buffer, 244, touchgfx::Rect(0, 0, 240, 320), touchgfx::Rect(0, 0, 240, 320));
    assert(pixelBytes().size() == 153600 && packets.size() == 155);

    // All partial draws are collected before one fresh edge / one transfer.
    reset(6 * 7 * 2);
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(10, 20, 3, 2));
    fireTe(); // A TE in the middle of rendering is stale by endFrame.
    hal.flushFrameBuffer(touchgfx::Rect(14, 25, 2, 2));
    assert(packets.empty() && te_waits == 0 && notifications == 2);
    require_te = true;
    hal.endFrame();
    assert(te_waits == 1 && te_gives == 2 && render_ends == 1);
    expectWindow(10, 20, 6, 7);
    assert(pixelBytes() == expectedPixels(buffer, 244, touchgfx::Rect(10, 20, 6, 7)));

    // No-argument flush must collect the whole screen too.
    reset(153600);
    assert(hal.beginFrame());
    hal.flushFrameBuffer();
    assert(packets.empty());
    require_te = true;
    hal.endFrame();
    expectWindow(0, 0, 240, 320);
    assert(te_waits == 1 && pixelBytes() == expectedPixels(buffer, 244, touchgfx::Rect(0, 0, 240, 320)));

    // A TE arriving between draining the old token and blocking is not lost.
    reset(2);
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(0, 0, 1, 1));
    te_arrival = BetweenDrainAndWait;
    require_te = true;
    hal.endFrame();
    assert(te_waits == 1 && te_gives == 1 && pixelBytes().size() == 2);

    // An old TE plus a GUI VSYNC flag cannot release a new transfer.
    reset();
    fireTe();
    fireTe(); // A full binary semaphore must safely coalesce extra TE events.
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(0, 0, 1, 1));
    te_arrival = NoPulse;
    require_te = true;
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(te_waits == 1 && packets.empty() && !transfer_allowed);

    // TE pulses during a transfer must not release the following frame.
    reset(2);
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(0, 0, 1, 1));
    require_te = inject_te_during_send = true;
    hal.endFrame();
    assert(te_gives == 2 && te_semaphore->available && pixelBytes().size() == 2);
    reset();
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(0, 0, 1, 1));
    require_te = true;
    te_arrival = NoPulse;
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(packets.empty() && te_waits == 1);

    // Empty frames neither wait for TE nor resend a previous dirty rectangle.
    reset();
    assert(hal.beginFrame());
    hal.endFrame();
    assert(packets.empty() && te_waits == 0 && render_ends == 1);

    // Every command/data phase and a later pixel chunk can fail independently.
    for (unsigned failed = 1; failed <= 7; ++failed)
    {
        reset();
        assert(hal.beginFrame());
        hal.flushFrameBuffer(touchgfx::Rect(0, 0, 240, 4));
        fail_at = failed;
        require_te = true;
        bool caught = false;
        try { hal.endFrame(); }
        catch (const DisplayFailure &) { caught = true; }
        assert(caught && te_waits == 1 && spi_calls == failed);
    }
    expectFlush(hal, buffer, 244, touchgfx::Rect(0, 0, 1, 1), touchgfx::Rect(0, 0, 1, 1));

    reset();
    assert(LCD_ColorStart(0xF800) == HAL_OK);
    reset();
    assert(ILI9341_WriteRectRGB565(0, 0, 1, 1, colors, 1) == HAL_BUSY);
    assert(packets.empty());
    while (LCD_TransferTask() == HAL_BUSY) {}
    expectFlush(hal, buffer, 244, touchgfx::Rect(0, 0, 1, 1), touchgfx::Rect(0, 0, 1, 1));

    display.stride = 479; // Odd-byte and too-short strides are both invalid.
    reset();
    assert(hal.beginFrame());
    hal.flushFrameBuffer(touchgfx::Rect(0, 0, 1, 1));
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(packets.empty() && te_waits == 0);
    display.stride = 478;
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(packets.empty() && te_waits == 0);
    display.stride = 488;
    hal.setTestFramebuffers(NULL, front.data());
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(packets.empty() && te_waits == 0);

    hal.setTestFramebuffers(buffer.data(), front.data());
    scheduler_state = taskSCHEDULER_SUSPENDED;
    try { hal.endFrame(); assert(false); }
    catch (const DisplayFailure &) {}
    assert(packets.empty() && te_waits == 0);
    scheduler_state = taskSCHEDULER_RUNNING;
    std::puts("PASS: deferred frame batching, fresh TE/races/timeout, ISR handoff, RGB565 window/stride/clipping/endian/full-frame/errors");
}
