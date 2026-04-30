#ifndef __LAYER_H__
#define __LAYER_H__

#include "utils.h"
#include "zonotope.h"

template <typename T>
class Layer {
    public:
    int type;

    size_t input_size;
    size_t output_size;  

    vector<Zonotope<T>*> expressions;

    virtual void forward(Layer<T>* prev_layer) = 0;
};

#endif