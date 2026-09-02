#ifndef __PARAMS_H__
#define __PARAMS_H__

#include "commons.h"
#include "interval/interval.h"

using namespace std;

template <typename T>
class Parameters {
    public:

    size_t n;                   // #rows = #variables in this layer
    size_t m;                   // #cols = #features 

    vector<vector<Interval<T>>> params_matrix;       // n * (m + 1)

    Parameters(size_t n, size_t m, Interval<T>* params){

        this->n = n;
        this->m = m;

        for(int i = 0; i < n; i++){
            vector<Interval<T>> param_row;

            // first input weights
            for(int j = 0; j < m; j++){
                param_row.push_back(params[i*m + j]);
            }

            // then input biases
            param_row.push_back(params[n * m + i]);

            this->params_matrix.push_back(param_row);
        }
    }

};


template <typename T>
class Kernel2D {
    public:

    size_t out_c;
    size_t in_c;
    size_t h;
    size_t w;


    size_t params_per_out_channel;
    size_t params_per_in_channel;


    Interval<T>* filter_matrix;

    Kernel2D(size_t out_c, size_t in_c, size_t h, size_t w, Interval<T>* params){
        this->out_c = out_c;
        this->in_c = in_c;
        this->h = h;
        this->w = w;
        this->filter_matrix = new Interval<T>[out_c * in_c * h * w + out_c];

        this->params_per_out_channel = this->in_c * this->h * this->w;
        this->params_per_in_channel = this->h * this->w;

        int t = 0;
        for(size_t q = 0; q < this->out_c; q++){
            // for each out channel

            for(size_t p = 0; p < this->in_c; p++){
                // for each in channel

                for(size_t i = 0; i < this->h; i++){
                    for(size_t j = 0; j < this->w; j++){
                        this->filter_matrix[
                            q * params_per_out_channel +
                            p * params_per_in_channel +
                            i * this->w +
                            j
                        ] = params[
                            q * params_per_out_channel +
                            p * params_per_in_channel +
                            i * this->w +
                            j
                        ];
                    }
                }
            }
        }

        for(size_t q = 0; q < this->out_c; q++){
            this->filter_matrix[this->out_c * this->params_per_out_channel + q] = params[this->out_c * this->params_per_out_channel + q];
        }
    }

    int num_parameters(){
        return out_c * in_c * h * w + out_c;
    }

    int num_weights(){
        return out_c * in_c * h * w;
    }


    Interval<T>* get_flattened_weights(int q){
        return this->filter_matrix + q * (this->params_per_out_channel);
    }

    void print_parameters(){
        ;
    }
};

#endif