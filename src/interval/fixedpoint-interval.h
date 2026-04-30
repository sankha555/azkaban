#ifndef __FIXEDPOINT_INTERVAL_H_
#define __FIXEDPOINT_INTERVAL_H_

#include "interval.h"

class FXPInterval {
    public:
    int64_t inf;
    int64_t sup;

    // Constructor that maintains interval soundness
    explicit FXPInterval(float val, bool already_scaled = false) {
        float scaled = already_scaled ? val : val * (1 << FXPSCALE);
        
        this->inf = static_cast<int64_t>(std::floor(scaled));
        this->sup = static_cast<int64_t>(std::ceil(scaled));
    }

    FXPInterval() : inf(0), sup(0) {}

    // Addition: Lower + Lower, Upper + Upper
    FXPInterval operator+(const FXPInterval& other) const {
        FXPInterval res;
        res.inf = this->inf + other.inf;
        res.sup = this->sup + other.sup;
        return res;
    }

    // Subtraction: Lower - Other.Upper, Upper - Other.Lower
    FXPInterval operator-(const FXPInterval& other) const {
        FXPInterval res;
        res.inf = this->inf - other.sup;
        res.sup = this->sup - other.inf;
        return res;
    }
    
    FXPInterval operator^(const FXPInterval& other) const {
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

        // Sound shift: Floor the lower, Ceil the upper
        int64_t res_l = static_cast<int64_t>(min_p >> FXPSCALE);
        int64_t res_u = static_cast<int64_t>(max_p >> FXPSCALE);
        if (max_p & (1ULL << FXPSCALE)) {
            res_u += 1; 
        }

        FXPInterval res; 
        res.inf = res_l;
        res.sup = res_u;
        return res;
    }

    FXPInterval operator*(const FXPInterval& other) const {
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
        if (max_p & (1ULL << FXPSCALE)) {
            res_u += 1; 
        }

        FXPInterval res; 
        res.inf = res_l;
        res.sup = res_u;
        return res;
    }    

    FXPInterval operator/(const FXPInterval& other) const {
        int64_t al = this->inf;
        int64_t au = this->sup;
        int64_t bl = other.inf;
        int64_t bu = other.sup;

        if (al < 0 || au < 0 || bl <= 0 || bu <= 0) {
            cerr << al << " " << au << " " << bl << " " << bu << "\n";
            std::cerr << "Error: Division operands must be strictly positive.\n";
            exit(1);
        }

        __int128 dividend_l = (__int128)al << FXPSCALE;
        int64_t res_l = static_cast<int64_t>(dividend_l / bu);

        __int128 dividend_u = (__int128)au << FXPSCALE;
        int64_t res_u = static_cast<int64_t>(dividend_u / bl);
        if (dividend_u % bl != 0) {
            res_u += 1; 
        }

        FXPInterval res;
        res.inf = res_l;
        res.sup = res_u;
        return res;
    }


    FXPInterval absolute() const {
        FXPInterval res;

        if(this->inf >= 0){
            res.inf = this->inf;
            res.sup = this->sup;
        } else if (this->sup <= 0){
            res.inf = -this->sup;
            res.sup = -this->inf;
        } else {
            res.inf = 0;
            res.sup = max(-res.inf, res.sup);
        }

        return res;
    }

    bool operator>=(const FXPInterval& other) const {
        return this->inf >= other.sup;
    }

    bool operator<=(const FXPInterval& other) const {
        return this->sup <= other.inf;
    }
};

#endif