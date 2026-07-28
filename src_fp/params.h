#ifndef __FP_PARAMS_H__
#define __FP_PARAMS_H__

#include "src/commons.h"
#include "src_fp/interval.h"

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

#endif
