#ifndef __FIELD_INTERVAL_H__
#define __FIELD_INTERVAL_H__

#include "commons.h"

class IntFpInterval {
    public:

    IntFp inf;
    IntFp sup;

    IntFpInterval() {
        this->inf = FIELD_ZERO;
        this->sup = FIELD_ZERO;
    }

    IntFpInterval(IntFp el) {
        this->inf = el;
        this->sup = el;
    }

    IntFpInterval(IntFp lower, IntFp upper) {
        this->inf = lower;
        this->sup = upper;
    }

    IntFpInterval(const IntFpInterval& other) {
        this->inf = other.inf;
        this->sup = other.sup;
    }


    static IntFpInterval* intervalize_from_reals(int party, int n, float* reals){
        IntFpInterval* intervals = new IntFpInterval[n];

        uint64_t unsigned_scaled_real;
        IntFp l, u;
        for(int i = 0; i < n; i++){
            if(party == ALICE){
                int64_t scaled_real = floor(reals[i] * (1ULL << FXPSCALE));

                unsigned_scaled_real = (scaled_real >= 0 ? scaled_real : PR + scaled_real);
            }
            l = IntFp(unsigned_scaled_real, ALICE);

            if(party == ALICE){
                int64_t scaled_real = ceil(reals[i] * (1ULL << FXPSCALE));

                unsigned_scaled_real = (scaled_real >= 0 ? scaled_real : PR + scaled_real);
            }
            u = IntFp(unsigned_scaled_real, ALICE);

            intervals[i] = IntFpInterval(l, u); 
        }

        return intervals;
    }


    IntFpInterval operator+(const IntFpInterval& other) const {
        IntFpInterval res;
        res.inf = this->inf + other.inf;
        res.sup = this->sup + other.sup;
        return res;
    }

    IntFpInterval operator-(const IntFpInterval& other) const {
        IntFpInterval res;
        IntFpInterval substrahend = IntFpInterval(other);
        res.inf = this->inf + substrahend.sup.negate();
        res.sup = this->sup + substrahend.inf.negate();
        return res;
    }

    IntFpInterval operator*(const IntFp& field_el) const {
        IntFpInterval res;
        res.inf = this->inf * field_el;
        res.sup = this->sup * field_el;
        return res;
    }

    // Multiplication
    static IntFpInterval multiply(int party, const IntFpInterval& A, const IntFpInterval& B, bool delay_trunc = true) {
        IntFp al = A.inf;
        IntFp au = A.sup;
        IntFp bl = B.inf;
        IntFp bu = B.sup;

        // Compute all 4 combinations
        IntFp* p = new IntFp[4];
        p[0] = al * bl;
        p[1] = al * bu;
        p[2] = au * bl;
        p[3] = au * bu;

        // Compute min and max of the 4 combinations
        ZKminmax4(party, p, p);

        if (!delay_trunc){
            // Sound shift: Floor the lower, Ceil the upper
            ZKgeneralTruncAny(party, p, p, 1, FXPSCALE);
            ZKgeneralTruncAnyRoundUp(party, p+1, p+1, 1, FXPSCALE);
        }

        return IntFpInterval(p[0], p[1]);
    }

    static IntFpInterval divide(int party, const IntFpInterval& A, const IntFpInterval& B) {
        IntFp al = A.inf;
        IntFp au = A.sup;
        IntFp bl = B.inf;
        IntFp bu = B.sup;

        // TODO: check all operands are positive

        IntFp reciprocal;

        // Lower Bound: floor(al / bu)
        ZKDiv(party, &bu, &reciprocal, 1, 1);
        IntFp l = al * reciprocal;
        ZKgeneralTruncAny(party, &l, &l, 1, FXPSCALE);

        // Upper Bound: ceil(au / bl)
        ZKDiv(party, &bl, &reciprocal, 1, 2);
        IntFp u = au * reciprocal;
        ZKgeneralTruncAnyRoundUp(party, &u, &u, 1, FXPSCALE);

        IntFpInterval res(l, u);
        return res;
    }

    static IntFpInterval* inner_product(int party, IntFpInterval* A, IntFpInterval* B, int n, bool delay_trunc = false){
        // cerr << "IP entered;\t";
        IntFpInterval* result = new IntFpInterval();
        for(int i = 0; i < n; i++){
            *result = *result + IntFpInterval::multiply(party, A[i], B[i], true);
        }

        if(!delay_trunc){
           IntFpInterval::truncate(party, result, 1, FXPSCALE);
        }
        
        // cerr << "Inner product result: " << result->to_string() << "\n";
        return result;
    }

    static void truncate(int party, IntFpInterval* x, int n, int trunclen){
        IntFp* lowers = new IntFp[n];
        IntFp* uppers = new IntFp[n];

        for(int i = 0; i < n; i++){
            lowers[i] = x[i].inf;
            uppers[i] = x[i].sup;
        }

        ZKgeneralTruncAny(party, lowers, lowers, n, trunclen);
        ZKgeneralTruncAnyRoundUp(party, uppers, uppers, n, trunclen);

        for(int i = 0; i < n; i++){
            x[i].inf = lowers[i];
            x[i].sup = uppers[i];
        }

        delete[] lowers;
        delete[] uppers;
    }
};

#endif