#include <cassert>
#include <cstdio>
#include <cstring>
#include <STM32TouchController.hpp>
#include <touchgfx/hal/HAL.hpp>
#include "bsp_ft6336.h"
extern "C" {
#include "bsp_i2c_bus.h"
#include "i2c.h"
}

uint16_t touchgfx::HAL::DISPLAY_WIDTH = 240;
uint16_t touchgfx::HAL::DISPLAY_HEIGHT = 320;
I2C_HandleTypeDef hi2c1 = {};
static uint32_t tick;
static uint32_t primask;
static unsigned reads;
static unsigned locks;
static unsigned unlocks;
static bool locked;
static bool fail_read;
static uint8_t frame[5];
static uint8_t registers[256];

extern "C" uint32_t HAL_GetTick(void) { return tick; }
extern "C" void HAL_Delay(uint32_t ms) { tick += ms; }
extern "C" uint32_t __get_PRIMASK(void) { return primask; }
extern "C" void __disable_irq(void) { primask = 1; }
extern "C" void __set_PRIMASK(uint32_t value) { primask = value; }
extern "C" uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *) { return 1; }
extern "C" HAL_StatusTypeDef BSP_I2C1_Lock(uint32_t)
{
    assert(!locked);
    locked = true;
    ++locks;
    return HAL_OK;
}
extern "C" void BSP_I2C1_Unlock(void)
{
    assert(locked);
    locked = false;
    ++unlocks;
}
extern "C" HAL_StatusTypeDef HAL_I2C_Mem_Read(
    I2C_HandleTypeDef *handle, uint16_t address, uint16_t reg, uint16_t,
    uint8_t *data, uint16_t size, uint32_t)
{
    assert(handle == &hi2c1 && address == (0x38U << 1) && locked);
    ++reads;
    if (reg == 0x02U)
    {
        assert(size == sizeof(frame));
        if (fail_read)
        {
            fail_read = false;
            return HAL_TIMEOUT;
        }
        std::memcpy(data, frame, size);
    }
    else
    {
        assert(size == 1 && reg < sizeof(registers));
        *data = registers[reg];
    }
    return HAL_OK;
}
extern "C" HAL_StatusTypeDef HAL_I2C_Mem_Write(
    I2C_HandleTypeDef *handle, uint16_t address, uint16_t reg, uint16_t,
    uint8_t *data, uint16_t size, uint32_t)
{
    assert(handle == &hi2c1 && address == (0x38U << 1) && locked);
    assert(size == 1 && reg < sizeof(registers));
    registers[reg] = *data;
    return HAL_OK;
}

static void scan(uint8_t count, uint8_t event, uint8_t id,
                 uint16_t x = 0, uint16_t y = 0)
{
    frame[0] = count;
    frame[1] = static_cast<uint8_t>((event << 6) | (x >> 8));
    frame[2] = static_cast<uint8_t>(x);
    frame[3] = static_cast<uint8_t>((id << 4) | (y >> 8));
    frame[4] = static_cast<uint8_t>(y);
    tick += 21;
    FT6336_irq_fuc();
    const uint32_t previous_primask = primask;
    FT6336_scan_task();
    assert(primask == previous_primask);
    assert(!locked && locks == unlocks);
}

static void expect(STM32TouchController &controller, bool pressed,
                   int32_t expected_x = 0, int32_t expected_y = 0)
{
    int32_t x = -10;
    int32_t y = -20;
    const unsigned previous_reads = reads;
    assert(controller.sampleTouch(x, y) == pressed);
    assert(reads == previous_reads); // The adapter never polls I2C a second time.
    assert(x == (pressed ? expected_x : -10));
    assert(y == (pressed ? expected_y : -20));
}

int main()
{
    STM32TouchController controller;
    controller.init();
    assert(tick == 0 && reads == 0); // No early initialization of shared RESET.
    expect(controller, false);

    FT6336_init(); // The board performs this after initializing the LCD.
    expect(controller, false);
    assert(!locked && locks == unlocks);
    uint16_t x = 0, y = 0;
    assert(!FT6336_get_touch(NULL, &y));
    assert(!FT6336_get_touch(&x, NULL));

    scan(1, 0, 1, 10, 20);
    expect(controller, true, 10, 20);
    const unsigned previous_reads = reads;
    FT6336_scan_task(); // A skipped poll must retain the current pressed state.
    assert(reads == previous_reads);
    expect(controller, true, 10, 20);

    scan(1, 2, 1, 12, 24);
    expect(controller, true, 12, 24);
    scan(1, 2, 1, 90, 24);
    assert(FT6336_take_swipe() == FT6336_SWIPE_RIGHT);
    assert(FT6336_take_swipe() == FT6336_SWIPE_NONE);
    expect(controller, true, 90, 24); // A reported swipe is still a held finger.
    scan(1, 2, 1, 100, 24);
    expect(controller, true, 100, 24);
    scan(0, 0, 0);
    expect(controller, false);

    scan(1, 0, 1, 0, 0);
    expect(controller, true, 0, 0);
    scan(1, 2, 1, 239, 319);
    expect(controller, true, 239, 319);
    scan(1, 2, 1, 240, 319);
    assert(FT6336_get_touch(&x, &y));
    expect(controller, false);
    scan(1, 2, 1, 239, 320);
    expect(controller, false);
    scan(1, 1, 1, 239, 319); // Explicit UP even if count is still one.
    expect(controller, false);

    scan(1, 0, 1, 50, 60);
    expect(controller, true, 50, 60);
    scan(2, 2, 1, 50, 60);
    expect(controller, false);
    scan(1, 2, 1, 51, 61); // Ignore the rest of a multi-touch sequence.
    expect(controller, false);
    scan(0, 0, 0);
    scan(1, 0, 1, 50, 60);
    expect(controller, true, 50, 60);
    scan(1, 2, 2, 50, 60); // Unexpected touch ID.
    expect(controller, false);
    scan(0, 0, 0);
    scan(1, 0, 15, 50, 60);
    expect(controller, false);
    scan(0, 0, 0);
    scan(1, 3, 1, 50, 60); // Reserved event.
    expect(controller, false);
    scan(0, 0, 0);

    scan(1, 0, 1, 50, 60);
    fail_read = true;
    scan(1, 2, 1, 51, 61);
    expect(controller, false); // No stale press on a bus timeout.
    scan(0, 0, 0);
    scan(1, 0, 1, 50, 60);
    expect(controller, true, 50, 60);
    primask = 1;
    scan(1, 2, 1, 51, 61);
    assert(primask == 1);
    primask = 0;
    FT6336_init();
    expect(controller, false); // Reinitialization clears a held sample.
    assert(!locked && locks == unlocks);
    std::puts("PASS: TouchGFX touch coordinates/hold/release/errors/bounds/swipe/cache/C linkage");
}
