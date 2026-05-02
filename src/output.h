#ifndef __OUTPU_H__
#define __OUTPU_H__

#include "layer.h"

template <typename T>
class Output : public Layer<T> {
    public:

    int gt;
    bool verified;

    Output(size_t input_size) {
        this->input_size = this->output_size = input_size;
        this->gt = gt;
        this->type = LAYER_TYPES::OUTPUT;
    }

    void set_output(int gt){
        this->gt = gt;
    }

    void forward(Layer<T>* prev_layer){
        vector<T> lbs, ubs;

        cout << setprecision(6) << "\n-- OUTPUT --\n";
        for(size_t i = 0; i < this->input_size; i++){
            Interval<T> bounds = prev_layer->expressions[i]->concrete();

            if constexpr (TYPE_EQ(T, float)){
                cout << "[" << bounds.inf << ", " << bounds.sup << "]\n";
            } else if constexpr (TYPE_EQ(T, int64_t)){
                cout << "[" << (bounds.inf * 1.0) / (1 << FXPSCALE) << ", " << (bounds.sup * 1.0) / (1 << FXPSCALE) << "]\n";
            } else if constexpr (TYPE_EQ(T, IntFp)){
                cout << "[" << CLT(bounds.inf) << ", " << CLT(bounds.sup) << "]\n"; 
            }

            lbs.push_back(bounds.inf);
            ubs.push_back(bounds.sup);
        }
        cout << setprecision(6);

        
        this->compute_differences(prev_layer);
        if(this->verified){
            NUM_VERIFIED++;
            cout << "YES\n";
        } else {
            cout << "NO\n";
        }

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::cout << "Finish time: " << std::ctime(&now_c);
    }

    void compute_differences(Layer<T>* prev_layer){
        int n = prev_layer->expressions.size();

        this->verified = true;
        Zonotope<T>* gt_zono = prev_layer->expressions[this->gt];

        vector<Interval<T>> bounds;
        for(int i = 0; i < n; i++){
            if(i == this->gt){
                continue;
            }

            Zonotope<T>* curr_zono = prev_layer->expressions[i];

            Interval<T> new_center = gt_zono->center - curr_zono->center;

            // z = gt_zono - curr_zono 

            map<size_t, Interval<T>> new_noise_symbols;
            for (const auto& [index, coeff] : gt_zono->noise_symbols) {
                new_noise_symbols[index] = coeff;
            }

            for (const auto& [index, coeff] : curr_zono->noise_symbols) {
                new_noise_symbols[index] = new_noise_symbols[index] - curr_zono->noise_symbols[index];
            }

            Zonotope<T>* new_zono = new Zonotope<T>(new_center, new_noise_symbols);
            Interval<T> bound =  new_zono->concrete();

            if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)){
                if(bound.inf <= 0){
                    this->verified = false;
                }
            } else {

                IntFp cmp(bound.inf);
                ZKcmpPositive(party, &cmp, ZERO_COMP_CONSTANT, &cmp, 1);

                // cerr << cmp.reveal() << "\n";

                if (!cmp.reveal()) {
                    this->verified = false;
                }
            }
        }
    }

    bool verify(vector<T> lbs, vector<T> ubs){
        T gtl = lbs[this->gt], gtu = ubs[this->gt];

        if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)){
            // check for incorrect prediction
            for(int i = 0; i < lbs.size(); i++){
                if(i == this->gt){
                    continue;
                }
                
                if(lbs[i] > lbs[this->gt]){
                    return false;
                }
            }

            // check for intersecting bounds
            for(size_t i = 0; i < lbs.size(); i++){
                if(i == this->gt){
                    continue;
                }

                if(ubs[i] >= gtl){
                    return false;
                }
            }

            return true;
        }

        if constexpr (TYPE_EQ(T, IntFp)) {

            IntFp* cmp = new IntFp[lbs.size()-1];
            int t = 0;
            for(int i = 0; i < lbs.size(); i++){
                if(i == this->gt){
                    continue;
                }

                cmp[t] = ubs[i] + lbs[this->gt].negate();
                t++;
            }
            ZKcmpPositive(party, cmp, ZERO_COMP_CONSTANT, cmp, t);

            t = 0;
            for(int i = 0; i < lbs.size(); i++){
                if(i == this->gt){
                    continue;
                }

                if(cmp[t++].reveal()){
                    return false;
                }
            }

            return true;
        }
        
        return true;
    }
};

#endif
