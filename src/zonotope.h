#ifndef __ZONOTOPE_H__
#define __ZONOTOPE_H__

#include "commons.h"
#include "interval/interval.h"

using namespace std;

template <typename T = float>
class Zonotope {
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

    Interval<T> concrete(int mode = 0) {
        Interval<T> bounds = this->center;

        T mag;

        for (auto& [index, coeff] : this->noise_symbols) {
            
            if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)){
                mag = max({abs(coeff.inf), abs(coeff.sup)});
                
                bounds.inf = bounds.inf - mag;
                bounds.sup = bounds.sup + mag;
            }

            if constexpr (TYPE_EQ(T, IntFp)){
                if(mode >= 0){  
                    IntFp cmp[3];
                    cmp[0] = coeff.inf;
                    cmp[1] = coeff.sup;
                    cmp[2] = coeff.inf + coeff.sup;

                    ZKcmpPositive(party, cmp, ZERO_COMP_CONSTANT, cmp, 3);
                    
                    mag = cmp[0] * coeff.sup +
                    NOT(cmp[1]) * coeff.inf.negate() +
                    NOT(cmp[0]) * (cmp[1]) * (cmp[2] * (coeff.inf + coeff.sup) + coeff.inf.negate());
                    
                    bounds.inf = bounds.inf + mag.negate();
                    bounds.sup = bounds.sup + mag;
                } 
            }

        }
        
        return bounds;
    }

    bool is_zero(){
        return (this->noise_symbols.count(-1) != 0);
    }
};


#endif
