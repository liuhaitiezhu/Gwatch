#include <gui/screen_screen/screenView.hpp>
#include <gui/screen_screen/screenPresenter.hpp>

screenPresenter::screenPresenter(screenView& v)
    : view(v)
{

}

void screenPresenter::activate()
{
    view.updateTemperature(model->getTemperatureCentiC(), model->hasValidTemperature());
}

void screenPresenter::deactivate()
{

}

void screenPresenter::temperatureUpdated(int32_t temperatureCentiC, bool valid)
{
    view.updateTemperature(temperatureCentiC, valid);
}
