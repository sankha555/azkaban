#ifndef __FLOAT_INTERVAL_H__
#define __FLOAT_INTERVAL_H__


#include "src/interval/interval.h"

#include <boost/multiprecision/cpp_dec_float.hpp>

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cfenv> 
#include <string>
#include <algorithm>

#pragma STDC FENV_ACCESS ON

class FloatInterval : public Interval<float> {
public:
    float lower;
    float upper;

    using Interval<float>::Interval;

    // Constructor: Brackets a real number into the nearest representable floats
    explicit FloatInterval(float val) {
        int prev_mode = fegetround();
        
        fesetround(FE_DOWNWARD);
        this->lower = val; 
        
        fesetround(FE_UPWARD);
        this->upper = val;
        
        fesetround(prev_mode);
    }

    FloatInterval() : lower(0.0f), upper(0.0f) {}
    FloatInterval(float l, float u) : lower(l), upper(u) {}

    FloatInterval(const FloatInterval& other) {
        lower = other.lower;
        upper = other.upper;
    }

    static FloatInterval* intervalize_from_reals(int party, int n, float* reals){
        FloatInterval* intervals = new FloatInterval[n];
        for(int i = 0; i < n; i++){
            intervals[i] = FloatInterval(reals[i]);
        }
        return intervals;
    }

    // Addition
    FloatInterval operator+(const FloatInterval& other) const {
        float l, u;
        int prev_mode = fegetround();

        fesetround(FE_DOWNWARD);
        l = this->lower + other.lower;

        fesetround(FE_UPWARD);
        u = this->upper + other.upper;

        fesetround(prev_mode);
        return FloatInterval(l, u);
    }

    // Subtraction
    FloatInterval operator-(const FloatInterval& other) const {
        float l, u;
        int prev_mode = fegetround();

        fesetround(FE_DOWNWARD);
        l = this->lower - other.upper;

        fesetround(FE_UPWARD);
        u = this->upper - other.lower;

        fesetround(prev_mode);
        return FloatInterval(l, u);
    }

    FloatInterval operator*(const float& scalar) const {
        assert(scalar == 1.0 || scalar == 0.0);

        return (scalar == 1.0) ? FloatInterval(this->lower, this->upper) : FloatInterval(0.0f, 0.0f);
    }

    FloatInterval operator*(const FloatInterval& other) const {
        return FloatInterval::multiply(*this, other);
    }

    FloatInterval negate() const {
        FloatInterval res(*this), res2(*this);
        res.lower = res.upper * -1.0f;
        res.upper = res2.lower * -1.0f;
        return res;
    }

    // Multiplication: Checks all 4 corners to handle negative ranges
    static FloatInterval multiply(const FloatInterval& A, const  FloatInterval& B) {
        float vals[4];
        int prev_mode = fegetround();

        fesetround(FE_DOWNWARD);
        vals[0] = A.lower * B.lower;
        vals[1] = A.lower * B.upper;
        vals[2] = A.upper * B.lower;
        vals[3] = A.upper * B.upper;
        float l = std::min({vals[0], vals[1], vals[2], vals[3]});

        fesetround(FE_UPWARD);
        vals[0] = A.lower * B.lower;
        vals[1] = A.lower * B.upper;
        vals[2] = A.upper * B.lower;
        vals[3] = A.upper * B.upper;
        float u = std::max({vals[0], vals[1], vals[2], vals[3]});

        fesetround(prev_mode);
        return FloatInterval(l, u);
    }

    // Division
    static FloatInterval divide(const FloatInterval& A, const FloatInterval& B) {
        // if (B.lower <= 0.0f && B.upper >= 0.0f) {
        //     cerr << B.lower << " " << B.upper << "\n";
        //     // In a sound system, division by an interval containing zero 
        //     // usually results in [-inf, inf]
        //     error("negative operands in division!");
        //     return FloatInterval(-INFINITY, INFINITY);
        // }

        float vals[4];
        int prev_mode = fegetround();

        fesetround(FE_DOWNWARD);
        vals[0] = A.lower / B.lower;
        vals[1] = A.lower / B.upper;
        vals[2] = A.upper / B.lower;
        vals[3] = A.upper / B.upper;
        float l = std::min({vals[0], vals[1], vals[2], vals[3]});

        fesetround(FE_UPWARD);
        vals[0] = A.lower / B.lower;
        vals[1] = A.lower / B.upper;
        vals[2] = A.upper / B.lower;
        vals[3] = A.upper / B.upper;
        float u = std::max({vals[0], vals[1], vals[2], vals[3]});

        fesetround(prev_mode);
        return FloatInterval(l, u);
    }

    bool* intersected_by(int party, FloatInterval* other, int n) {
        bool* flags = new bool[n];
        for(int i = 0; i < n; i++){
            flags[i] = this->lower <= other[i].upper;
        }
        return flags;
    }

    static float* is_less_eq_zero(int party, FloatInterval* intervals, int n){
        float* flag = new float[n];

        float upper;
        for(int i = 0; i < n; i++){
            if (intervals[i].upper <= 0){
                flag[i] = (float) true;
            } else {
                flag[i] = (float) false;
            }
        }

        return flag;
    }

    static float* is_greater_eq_zero(int party, FloatInterval* intervals, int n){
        float* flag = new float[n];

        float lower;
        for(int i = 0; i < n; i++){
            if (intervals[i].lower >= 0){
                flag[i] = (float) true;
            } else {
                flag[i] = (float) false;
            }
        }

        return flag;
    }

    static FloatInterval* inner_product(FloatInterval* A, FloatInterval* B, int n){
        FloatInterval* result = new FloatInterval(0.0f, 0.0f);
        for(int i = 0; i < n; i++){
            *result = *result + FloatInterval::multiply(A[i], B[i]);
        }
        return result;
    }

    pair<float, float> to_real(){
        return make_pair(this->lower, this->upper);
    }

    string to_string(int bound_type = 0){
        switch (bound_type) {
            case 1:
                return std::to_string(this->lower);
                break;
            
            case 2:
                return std::to_string(this->upper);
                break;

            default:
                break;
        }
        return std::string("[" + std::to_string(this->lower) + ", " + std::to_string(this->upper) + "]");
    }
};

#endif