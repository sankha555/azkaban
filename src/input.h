#ifndef __INPUT_H__
#define __INPUT_H__

#include "layer.h"

template <typename T>
class Input : public Layer<T> {
    public:

    set<int> sensitive_attrs;

    Input(size_t input_size){
        this->input_size = this->output_size = input_size;  
        this->type = LAYER_TYPES::INPUT; 
    }

    void set_input(vector<float> x, float eps){

        Interval<T> eps_itvl = Interval<T>::intervalize_from_real(eps);        

        Interval<T> zero_itvl = Interval<T>();

        Interval<T> x_itvl;

        for(int i = 0; i < this->input_size; i++){
           
            x_itvl = Interval<T>::intervalize_from_real(x[i]);
            
            Zonotope<T>* node;
            if(this->sensitive_attrs.count(i)){
                node = new Zonotope<T>(x_itvl, {GLOBAL_NOISE_SYMBOL_CTR++}, {zero_itvl});
            } else {
                node = new Zonotope<T>(x_itvl, {GLOBAL_NOISE_SYMBOL_CTR++}, {eps_itvl});
            } 
           
            this->expressions.push_back(node);
        }
    }

    void set_sensitive_attrs(set<int> sensitive_attrs){
        this->sensitive_attrs = sensitive_attrs;
    }

    void forward(Layer<T>* prev_layer){
        ;
    }
};

#endif 