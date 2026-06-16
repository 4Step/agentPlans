#pragma once
// Sampling + math helpers mirroring the R routines the scripts rely on
// (sample(prob=..., replace=TRUE), and qnorm() for truck VOT).
//
// NOTE: R's RNG stream cannot be reproduced bit-for-bit in C++. Results are
// statistically equivalent and reproducible for a fixed seed, but individual
// draws differ from the R output. Aggregate trip tables match within sampling
// noise.
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace ap {

class Rng {
public:
    explicit Rng(uint64_t seed) : gen_(seed) {}
    void seed(uint64_t s) { gen_.seed(s); }

    // Weighted sample-with-replacement of one value from `values` using `probs`
    // (weights need not sum to 1; normalized internally, as in R sample()).
    template <class T>
    T sample_weighted(const std::vector<T>& values, const std::vector<double>& probs) {
        std::discrete_distribution<size_t> d(probs.begin(), probs.end());
        return values[d(gen_)];
    }

    // Build a reusable weighted sampler (avoids rebuilding the distribution).
    std::discrete_distribution<size_t> make_disc(const std::vector<double>& probs) {
        return std::discrete_distribution<size_t>(probs.begin(), probs.end());
    }
    size_t draw(std::discrete_distribution<size_t>& d) { return d(gen_); }

    // Uniform integer in [lo, hi] (inclusive), as R sample(lo:hi, ... replace=T).
    int uniform_int(int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(gen_);
    }

    std::mt19937_64& engine() { return gen_; }

private:
    std::mt19937_64 gen_;
};

// Inverse normal CDF (quantile) — Acklam's algorithm. Equivalent to R qnorm().
inline double qnorm(double p, double mean = 0.0, double sd = 1.0) {
    if (p <= 0.0) return -INFINITY;
    if (p >= 1.0) return INFINITY;
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double plow = 0.02425, phigh = 1.0 - plow;
    double q, r, x;
    if (p < plow) {
        q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= phigh) {
        q = p - 0.5; r = q*q;
        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
            (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
             ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    return mean + sd * x;
}

} // namespace ap
