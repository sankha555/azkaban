#ifndef __FP_TYPE_H__
#define __FP_TYPE_H__

// ---------------------------------------------------------------------------
//  Fp : a plaintext element of the prime field used by the ZK backend,
//       p = 2^61 - 1  (== emp-zk PR = 2305843009213693951).
//
//  Values are fixed-point (scaled by 2^FXPSCALE, like the int64_t reference
//  path).  A field element x lives in [0, p).  Its SIGNED interpretation is
//        x            if x <= (p-1)/2      (non-negative)
//        x - p        otherwise            (negative)
//  so the representable signed range is  [-(p-1)/2, (p-1)/2].
//
//  "Overflow" = an arithmetic result whose TRUE (non-modular) value falls
//  outside [-(p-1)/2, (p-1)/2].  In the real field that value wraps modulo p,
//  which flips its sign (a positive result becomes negative, or vice-versa) --
//  exactly the corruption we want to catch.  Every Fp operation therefore
//  computes the result in a wide (__int128) accumulator, checks whether it
//  fits the field, records an event if it does not, and then stores the
//  field-canonical (wrapped) value so the rest of the computation mirrors what
//  the real ZK prover would actually compute.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <iostream>
#include <cstdlib>

#include "src/commons.h"   // FXPSCALE (runtime global), PR (from emp headers)

// ---------------------------------------------------------------------------
// Global overflow bookkeeping (single shared instance via C++17 inline var).
// ---------------------------------------------------------------------------
struct FpOverflowTracker {
    unsigned long long checks = 0;             // total field-relevant operations checked
    unsigned long long events = 0;             // total overflow events observed
    bool               verbose = true;         // log the first `log_cap` events
    unsigned long long log_cap = 20;           // cap on logged events (count keeps rising)
    bool               abort_on_overflow = true;  // abort() on the very first event
    std::string        first_op;               // name of the first operation that overflowed

    void reset() { checks = 0; events = 0; first_op.clear(); }
};

inline FpOverflowTracker FP_OVERFLOW;

// __int128 has no default stream inserter; format it by hand for diagnostics.
inline std::string fp_i128_to_string(__int128 x) {
    if (x == 0) return "0";
    bool neg = x < 0;
    unsigned __int128 u = neg ? (unsigned __int128)(-(x + 1)) + 1 : (unsigned __int128)x;
    char buf[64];
    int pos = 64;
    while (u) { buf[--pos] = char('0' + (int)(u % 10)); u /= 10; }
    std::string s(buf + pos, buf + 64);
    return neg ? "-" + s : s;
}

class Fp {
public:
    static constexpr uint64_t P       = (uint64_t(1) << 61) - 1;   // 2^61 - 1
    static constexpr int64_t  POS_MAX = int64_t((P - 1) / 2);      // 2^60 - 1
    static constexpr int64_t  NEG_MIN = -POS_MAX;                  // -(2^60 - 1)

    int64_t v;   // field-canonical SIGNED value, always in [NEG_MIN, POS_MAX]

    Fp() : v(0) {}
    Fp(int64_t x) : v((int64_t) wrap((__int128) x)) {}

    // Build directly from an already-canonical value (no reduction/check).
    static Fp raw(int64_t canonical) { Fp f; f.v = canonical; return f; }

    // Does the true value fit the signed field range?
    static bool fits(__int128 t) {
        return t >= (__int128) NEG_MIN && t <= (__int128) POS_MAX;
    }

    // Reduce a true value to its field-canonical SIGNED representative.
    static __int128 wrap(__int128 t) {
        const __int128 P128 = (__int128) P;
        __int128 r = t % P128;
        if (r < 0) r += P128;                 // now in [0, P)
        if (r > (__int128) POS_MAX) r -= P128; // fold high half to negatives
        return r;                              // in [NEG_MIN, POS_MAX]
    }

    static void record(__int128 t, const char* op) {
        FP_OVERFLOW.events++;
        if (FP_OVERFLOW.first_op.empty()) FP_OVERFLOW.first_op = op;
        if (FP_OVERFLOW.verbose && FP_OVERFLOW.events <= FP_OVERFLOW.log_cap) {
            std::cerr << "[fp-overflow] #" << FP_OVERFLOW.events
                      << " op=" << op
                      << " true=" << fp_i128_to_string(t)
                      << " real=" << ((double) t / (double)(1LL << FXPSCALE))
                      << " wraps-to=" << fp_i128_to_string(wrap(t))
                      << " (|x| must be <= (p-1)/2 = " << fp_i128_to_string((__int128) POS_MAX) << ")\n";
        }
        if (FP_OVERFLOW.abort_on_overflow) {
            std::cerr << "[fp-overflow] aborting (abort_on_overflow set)\n";
            std::abort();
        }
    }

    // Check a true value for field overflow, record it if needed, and return
    // the field-canonical (wrapped) representative.
    static int64_t check(__int128 t, const char* op) {
        FP_OVERFLOW.checks++;
        if (!fits(t)) record(t, op);
        return (int64_t) wrap(t);
    }

    __int128 wide()    const { return (__int128) v; }
    int64_t  signed64() const { return v; }
    double   to_real() const { return (double) v / (double)(1LL << FXPSCALE); }

    // ---- checked field arithmetic (stores wrapped canonical value) ----
    Fp operator+(const Fp& o) const { return raw(check((__int128) v + o.v, "+")); }
    Fp operator-(const Fp& o) const { return raw(check((__int128) v - o.v, "-")); }
    Fp operator-()            const { return raw(check(-(__int128) v, "unary-")); }
    // raw (untruncated) modular multiply; the Interval class does the
    // fixed-point truncating multiply itself, so this is only for completeness.
    Fp operator*(const Fp& o) const { return raw(check((__int128) v * o.v, "*")); }

    Fp& operator+=(const Fp& o) { v = check((__int128) v + o.v, "+="); return *this; }
    Fp& operator-=(const Fp& o) { v = check((__int128) v - o.v, "-="); return *this; }

    // ---- comparisons on the signed canonical value ----
    bool operator< (const Fp& o) const { return v <  o.v; }
    bool operator> (const Fp& o) const { return v >  o.v; }
    bool operator<=(const Fp& o) const { return v <= o.v; }
    bool operator>=(const Fp& o) const { return v >= o.v; }
    bool operator==(const Fp& o) const { return v == o.v; }
    bool operator!=(const Fp& o) const { return v != o.v; }

    bool operator< (int64_t x) const { return v <  x; }
    bool operator> (int64_t x) const { return v >  x; }
    bool operator<=(int64_t x) const { return v <= x; }
    bool operator>=(int64_t x) const { return v >= x; }
    bool operator==(int64_t x) const { return v == x; }
    bool operator!=(int64_t x) const { return v != x; }
};

// abs() found by ADL (arg is Fp); result magnitude <= POS_MAX so always fits.
inline Fp abs(const Fp& x) { return Fp::raw(x.v < 0 ? -x.v : x.v); }

inline std::ostream& operator<<(std::ostream& os, const Fp& x) {
    return os << fp_i128_to_string(x.v);
}

// Sanity: our hard-coded modulus must equal the backend prime.
static_assert(Fp::P == 2305843009213693951ULL, "Fp::P must be 2^61 - 1");

#endif
