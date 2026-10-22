#include <gui/screen_screen/screenView.hpp>

screenView::screenView()
{

}

void screenView::setupScreen()
{
    screenViewBase::setupScreen();
    // ScrollableContainer records the drag origin from the touched child.
    image1.setTouchable(true);
    // The fixed temperature overlay must not intercept image drags.
    textArea1.setTouchable(false);
    updateTemperature(0, false);
}

void screenView::tearDownScreen()
{
    screenViewBase::tearDownScreen();
}

void screenView::handleGestureEvent(const touchgfx::GestureEvent& event)
{
    // Drag events still move the image. Suppress the vertical flick animation
    // so releasing the finger stops at the current position.
    if (event.getType() != touchgfx::GestureEvent::SWIPE_VERTICAL)
    {
        screenViewBase::handleGestureEvent(event);
    }
}

void screenView::updateTemperature(int32_t temperatureCentiC, bool valid)
{
    if (valid)
    {
        // Keep the sign separate so -0.01 is displayed correctly; no float formatter.
        const uint32_t magnitude = temperatureCentiC < 0
                                       ? 0U - static_cast<uint32_t>(temperatureCentiC)
                                       : static_cast<uint32_t>(temperatureCentiC);
        touchgfx::Unicode::snprintf(textArea1Buffer, TEXTAREA1_SIZE,//在这里给wildcardbuffer进行写入
            temperatureCentiC < 0 ? "-%u.%02u" : "%u.%02u",
            static_cast<unsigned int>(magnitude / 100U),
            static_cast<unsigned int>(magnitude % 100U));
    }
    else
    {
        touchgfx::Unicode::snprintf(textArea1Buffer, TEXTAREA1_SIZE, "--.--");
    }
    // The Designer's fixed rectangle also clears digits left by a longer old value.
    textArea1.invalidate();
}
