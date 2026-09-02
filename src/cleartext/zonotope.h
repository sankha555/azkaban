#ifndef __FP_ZONOTOPE_H__
#define __FP_ZONOTOPE_H__

#include "src/commons.h"
#include "src/cleartext/interval.h"

using namespace std;

template <typename T>
class Zonotope {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

    public:

    Interval<T> center;
    map<size_t, Interval<T>> noise_symbols;       // coeff index, coeff

    Zonotope(){
        this->center = Interval<T>();
        this->noise_symbols[-1] = Interval<T>();
    }

    Zonotope(Interval<T> center, vector<size_t> symbol_indexes, vector<Interval<T>> noise_coeffs){
        this->center = center;
        for(int i = 0; i < noise_coeffs.size(); i++){
            this->noise_symbols[symbol_indexes[i]] = noise_coeffs[i];
        }
    }

    Zonotope(Interval<T> center, map<size_t, Interval<T>> noise_symbols){
        this->center = center;
        this->noise_symbols = noise_symbols;
    }

    size_t num_noise_symbols(){
        return (size_t) this->noise_symbols.size();
    }

    // `mode` is kept for signature compatibility with the ZK backend, where a
    // negative mode skips the (expensive) magnitude gadget; here the magnitude
    // is always computed.
    Interval<T> concrete(int mode = 0) {
        Interval<T> bounds = this->center;

        T mag;

        for (auto& [index, coeff] : this->noise_symbols) {
            mag = max({abs(coeff.inf), abs(coeff.sup)});

            bounds.inf = bounds.inf - mag;
            bounds.sup = bounds.sup + mag;
        }

        return bounds;
    }

    bool is_zero(){
        return (this->noise_symbols.count(-1) != 0);
    }
};


#endif
