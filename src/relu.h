#ifndef __RELU_H__
#define __RELU_H__

#include "layer.h"
 
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

                Zonotope<T>* new_zono;
                Interval<float> new_center;
                map<size_t, Interval<float>> new_noise_symbols;

                assert(input_zono->noise_symbols.size() == GLOBAL_NOISE_SYMBOL_CTR-i);

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

                Zonotope<T>* new_zono;
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

        if constexpr (TYPE_EQ(T, IntFp)){
            vector<Interval<T>> bounds;
            for(size_t i = 0; i < this->input_size; i++){
                Interval<T> bound = prev_layer->expressions[i]->concrete();
                bounds.push_back(bound);
            }

            vector<IntFp> cmp;
            for(size_t i = 0; i < this->input_size; i++){
                cmp.push_back(bounds[i].inf);
                cmp.push_back(bounds[i].sup);
            }
            ZKcmpPositive(party, cmp.data(), ZERO_COMP_CONSTANT, cmp.data(), cmp.size());

            vector<IntFp> straddles;
            for(int i = 0; i < this->input_size; i++){
                straddles.push_back(NOT(cmp[i*2]) * cmp[i*2+1]);
            }

            vector<Interval<IntFp>> lambdas, mus;
            for(size_t i = 0; i < this->input_size; i++){
                Interval<T> bound = bounds[i];
                Interval<T> l_itvl = Interval<T>(bound.inf);
                Interval<T> u_itvl = Interval<T>(bound.sup);

                Interval<T> diff = u_itvl - l_itvl;
                Interval<T> prod = u_itvl * l_itvl;
                prod = !prod;

                Interval<T> lambda = u_itvl / diff;

                diff = diff + diff;             // 2 * diff
                Interval<T> mu = prod / diff;

                lambdas.push_back(lambda);
                mus.push_back(mu);
            }

            for(size_t i = 0; i < this->input_size; i++){
                input_zono = prev_layer->expressions[i];

                Interval<IntFp> new_center = (input_zono->center & cmp[i*2]) +
                                            ((lambdas[i] * input_zono->center + mus[i]) & straddles[i]) +
                                            Interval<IntFp>(FIELD_ZERO, FIELD_ZERO);


                map<size_t, Interval<IntFp>> new_noise_symbols;
                for(const auto& [index, coeff] : input_zono->noise_symbols){
                    new_noise_symbols[index] = (((Interval<IntFp>) coeff) & cmp[i*2]) +
                                               ((lambdas[i] * coeff) & straddles[i]) + 
                                               Interval<IntFp>(FIELD_ZERO, FIELD_ZERO);
                }
                new_noise_symbols[GLOBAL_NOISE_SYMBOL_CTR++] =  (mus[i] & straddles[i]) + 
                                                                Interval<IntFp>(FIELD_ZERO, FIELD_ZERO);


                new_zono = new Zonotope<T>(new_center, new_noise_symbols);
                this->expressions.push_back(new_zono);
            }
        }
    }
};

#endif 
