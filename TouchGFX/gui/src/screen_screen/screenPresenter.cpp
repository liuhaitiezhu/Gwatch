#include <gui/screen_screen/screenView.hpp>
#include <gui/screen_screen/screenPresenter.hpp>

screenPresenter::screenPresenter(screenView& v)
    : view(v)
{

}

void screenPresenter::activate()//每次切换到这个screen就执行一次activate里面的函数
{
    view.updateTemperature(model->getTemperatureCentiC(), model->hasValidTemperature());
}

void screenPresenter::deactivate()//这个screen结束的时候调用
{

}

void screenPresenter::temperatureUpdated(int32_t temperatureCentiC, bool valid)
{
    view.updateTemperature(temperatureCentiC, valid);
}
