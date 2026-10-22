#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

#ifndef SIMULATOR
#include "app_freertos.h"
#endif

Model::Model() : modelListener(0), temperatureCentiC(0), temperatureValid(false)//初始化应为上电默认值，不能是ture
{

}

void Model::tick()
{
#ifndef SIMULATOR
    App_TemperatureReading reading;
    if (App_Temperature_TryRead(&reading))
    {
        const bool valid = reading.valid;
        if (valid != temperatureValid || (valid && reading.temperature_centi_c != temperatureCentiC))
        {
            temperatureCentiC = valid ? reading.temperature_centi_c : 0;//三目运算符，valid为true时，temperatureCentiC为reading.temperature_centi_c，否则为0
            temperatureValid = valid;
            if (modelListener)
            {
                modelListener->temperatureUpdated(temperatureCentiC, temperatureValid);
            }
        }
    }
#endif
}
