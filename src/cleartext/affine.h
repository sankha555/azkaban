#ifndef __FP_AFFINE_H__
#define __FP_AFFINE_H__

#include "src/cleartext/layer.h"
#include "src/cleartext/params.h"

template <typename T>
class Affine : public Layer<T> {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

    public:

    Parameters<T>* params = nullptr;

    Affine(size_t num_inputs, size_t num_neurons){
        this->input_size = num_inputs;
        this->output_size = num_neurons;
        this->type = LAYER_TYPES::AFFINE;
    }

    ~Affine(){
        delete this->params;
    }

    size_t set_parameters(const char* PARAMS_FILE_PATH, size_t layer_offset){
        size_t num_params = (this->input_size + 1) * this->output_size;

        std::vector<float> read_buffer(num_params);
        if(party == ALICE){
            read_next_elements(num_params, read_buffer.data(), layer_offset, PARAMS_FILE_PATH);
        }

        vector<Interval<T>> params_itvl(num_params);
        for(int i = 0; i < num_params; i++){
            params_itvl[i] = Interval<T>::intervalize_from_real(read_buffer[i]);
        }

        this->params = new Parameters<T>(this->output_size, this->input_size, params_itvl.data());

        return num_params;
    }

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
            }

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

            // cout << new_zono->concrete().to_string() << "\n";
        }
        // cout << "\n\n";
    }
};

#endif
