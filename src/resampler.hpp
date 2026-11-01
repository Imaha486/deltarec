// resampler.hpp - Rational (L/M) polyphase FIR interpolator with a Kaiser
// windowed-sinc prototype.  Streaming, double precision.
//
// The converter chains two of these:
//   stage 1: fs_in -> fi  (sharp anti-image filter, runs at a low rate)
//   stage 2: fi -> fs_dsd (very relaxed filter, ~10 taps per output)
// so that the total cost is ~15-20 multiply-adds per DSD sample instead
// of the ~200 a single-stage design would need.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "ntf.hpp"  // kPi

namespace deltarec {

namespace detail {
inline double besselI0(double x) {
    // Series expansion, converges quickly for the beta range we use (< 40).
    double sum = 1.0, term = 1.0, hx = 0.5 * x;
    for (int k = 1; k < 500; ++k) {
        term *= (hx / k) * (hx / k);
        sum += term;
        if (term < sum * 1e-18) break;
    }
    return sum;
}
}  // namespace detail

class PolyphaseResampler {
public:
    struct Spec {
        uint32_t L = 1;          // interpolation factor
        uint32_t M = 1;          // decimation factor
        double passband = 0.0;   // Hz, end of passband (relative to input fs)
        double stopband = 0.0;   // Hz, start of stopband
        double inputRate = 0.0;  // Hz
        double attenuationDb = 160.0;
    };

    PolyphaseResampler() = default;
    explicit PolyphaseResampler(const Spec& s) { init(s); }

    void init(const Spec& s) {
        if (s.L == 0 || s.M == 0) throw std::runtime_error("resampler: L and M must be > 0");
        if (s.stopband <= s.passband) throw std::runtime_error("resampler: stopband must exceed passband");
        L_ = s.L;
        M_ = s.M;
        const double highRate = s.inputRate * L_;
        const double dw = 2.0 * kPi * (s.stopband - s.passband) / highRate;  // transition width, rad/sample
        const double A = s.attenuationDb;
        double beta = A > 50.0 ? 0.1102 * (A - 8.7)
                    : A >= 21.0 ? 0.5842 * std::pow(A - 21.0, 0.4) + 0.07886 * (A - 21.0) : 0.0;
        double Nf = (A - 8.0) / (2.285 * dw);
        K_ = static_cast<uint32_t>(std::ceil(Nf / L_));
        if (K_ < 4) K_ = 4;
        N_ = K_ * L_;

        // Prototype lowpass at the high rate, cutoff mid-way through the transition band.
        const double fc = 0.5 * (s.passband + s.stopband) / highRate;  // cycles/sample
        // Use an odd effective length so the group delay is an integer number
        // of high-rate samples (the last tap is simply zero when N is even).
        const uint32_t Neff = (N_ % 2 == 0) ? N_ - 1 : N_;
        std::vector<double> h(N_, 0.0);
        const double center = 0.5 * (Neff - 1.0);
        const double i0b = detail::besselI0(beta);
        for (uint32_t n = 0; n < Neff; ++n) {
            double t = n - center;
            double sinc = (t == 0.0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);
            double r = 2.0 * t / (Neff - 1.0);
            double win = detail::besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h[n] = sinc * win;
        }
        // Normalise DC gain to exactly L (sum of each polyphase branch == 1).
        double sum = 0.0;
        for (double v : h) sum += v;
        for (double& v : h) v *= L_ / sum;

        // Polyphase decomposition: coef_[p][k] multiplies x[base - (K-1) + k]
        // (window stored oldest-first), i.e. h[p + (K-1-k)*L].
        coef_.assign(static_cast<size_t>(L_) * K_, 0.0);
        for (uint32_t p = 0; p < L_; ++p)
            for (uint32_t k = 0; k < K_; ++k)
                coef_[static_cast<size_t>(p) * K_ + k] = h[p + (K_ - 1 - k) * L_];

        buf_.assign(static_cast<size_t>(2) * K_, 0.0);
        pos_ = 0;
        inIndex_ = 0;
        tBase_ = 0;
        phase_ = 0;
        // Group delay is (N-1)/2 high-rate samples; drop that many output samples.
        skipOut_ = static_cast<uint64_t>(std::llround(((Neff - 1.0) / 2.0) / M_));
        flushInputs_ = K_ / 2 + 2;
    }

    uint32_t L() const { return L_; }
    uint32_t M() const { return M_; }
    uint32_t tapsPerPhase() const { return K_; }
    uint32_t totalTaps() const { return N_; }

    // Feed `count` input samples, appending output samples to `out`.
    void process(const double* in, size_t count, std::vector<double>& out) {
        out.reserve(out.size() + (count * L_) / M_ + 2);
        if (M_ == 1) { for (size_t i = 0; i < count; ++i) pushInterp(in[i], out); }
        else         { for (size_t i = 0; i < count; ++i) pushRational(in[i], out); }
    }

    // Push zeros to drain the filter's group delay.
    void flush(std::vector<double>& out) {
        for (uint32_t i = 0; i < flushInputs_; ++i) {
            if (M_ == 1) pushInterp(0.0, out); else pushRational(0.0, out);
        }
    }

private:
    inline const double* store(double x) {
        buf_[pos_] = x;
        buf_[pos_ + K_] = x;
        const double* win = &buf_[pos_ + 1];  // oldest-first window of K samples
        pos_ = (pos_ + 1 == K_) ? 0 : pos_ + 1;
        return win;
    }

    inline double dot(uint32_t phase, const double* win) const {
        const double* c = &coef_[static_cast<size_t>(phase) * K_];
        double acc = 0.0;
        for (uint32_t k = 0; k < K_; ++k) acc += c[k] * win[k];
        return acc;
    }

    // Pure interpolation (M == 1): every input yields exactly L outputs.
    inline void pushInterp(double x, std::vector<double>& out) {
        const double* win = store(x);
        uint32_t p = 0;
        if (skipOut_ > 0) {
            while (p < L_ && skipOut_ > 0) { --skipOut_; ++p; }
        }
        for (; p < L_; ++p) out.push_back(dot(p, win));
    }

    // Rational L/M: outputs are produced whenever the next output instant
    // falls on the newest input sample (no divisions in the loop).
    inline void pushRational(double x, std::vector<double>& out) {
        const double* win = store(x);
        const uint64_t newest = inIndex_++;
        while (tBase_ <= newest) {
            double acc = dot(phase_, win);
            if (skipOut_ > 0) --skipOut_;
            else out.push_back(acc);
            phase_ += M_;
            while (phase_ >= L_) { phase_ -= L_; ++tBase_; }
        }
    }

    uint32_t L_ = 1, M_ = 1, K_ = 0, N_ = 0;
    std::vector<double> coef_;
    std::vector<double> buf_;
    uint32_t pos_ = 0;
    uint64_t inIndex_ = 0;
    uint64_t tBase_ = 0;   // input index of the next output instant
    uint32_t phase_ = 0;   // polyphase branch of the next output instant
    uint64_t skipOut_ = 0;
    uint32_t flushInputs_ = 0;
};

}  // namespace deltarec
