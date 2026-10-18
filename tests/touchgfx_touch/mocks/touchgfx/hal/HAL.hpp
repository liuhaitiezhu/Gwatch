#ifndef TOUCH_TEST_TGFX_HAL_HPP
#define TOUCH_TEST_TGFX_HAL_HPP
#include <stdint.h>
namespace touchgfx
{
// Only display dimensions are mocked; the real controller and driver are built.
class HAL
{
public:
    static uint16_t DISPLAY_WIDTH;
    static uint16_t DISPLAY_HEIGHT;
};
}
#endif
