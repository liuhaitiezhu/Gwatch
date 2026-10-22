#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>

class ModelListener
{
public://在类内部定义的函数默认就是inline的，也就是内联的，下面的函数没有内容，没有对应的.cpp文件也是可以的
    ModelListener() : model(0) {}
    
    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }
    virtual void temperatureUpdated(int32_t, bool) {}
protected:
    Model* model;
};

#endif // MODELLISTENER_HPP
