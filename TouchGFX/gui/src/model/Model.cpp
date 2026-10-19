#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

#ifndef SIMULATOR
#include "app_freertos.h"
#endif

Model::Model() : modelListener(0), temperatureCentiC(0), temperatureValid(false)
{

}

void Model::tick()
{
#ifndef SIMULATOR
    App_TemperatureReading reading;
    if (App_Temperature_TryRead(&reading))
    {
        const bool valid = reading.valid != 0U;
        if (valid != temperatureValid || (valid && reading.temperature_centi_c != temperatureCentiC))
        {
            temperatureCentiC = valid ? reading.temperature_centi_c : 0;
            temperatureValid = valid;
            if (modelListener)
            {
                modelListener->temperatureUpdated(temperatureCentiC, temperatureValid);
            }
        }
    }
#endif
}
