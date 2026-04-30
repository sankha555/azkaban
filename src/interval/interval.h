#ifndef __INTERVAL_H__
#define __INTERVAL_H__

#pragma once

#include "src/commons.h"
#include "src/utils.h"
#include "src/emp-zk-math/ZKmath-global.h"
#include "src/emp-zk-math/ZKmath-functions.h"


#include <cfenv> 
#include <bit>  

using namespace std;


int msnzb(int64_t x) {
    if (x == 0) return -1;              // no bits set
    return 63 - __builtin_clzll(x);
}

int64_t fxpdiv(int64_t x, int mode = 0)  //y=1/x
{

    // cerr << "x: " << x << "\n"; 

    // step 1: msnzb
    int k = msnzb(x);

    // cerr << "k: " << k << "\n";
    
    // step 2: extend
    int64_t extendLen = DIV_N - 1 - k;
    int64_t z = x << extendLen;

    // cerr << "z: " << z << "\n";

    // step 3: DigitDec
    uint64_t z1 = z & ((1 << (DIV_N - DIV_M - 1))-1);
    uint64_t z2 = z >> (DIV_N - DIV_M - 1);
    z2 = z2 & ((1 << DIV_M)-1);

    // cerr << "z2: " << z2 << "\n";
    // cerr << "z1: " << z1 << "\n";

    // step 4: LUT + y'trunc
    uint64_t a, b;
    if(mode == 1){
        float p = 1.0 + (float(z2) / float(1 << DIV_M));
        float p_next = p + (1.0 / float(1 << DIV_M)) - (1.0 / float(1 << (DIV_N - 1))) + 1;
        float z_sec = p * p_next;

        float A0_val = 1 / p;
        a = (uint64_t)ceil(A0_val * (1ULL << (SCALE + DIV_N - 1)));

        float A1_val = 1.0 / z_sec;
        b = (uint64_t)floor(A1_val * (1ULL << SCALE));

    } else {

        float p = 1.0 + (float(z2) / float(1 << DIV_M));
        float A0 = 1.0 / p;
        a = (uint64_t)floor(A0 * (1ULL << (SCALE + DIV_N - 1)));

        float A1 = 1.0 / (p * p);
        b = (uint64_t)ceil(A1 * (1ULL << SCALE)); // Subtract more to stay below
    }

    // cerr << "a: " << a << "\n";
    // cerr << "b: " << b << "\n";

    int64_t y_prime = a - b * z1;
    // cerr << "y': " << y_prime;

    if (mode == 1){
        int add_one = y_prime & ((1 << (DIV_N - 1))-1);
        add_one = (int) (add_one != 0);
        
        y_prime = (y_prime >> (DIV_N - 1)) + add_one;

    } else {
        y_prime = (y_prime >> (DIV_N - 1));
    }

    // cerr << "y'_t: " << y_prime << "\n";

    y_prime = y_prime << extendLen;

    // cerr << "y'_e: " << y_prime << "\n";


    if (mode == 1){
        int add_one = y_prime & ((1 << (DIV_N - SCALE - 1))-1);
        add_one = (int) (add_one != 0); 

        y_prime = (y_prime >> (DIV_N - SCALE - 1)) + add_one;

    } else {	
        y_prime = (y_prime >> (DIV_N - SCALE - 1));  
    }

    // cerr << "y: " << y_prime << "\n\n";
    return y_prime;
}


template <typename T>
class Interval {
    public:
    T inf;
    T sup;

    Interval() {
        if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)){
            this->inf = T(0);
            this->sup = T(0);
        } else {
            this->inf = IntFp(0, ALICE);
            this->sup = IntFp(0, ALICE);
        }
    }
    Interval(T l, T u) : inf(l), sup(u) {}

    explicit Interval(T val) : inf(val), sup(val) {
        if constexpr (std::is_same_v<T, float>) {
            int prev_mode = fegetround();
            fesetround(FE_DOWNWARD); this->inf = val;
            fesetround(FE_UPWARD);   this->sup = val;
            fesetround(prev_mode);
        }
    }

    Interval(const Interval<T>& other) : inf(other.inf), sup(other.sup) {}

    Interval<IntFp> operator!(){
        IntFp l = this->inf;
        this->inf = this->sup.negate();
        this->sup = l.negate();
        return *this;
    }

    Interval<IntFp> operator&(const IntFp& selector){
        Interval<IntFp> res;
        res.inf = this->inf * selector;
        res.sup = this->sup * selector;
        return res;
    }

    Interval<T> operator+(const Interval<T>& other) const {
        Interval<T> res;
        int prevmode = fegetround();
        if constexpr (TYPE_EQ(T, float)){
            fesetround(FE_DOWNWARD);
        }
        res.inf = this->inf + other.inf;

        if constexpr (TYPE_EQ(T, float)){
            fesetround(FE_UPWARD);
        }
        res.sup = this->sup + other.sup;
    
        fesetround(prevmode);

        return res;
    } 

    Interval<T> operator-(const Interval<T>& other) const {
        Interval<T> res;
        if constexpr (TYPE_EQ(T, float)){
            int prevmode = fegetround();

            fesetround(FE_DOWNWARD);
            res.inf = this->inf - other.sup;

            fesetround(FE_UPWARD);
            res.sup = this->sup - other.inf;

            fesetround(prevmode);
        } else 
        if constexpr (TYPE_EQ(T, int64_t)) {
            res.inf = this->inf - other.sup;
            res.sup = this->sup - other.inf;
        } else 
        if constexpr (TYPE_EQ(T, IntFp)) {
            Interval<T> subtrahend = Interval<T>(other);
            res.inf = this->inf + subtrahend.sup.negate();
            res.sup = this->sup + subtrahend.inf.negate();
        }

        return res;
    }

    Interval<T> operator*(const Interval<T>& other) const {
        Interval<T> res;
        if constexpr (TYPE_EQ(T, float)){
            float al = this->inf;
            float au = this->sup;
            float bl = other.inf;
            float bu = other.sup;

            // Compute all 4 combinations
            int prevmode = fegetround();

            fesetround(FE_DOWNWARD);            
            float p1 = al * bl;
            float p2 = al * bu;
            float p3 = au * bl;
            float p4 = au * bu;
            float res_l = std::min({p1, p2, p3, p4});


            fesetround(FE_UPWARD);
            p1 = al * bl;
            p2 = al * bu;
            p3 = au * bl;
            p4 = au * bu;

            // Find the min and max of the products
            float res_u = std::max({p1, p2, p3, p4});

            fesetround(prevmode);

            res.inf = res_l;
            res.sup = res_u;
        } 

        if constexpr (TYPE_EQ(T, int64_t)){
            int64_t al = this->inf;
            int64_t au = this->sup;
            int64_t bl = other.inf;
            int64_t bu = other.sup;

            // Compute all 4 combinations
            __int128 p1 = (__int128)al * bl;
            __int128 p2 = (__int128)al * bu;
            __int128 p3 = (__int128)au * bl;
            __int128 p4 = (__int128)au * bu;

            // Find the min and max of the products
            __int128 min_p = std::min({p1, p2, p3, p4});
            __int128 max_p = std::max({p1, p2, p3, p4});

            int64_t res_l = static_cast<int64_t>(min_p >> FXPSCALE);
            int64_t res_u = static_cast<int64_t>(max_p >> FXPSCALE);
            if (max_p & (((unsigned __int128)1 << FXPSCALE) - 1)) {
                res_u += 1; 
            }

            res.inf = res_l;
            res.sup = res_u;

        } 

        if constexpr (TYPE_EQ(T, IntFp)) {
            
            IntFp al = this->inf;
            IntFp au = this->sup;
            IntFp bl = other.inf;
            IntFp bu = other.sup;

            // Compute all 4 combinations
            IntFp p[4];
            p[0] = al * bl;
            p[1] = al * bu;
            p[2] = au * bl;
            p[3] = au * bu;

            // Compute min and max of the 4 combinations
            ZKminmax4(party, p, p);

            // Scale down and up
            ZKgeneralTruncAny(party, p, p, 1, FXPSCALE);
            ZKgeneralTruncAnyRoundUp(party, p+1, p+1, 1, FXPSCALE);

            res.inf = p[0];
            res.sup = p[1];
        }

        return res;
    }

    Interval<T> operator^(const Interval<T>& other) const {
        // multiplication without truncation for int64_t and IntFp

        Interval<T> res;

        if constexpr (TYPE_EQ(T, float)) {
            std::cerr << "Error: Operator ^ defined only for int64_t and IntFp";
            exit(1);
        }

        if constexpr (TYPE_EQ(T, IntFp)) {
            IntFp al = this->inf;
            IntFp au = this->sup;
            IntFp bl = other.inf;
            IntFp bu = other.sup;

            // Compute all 4 combinations
            IntFp p[4];
            p[0] = al * bl;
            p[1] = al * bu;
            p[2] = au * bl;
            p[3] = au * bu;

            // Compute min and max of the 4 combinations
            ZKminmax4(party, p, p);

            res.inf = p[0];
            res.sup = p[1];
        }

        return res;
    }

    

    Interval<T> operator/(const Interval<T>& other) const {
        Interval<T> res;
        
        if constexpr (TYPE_EQ(T, float)) {
            float p[4];
            int prev_mode = fegetround();

            fesetround(FE_DOWNWARD);
            p[0] = this->inf / other.inf;
            p[1] = this->inf / other.sup;
            p[2] = this->sup / other.inf;
            p[3] = this->sup / other.sup;
            float l = std::min({p[0], p[1], p[2], p[3]});

            fesetround(FE_UPWARD);
            p[0] = this->inf / other.inf;
            p[1] = this->inf / other.sup;
            p[2] = this->sup / other.inf;
            p[3] = this->sup / other.sup;
            float u = std::max({p[0], p[1], p[2], p[3]});

            fesetround(prev_mode);
            res.inf = l;
            res.sup = u;
        }

        // if constexpr (TYPE_EQ(T, int64_t)) {
        //     int64_t al = this->inf;
        //     int64_t au = this->sup;
        //     int64_t bl = other.inf;
        //     int64_t bu = other.sup;

        //     if (al < 0 || au < 0 || bl <= 0 || bu <= 0) {
        //         cerr << al << " " << au << " " << bl << " " << bu << "\n";
        //         std::cerr << "Error: Division operands must be strictly positive.\n";
        //         exit(1);
        //     }

        //     __int128 dividend_l = (__int128)al << FXPSCALE;
        //     int64_t res_l = static_cast<int64_t>(dividend_l / bu);

        //     __int128 dividend_u = (__int128)au << FXPSCALE;
        //     int64_t res_u = static_cast<int64_t>(dividend_u / bl);
        //     if (dividend_u % bl != 0) {
        //         res_u += 1; 
        //     }

        //     res.inf = res_l;
        //     res.sup = res_u;
        // }

        if constexpr (TYPE_EQ(T, int64_t)) {
            int64_t al = this->inf;
            int64_t au = this->sup;
            int64_t bl = other.inf;
            int64_t bu = other.sup;

            int64_t reci_bl = fxpdiv(bl, 1);
            int64_t reci_bu = fxpdiv(bu, 0);
            
            __int128 quo_l = al * reci_bu;
            quo_l = quo_l >> FXPSCALE;

            __int128 quo_u = au * reci_bl;
            if(quo_u % (1 << FXPSCALE)){
                quo_u = quo_u >> FXPSCALE;
                quo_u++;
            } else {
                quo_u = quo_u >> FXPSCALE;
            }

            res.inf = quo_l;
            res.sup = quo_u;
        }

        if constexpr (TYPE_EQ(T, IntFp)) {
            IntFp al = this->inf;
            IntFp au = this->sup;
            IntFp bl = other.inf;
            IntFp bu = other.sup;

            // if(party == ALICE){
            //     assert(HIGH64(al.value) < (PR-1)/2);
            //     assert(HIGH64(au.value) < (PR-1)/2);
            //     assert(HIGH64(bl.value) < (PR-1)/2);
            //     assert(HIGH64(bu.value) < (PR-1)/2);
            // }

            IntFp reciprocal;

            // // Lower Bound: floor(al / bu)
            ZKDiv(party, &bu, &reciprocal, 1, -1);
            IntFp l = al * reciprocal;
            ZKgeneralTruncAny(party, &l, &l, 1, FXPSCALE);

            // // Upper Bound: ceil(au / bl)
            ZKDiv(party, &bl, &reciprocal, 1, 1);
            IntFp u = au * reciprocal;
            ZKgeneralTruncAnyRoundUp(party, &u, &u, 1, FXPSCALE);

            res.inf = l;
            res.sup = u;
        }

        return res;
    }

    Interval<T>& operator+=(const Interval<T>& other) {
        this->inf = this->inf + other.inf;
        this->sup = this->sup + other.sup;
        return *this;
    }

    Interval<T>& operator-=(const Interval<T>& other) {
        if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)) {
            this->inf = this->inf - other.sup;
            this->sup = this->sup - other.inf;
        } else if constexpr (TYPE_EQ(T, IntFp)) {
            this->inf = this->inf + other.sup.negate();
            this->sup = this->sup + other.inf.negate();
        }
        
        return *this;
    }

    Interval<IntFp> mult(const Interval<IntFp>& other, int mode = 0){
        if(mode == 0){
            return *this * other;
        } else if(mode == 1){
            // special case where second operand is of the form [w, w+1]
        }
        return Interval<IntFp>();
    }

    Interval<T> absolute(){
        Interval<T> res;

        if constexpr (TYPE_EQ(T, float) || TYPE_EQ(T, int64_t)){
            if(this->inf >= 0){
                res.inf = this->inf;
                res.sup = this->sup;
            } else if (this->sup <= 0){
                res.inf = -this->sup;
                res.sup = -this->inf;
            } else {
                res.inf = 0;
                res.sup = max(-this->inf, this->sup);
            }
        }

        if constexpr (TYPE_EQ(T, IntFp)){
            
            IntFp* cmp = new IntFp[3];
            cmp[0] = this->inf;
            cmp[1] = this->sup;
            cmp[2] = this->sup + this->inf; // assuming sup > 0 and inf < 0 => cmp[2] >= 0 is |sup| >= |inf|
            ZKcmpPositive(party, cmp, ZERO_COMP_CONSTANT, cmp, 3);

            Interval<T> res;

            IntFp c1 = cmp[0];
            IntFp c2 = NOT(cmp[0]) * NOT(cmp[1]);
            IntFp c3 = NOT(cmp[0]) * cmp[1];

            res.inf = c1 * this->inf + 
                      c2 * (this->sup).negate() + 
                      FIELD_ZERO;
            
            res.sup = c1 * this->inf + 
                      c2 * (this->sup).negate() + 
                      c3 * (cmp[2] * this->sup + NOT(cmp[2]) * (this->inf).negate());
        }

        return res;
    }


    static Interval<T> intervalize_from_real(float real){

        Interval<T> interval;

        if constexpr (TYPE_EQ(T, float)) {
            interval = Interval<float>(real);
        }

        if constexpr (TYPE_EQ(T, int64_t)) {
            float scaled = real * (1LL << FXPSCALE);
            int64_t l = static_cast<int64_t>(std::floor(scaled));
            int64_t u = static_cast<int64_t>(std::ceil(scaled));

            interval = Interval<int64_t>(l, u);
        }

        if constexpr (TYPE_EQ(T, IntFp)) {
            uint64_t unsigned_scaled_real;
            IntFp l, u;

            if(party == ALICE){
                int64_t scaled_real = floor(real * (1LL << FXPSCALE));

                unsigned_scaled_real = (scaled_real >= 0 ? scaled_real : PR + scaled_real);
            }
            l = IntFp(unsigned_scaled_real, ALICE);

            if(party == ALICE){
                int64_t scaled_real = ceil(real * (1LL << FXPSCALE));

                unsigned_scaled_real = (scaled_real >= 0 ? scaled_real : PR + scaled_real);
            }
            u = IntFp(unsigned_scaled_real, ALICE);

            IntFp diff = u + l.negate();
            ZKcmpRealVrfyPositive(party, &diff, ZERO_COMP_CONSTANT, &diff, 1); 
            // cerr << "diff: " << diff.reveal() << "\n";

            interval = Interval<IntFp>(l, u);
        }

        return interval;
    }

    string to_string() {
        if constexpr (TYPE_EQ(T, float)){
            return "[" + std::to_string(this->inf) + ", " + std::to_string(this->sup) + "]"; 
        }

        if constexpr (TYPE_EQ(T, int64_t)){
            return "[" + std::to_string((this->inf * 1.0) / (1LL << FXPSCALE)) + ", " + std::to_string((this->sup * 1.0) / (1LL << FXPSCALE)) + "]"; 
        }

        if constexpr (TYPE_EQ(T, IntFp)){
            return "[" + std::to_string(CLT(this->inf)) + ", " + std::to_string(CLT(this->sup)) + "]";
        }

        return "";
    }    

    static Interval<T> inner_product(size_t n, vector<Interval<T>> A, vector<Interval<T>> B) {
        
        Interval<T> res;
        
        if constexpr (TYPE_EQ(T, float)){
            for(size_t i = 0; i < n; i++){

                // cout << "A: " << A[i].to_string() << " " << "; B: " << B[i].to_string() << "\n"; 

                res = res + (A[i] * B[i]);
            }
        }

        if constexpr (TYPE_EQ(T, int64_t)){
            __int128 acc_l = 0;
            __int128 acc_u = 0;

            for(size_t i = 0; i < n; i++) {
                __int128 al = (__int128)A[i].inf;
                __int128 au = (__int128)A[i].sup;
                __int128 bl = (__int128)B[i].inf;
                __int128 bu = (__int128)B[i].sup;

                // Interval Multiplication: [min(products), max(products)]
                __int128 p1 = al * bl;
                __int128 p2 = al * bu;
                __int128 p3 = au * bl;
                __int128 p4 = au * bu;

                acc_l += std::min({p1, p2, p3, p4});
                acc_u += std::max({p1, p2, p3, p4});
            }

            // Now truncate the final sums
            int64_t final_inf = static_cast<int64_t>(acc_l >> FXPSCALE);
            int64_t final_sup = static_cast<int64_t>(acc_u >> FXPSCALE);
            
            // Round up the upper bound if there are any fractional bits lost
            // Using a mask for all bits below FXPSCALE for correct rounding
            unsigned __int128 mask = ((unsigned __int128)1 << FXPSCALE) - 1;
            if (acc_u & mask) {
                final_sup += 1;
            }

            res.inf = final_inf;
            res.sup = final_sup;
        }

        if constexpr (TYPE_EQ(T, IntFp)) {
            
            // accumulate
            for(size_t i = 0; i < n; i++){
                // cout << "A: " << A[i].to_string() << " " << "; B: " << B[i].to_string() << "\n"; 

                res += A[i] ^ B[i];
            }

            // truncate
            ZKgeneralTruncAny(party, &res.inf, &res.inf, 1, FXPSCALE);
            ZKgeneralTruncAnyRoundUp(party, &res.sup, &res.sup, 1, FXPSCALE);
        }

        return res;
    }
};

class FloatInterval;
class FXPInterval;

#include "src/interval/float-interval.h"
#include "src/interval/fixedpoint-interval.h"

#endif