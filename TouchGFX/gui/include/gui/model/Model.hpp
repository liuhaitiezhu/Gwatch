#ifndef MODEL_HPP
#define MODEL_HPP

#include <stdint.h>

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();
    int32_t getTemperatureCentiC() const { return temperatureCentiC; }
    bool hasValidTemperature() const { return temperatureValid; }
protected:
    ModelListener* modelListener;
    int32_t temperatureCentiC;
    bool temperatureValid;
};

#endif // MODEL_HPP
