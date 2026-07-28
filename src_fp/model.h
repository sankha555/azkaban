#ifndef __FP_MODEL_H__
#define __FP_MODEL_H__

#include "src_fp/input.h"
#include "src_fp/affine.h"
#include "src_fp/conv2d.h"
#include "src_fp/relu.h"
#include "src_fp/output.h"
#include "src/utils.h"

template <typename T>
class Model {
    public:

    vector<Layer<T>*> layers;

    Model(){
        this->layers.clear();
    }

    void add_layer(Layer<T>* layer){
        this->layers.push_back(layer);
    }

    void read_params(const char* PARAMS_FILE_PATH){
        size_t layer_offset = 0;
        for(Layer<T>* layer : this->layers){
            if(layer->type == LAYER_TYPES::AFFINE){
                layer_offset += ((Affine<T>*) layer)->set_parameters(PARAMS_FILE_PATH, layer_offset);
            } else if(layer->type == LAYER_TYPES::CONV2D){
                layer_offset += ((Conv2D<T>*) layer)->set_parameters(PARAMS_FILE_PATH, layer_offset);
            }
        }
    }

    void forward(vector<float> x, float eps, set<int> sensitive_attrs = {}){
        ((Input<T>*) this->layers[0])->set_sensitive_attrs(sensitive_attrs);
        ((Input<T>*) this->layers[0])->set_input(x, eps);

        Layer<T>* prev_layer = this->layers[0];
        for(int i = 1; i < this->layers.size(); i++){
            this->layers[i]->forward(prev_layer);
            prev_layer = this->layers[i];
        }
    }

    void reset(){
        for(int i = 0; i < this->layers.size(); i++){
            for(Zonotope<T>* zono : this->layers[i]->expressions){
                delete zono;
            }
            this->layers[i]->expressions.clear();
        }
        GLOBAL_NOISE_SYMBOL_CTR = 0;
    }

    ~Model(){
        for(Layer<T>* layer : this->layers){
            delete layer;
        }
        this->layers.clear();
    }
};

#endif
