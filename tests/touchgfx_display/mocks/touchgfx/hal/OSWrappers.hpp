#ifndef DISPLAY_TEST_OSWRAPPERS_HPP
#define DISPLAY_TEST_OSWRAPPERS_HPP
void testSignalVSync();
namespace touchgfx
{
class OSWrappers
{
public:
    static void signalVSync() { testSignalVSync(); }
};
}
#endif
