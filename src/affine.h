#ifndef __AFFINE_H__
#define __AFFINE_H__

#include "layer.h"
#include "params.h"

template <typename T>
class Affine : public Layer<T> {
    public:

    Parameters<T>* params;

    Affine(size_t num_inputs, size_t num_neurons){
        this->input_size = num_inputs;
        this->output_size = num_neurons;
        this->type = LAYER_TYPES::AFFINE;
    }

    size_t set_parameters(const char* PARAMS_FILE_PATH, size_t layer_offset){
        size_t num_params = (this->input_size + 1) * this->output_size;

        std::vector<float> read_buffer(num_params);
        if(party == ALICE){
            cout << "reading params\n";
            read_next_elements(num_params, read_buffer.data(), layer_offset, PARAMS_FILE_PATH);
        }

        vector<Interval<T>> params_itvl(num_params);
        for(int i = 0; i < num_params; i++){
            params_itvl[i] = Interval<T>::intervalize_from_real(read_buffer[i]);
            // cout << params_itvl[i].to_string() << "\n";
        }

        this->params = new Parameters<T>(this->output_size, this->input_size, params_itvl.data());

        return num_params;
    }

    // void forward(Layer<T>* prev_layer){

    //     Interval<T> w, b;
    //     size_t m = this->input_size;

    //     Interval<T> new_centre;
    //     Zonotope<T>* input_zono;

    //     cerr << prev_layer->expressions.size() << "\n";
        

    //     for(size_t i = 0; i < this->output_size; i++){
    //         b = this->params->params_matrix[i][m];

    //         new_centre = b;
    //         map<size_t, Interval<T>> new_noise_symbols;

    //         for(size_t j = 0; j < this->input_size; j++){
    //             w = this->params->params_matrix[i][j];

    //             // get the input zonotope
    //             input_zono = prev_layer->expressions[j];
    //             // if(input_zono->is_zero()){
    //             //     continue;
    //             // }

    //             // centre computation
    //             Interval<T> add = w * input_zono->center;    
    //             // cout << input_zono->center.to_string() << " " << w.to_string() << " " << add.to_string() << "\n";

    //             new_centre = new_centre + add;
                

    //             // noise symbols computation as linear recombination of previous symbols
    //             for (const auto& [index, coeff] : input_zono->noise_symbols) {
    //                 new_noise_symbols[index] = new_noise_symbols[index] + w * coeff;            
    //             }
    //         }

    //         Zonotope<T>* new_zono = new Zonotope<T>(new_centre, new_noise_symbols);
    //         this->expressions.push_back(new_zono);
    //     }
    //     // exit(0);
    // }

    void forward(Layer<T>* prev_layer) {
        size_t m = this->input_size;

        for(size_t i = 0; i < this->output_size; i++){

            Zonotope<T>* new_zono;

            Interval<T> b = this->params->params_matrix[i][m];

            Interval<T> new_center = b;

            map<size_t, Interval<T>> new_noise_symbols;

            vector<Interval<T>> A, B;
            for(size_t j = 0; j < m; j++){
                // \sum_{j = 1}^{m}{w_ij * c_j}
                A.push_back(this->params->params_matrix[i][j]);
                B.push_back(prev_layer->expressions[j]->center);
                // cerr << prev_layer->expressions[j]->concrete().to_string() << "\n";
            }

            // exit(0);

            Interval<T> ip = Interval<T>::inner_product(m, A, B);
            new_center += ip;

            for(size_t k = 0; k < GLOBAL_NOISE_SYMBOL_CTR; k++){
                A.clear();
                B.clear();

                for(size_t j = 0; j < m; j++){
                    if(prev_layer->expressions[j]->noise_symbols.count(k)){
                        B.push_back(prev_layer->expressions[j]->noise_symbols[k]);
                        A.push_back(this->params->params_matrix[i][j]);
                    }
                }

                Interval<T> new_coeff = Interval<T>::inner_product(A.size(), A, B);

                new_noise_symbols[k] = new_coeff;
            }

            new_zono = new Zonotope<T>(new_center, new_noise_symbols);

            this->expressions.push_back(new_zono);
        }
    
    }
};

#endif