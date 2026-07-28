#ifndef __FP_RELU_H__
#define __FP_RELU_H__

#include "src_fp/layer.h"

template <typename T>
class ReLU : public Layer<T> {
    public:

    ReLU(size_t input_size) {
        this->input_size = this->output_size = input_size;
        this->type = LAYER_TYPES::RELU;
        this->expressions.clear();
    }

    void forward(Layer<T>* prev_layer){

        Zonotope<T>* new_zono;
        Zonotope<T>* input_zono;

        if constexpr (TYPE_EQ(T, float)) {
            for(size_t i = 0; i < this->output_size; i++){
                input_zono = prev_layer->expressions[i];
                Interval<T> bounds = input_zono->concrete();

                Interval<float> new_center;
                map<size_t, Interval<float>> new_noise_symbols;

                assert(input_zono->noise_symbols.size() <= GLOBAL_NOISE_SYMBOL_CTR-i);

                if (bounds.inf >= 0){

                    new_center = input_zono->center;
                    new_noise_symbols = input_zono->noise_symbols;
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<float>(0);

                } else if(bounds.sup <= 0){

                    new_center = Interval<float>(0);
                    new_noise_symbols = input_zono->noise_symbols;
                    for(const auto& [index, coeff] : new_noise_symbols){
                        new_noise_symbols[index] = Interval<float>(0);
                    }
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<float>(0);

                } else {

                    Interval<float> l_itvl = Interval<float>(bounds.inf);
                    Interval<float> u_itvl = Interval<float>(bounds.sup);

                    Interval<float> lambda = u_itvl / (u_itvl - l_itvl);
                    Interval<float> mu = (Interval<float>(-1) * u_itvl * l_itvl) / (Interval<float>(2.0) * (u_itvl - l_itvl));

                    new_center = lambda * input_zono->center + mu;

                    for(const auto& [index, coeff] : input_zono->noise_symbols){
                        new_noise_symbols[index] = lambda * coeff;
                    }

                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = mu;

                }

                new_zono = new Zonotope<float>(new_center, new_noise_symbols);
                this->expressions.push_back(new_zono);
            }
        }

        if constexpr (TYPE_EQ(T, int64_t)) {

            for(size_t i = 0; i < this->output_size; i++){

                input_zono = prev_layer->expressions[i];
                Interval<T> bounds = input_zono->concrete();

                Interval<int64_t> new_center;
                map<size_t, Interval<int64_t>> new_noise_symbols;

                if (bounds.inf >= 0){
                    new_center = input_zono->center;
                    new_noise_symbols = input_zono->noise_symbols;
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<int64_t>(0);

                } else if(bounds.sup <= 0){
                    new_center = Interval<int64_t>(0);
                    new_noise_symbols = input_zono->noise_symbols;
                    for(const auto& [index, coeff] : new_noise_symbols){
                        new_noise_symbols[index] = Interval<int64_t>(0);
                    }
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<int64_t>(0);

                } else {

                    Interval<int64_t> l_itvl = Interval<int64_t>(bounds.inf);
                    Interval<int64_t> u_itvl = Interval<int64_t>(bounds.sup);

                    Interval<int64_t> lambda = u_itvl / (u_itvl - l_itvl);
                    Interval<int64_t> mu = (Interval<int64_t>((-1) * (1LL << FXPSCALE)) * u_itvl * l_itvl) / (Interval<int64_t>((2) * (1LL << FXPSCALE)) * (u_itvl - l_itvl));

                    new_center = lambda * input_zono->center + mu;

                    for(const auto& [index, coeff] : input_zono->noise_symbols){
                        new_noise_symbols[index] = lambda * coeff;
                    }

                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = mu;

                }

                new_zono = new Zonotope<int64_t>(new_center, new_noise_symbols);
                this->expressions.push_back(new_zono);
            }
        }

        // ---- Fp path: identical fixed-point logic to the int64_t reference,
        // but every field-relevant operation is checked for overflow. ----
        if constexpr (TYPE_EQ(T, Fp)) {

            for(size_t i = 0; i < this->output_size; i++){

                input_zono = prev_layer->expressions[i];
                Interval<T> bounds = input_zono->concrete();

                Interval<Fp> new_center;
                map<size_t, Interval<Fp>> new_noise_symbols;

                if (bounds.inf >= 0){
                    new_center = input_zono->center;
                    new_noise_symbols = input_zono->noise_symbols;
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<Fp>(0);

                } else if(bounds.sup <= 0){
                    new_center = Interval<Fp>(0);
                    new_noise_symbols = input_zono->noise_symbols;
                    for(const auto& [index, coeff] : new_noise_symbols){
                        new_noise_symbols[index] = Interval<Fp>(0);
                    }
                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = Interval<Fp>(0);

                } else {

                    Interval<Fp> l_itvl = Interval<Fp>(bounds.inf);
                    Interval<Fp> u_itvl = Interval<Fp>(bounds.sup);

                    Interval<Fp> lambda = u_itvl / (u_itvl - l_itvl);
                    Interval<Fp> mu = (Interval<Fp>((-1) * (1LL << FXPSCALE)) * u_itvl * l_itvl) / (Interval<Fp>((2) * (1LL << FXPSCALE)) * (u_itvl - l_itvl));

                    new_center = lambda * input_zono->center + mu;

                    for(const auto& [index, coeff] : input_zono->noise_symbols){
                        new_noise_symbols[index] = lambda * coeff;
                    }

                    new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] = mu;

                }

                new_zono = new Zonotope<Fp>(new_center, new_noise_symbols);
                this->expressions.push_back(new_zono);
            }
        }
    }
};

#endif
