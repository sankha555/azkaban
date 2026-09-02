#ifndef __FP_INTERVAL_H__
#define __FP_INTERVAL_H__

#pragma once

#include "src/commons.h"
#include "src/utils.h"

#include "src/cleartext/fp.h"

#include <bit>

using namespace std;

int msnzb(int64_t x) {
    if (x == 0) return -1;              // no bits set
    return 63 - __builtin_clzll(x);
}

int64_t fxpdiv(int64_t x, int mode = 0)  //y=1/x
{
    // step 1: msnzb
    int k = msnzb(x);

    // step 2: extend
    int64_t extendLen = DIV_N - 1 - k;
    int64_t z = x << extendLen;

    // step 3: DigitDec
    uint64_t z1 = z & ((1 << (DIV_N - DIV_M - 1))-1);
    uint64_t z2 = z >> (DIV_N - DIV_M - 1);
    z2 = z2 & ((1 << DIV_M)-1);

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

    int64_t y_prime = a - b * z1;

    if (mode == 1){
        int add_one = y_prime & ((1 << (DIV_N - 1))-1);
        add_one = (int) (add_one != 0);

        y_prime = (y_prime >> (DIV_N - 1)) + add_one;

    } else {
        y_prime = (y_prime >> (DIV_N - 1));
    }

    y_prime = y_prime << extendLen;

    if (mode == 1){
        int add_one = y_prime & ((1 << (DIV_N - SCALE - 1))-1);
        add_one = (int) (add_one != 0);

        y_prime = (y_prime >> (DIV_N - SCALE - 1)) + add_one;

    } else {
        y_prime = (y_prime >> (DIV_N - SCALE - 1));
    }

    return y_prime;
}


template <typename T>
class Interval {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

    public:
    T inf;
    T sup;

    Interval() : inf(T(0)), sup(T(0)) {}
    Interval(T l, T u) : inf(l), sup(u) {}

    explicit Interval(T val) : inf(val), sup(val) {}

    Interval(const Interval<T>& other) : inf(other.inf), sup(other.sup) {}

    Interval<T> operator+(const Interval<T>& other) const {
        Interval<T> res;
        res.inf = this->inf + other.inf;
        res.sup = this->sup + other.sup;
        return res;
    }

    Interval<T> operator-(const Interval<T>& other) const {
        Interval<T> res;
        res.inf = this->inf - other.sup;
        res.sup = this->sup - other.inf;
        return res;
    }

    Interval<T> operator*(const Interval<T>& other) const {
        Interval<T> res;

        __int128 al = this->inf.wide();
        __int128 au = this->sup.wide();
        __int128 bl = other.inf.wide();
        __int128 bu = other.sup.wide();

        __int128 p1 = al * bl;
        __int128 p2 = al * bu;
        __int128 p3 = au * bl;
        __int128 p4 = au * bu;

        __int128 min_p = std::min({p1, p2, p3, p4});
        __int128 max_p = std::max({p1, p2, p3, p4});

        int64_t wl = Fp::check(min_p, "Interval::operator* (untruncated product)");
        int64_t wu = Fp::check(max_p, "Interval::operator* (untruncated product)");

        int64_t res_l = static_cast<int64_t>((__int128)wl >> FXPSCALE);
        int64_t res_u = static_cast<int64_t>((__int128)wu >> FXPSCALE);
        if ((unsigned __int128)wu & (((unsigned __int128)1 << FXPSCALE) - 1)) {
            res_u += 1;
        }

        res.inf = Fp::raw(res_l);
        res.sup = Fp::raw(res_u);

        return res;
    }

    Interval<T> operator/(const Interval<T>& other) const {
        Interval<T> res;

        int64_t al = this->inf.signed64();
        int64_t au = this->sup.signed64();
        int64_t bl = other.inf.signed64();
        int64_t bu = other.sup.signed64();

        int64_t reci_bl = fxpdiv(bl, 1);
        int64_t reci_bu = fxpdiv(bu, 0);

        // UNTRUNCATED dividend*reciprocal must fit the field before truncation.
        __int128 prod_l = (__int128)al * reci_bu;
        int64_t wl = Fp::check(prod_l, "Interval::operator/ (untruncated)");
        __int128 quo_l = (__int128)wl >> FXPSCALE;

        __int128 prod_u = (__int128)au * reci_bl;
        int64_t wu = Fp::check(prod_u, "Interval::operator/ (untruncated)");
        __int128 quo_u;
        if((__int128)wu % (1 << FXPSCALE)){
            quo_u = (__int128)wu >> FXPSCALE;
            quo_u++;
        } else {
            quo_u = (__int128)wu >> FXPSCALE;
        }

        res.inf = Fp::raw((int64_t)quo_l);
        res.sup = Fp::raw((int64_t)quo_u);

        return res;
    }

    Interval<T>& operator+=(const Interval<T>& other) {
        this->inf = this->inf + other.inf;
        this->sup = this->sup + other.sup;
        return *this;
    }

    Interval<T>& operator-=(const Interval<T>& other) {
        this->inf = this->inf - other.sup;
        this->sup = this->sup - other.inf;
        return *this;
    }

    Interval<T> absolute(){
        Interval<T> res;

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

        return res;
    }


    static Interval<T> intervalize_from_real(float real){

        float scaled = real * (1LL << FXPSCALE);
        int64_t l = static_cast<int64_t>(std::floor(scaled));
        int64_t u = static_cast<int64_t>(std::ceil(scaled));

        Fp::check((__int128)l, "intervalize_from_real (input)");
        Fp::check((__int128)u, "intervalize_from_real (input)");

        return Interval<Fp>(Fp(l), Fp(u));
    }

    string to_string() {
        return "[" + std::to_string(this->inf.to_real()) + ", " + std::to_string(this->sup.to_real()) + "]";
    }

    static Interval<T> inner_product(size_t n, vector<Interval<T>> A, vector<Interval<T>> B) {

        Interval<T> res;

        __int128 acc_l = 0;
        __int128 acc_u = 0;

        for(size_t i = 0; i < n; i++) {
            __int128 al = A[i].inf.wide();
            __int128 au = A[i].sup.wide();
            __int128 bl = B[i].inf.wide();
            __int128 bu = B[i].sup.wide();

            __int128 p1 = al * bl;
            __int128 p2 = al * bu;
            __int128 p3 = au * bl;
            __int128 p4 = au * bu;

            acc_l += std::min({p1, p2, p3, p4});
            acc_u += std::max({p1, p2, p3, p4});
        }

        // The field accumulates the untruncated products and truncates ONCE;
        // it is the accumulated sum that must fit the field.
        int64_t wl = Fp::check(acc_l, "Interval::inner_product (untruncated accumulator)");
        int64_t wu = Fp::check(acc_u, "Interval::inner_product (untruncated accumulator)");

        int64_t final_inf = static_cast<int64_t>((__int128)wl >> FXPSCALE);
        int64_t final_sup = static_cast<int64_t>((__int128)wu >> FXPSCALE);

        unsigned __int128 mask = ((unsigned __int128)1 << FXPSCALE) - 1;
        if ((unsigned __int128)wu & mask) {
            final_sup += 1;
        }

        res.inf = Fp::raw(final_inf);
        res.sup = Fp::raw(final_sup);

        return res;
    }
};

#endif
