#ifndef __FP_RELU_H__
#define __FP_RELU_H__

#include "src/cleartext/layer.h"

template <typename T>
class ReLU : public Layer<T> {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

    public:

    ReLU(size_t input_size) {
        this->input_size = this->output_size = input_size;
        this->type = LAYER_TYPES::RELU;
        this->expressions.clear();
    }

    // Zonotope ReLU transformer, fixed-point: exact for a stable neuron,
    // otherwise the standard lambda/mu relaxation with a fresh noise symbol.
    // Every field-relevant operation is checked for overflow inside Interval<Fp>.
    void forward(Layer<T>* prev_layer){

        Zonotope<T>* new_zono;
        Zonotope<T>* input_zono;

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
};

#endif
