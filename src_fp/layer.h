#ifndef __FP_LAYER_H__
#define __FP_LAYER_H__

#include "src/utils.h"
#include "src_fp/zonotope.h"

template <typename T>
class Layer {
    public:
    int type;

    size_t input_size;
    size_t output_size;

    vector<Zonotope<T>*> expressions;

    virtual void forward(Layer<T>* prev_layer) = 0;

    virtual ~Layer() {
        for(Zonotope<T>* zono : this->expressions){
            delete zono;
        }
        this->expressions.clear();
    }
};

#endif
