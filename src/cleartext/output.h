#ifndef __FP_OUTPUT_H__
#define __FP_OUTPUT_H__

#include "src/cleartext/layer.h"

template <typename T>
class Output : public Layer<T> {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

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

        for(size_t i = 0; i < this->input_size; i++){
            Interval<T> bounds = prev_layer->expressions[i]->concrete();

            if (DEBUG)
                cout << "[" << bounds.inf.to_real() << ", " << bounds.sup.to_real() << "]\n";

            lbs.push_back(bounds.inf);
            ubs.push_back(bounds.sup);
        }
        cout << setprecision(6);

        this->compute_differences(prev_layer);
        if(this->verified){
            NUM_VERIFIED++;
            if (DEBUG) cout << "YES\n";
        } else {
            if (DEBUG) cout << "NO\n";
        }

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
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

            Zonotope<T> new_zono(new_center, new_noise_symbols);
            Interval<T> bound =  new_zono.concrete();

            // certified iff the gt logit stays strictly above every other one
            // over the whole zonotope.
            if(bound.inf <= 0){
                this->verified = false;
            }
        }
    }

    bool verify(vector<T> lbs, vector<T> ubs){
        T gtl = lbs[this->gt], gtu = ubs[this->gt];

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
};

#endif
