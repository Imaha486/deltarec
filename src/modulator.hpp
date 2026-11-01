// modulator.hpp - 1-bit delta-sigma modulator (software "DSD recorder").
//
// Topology: error-feedback form with an exact rational NTF.
//   y[n] = u[n] + ((N/D) - 1) * e[n]        (strictly causal part)
//   v[n] = sign(y[n] + dither)              (1-bit quantiser)
//   e[n] = v[n] - y[n]                      (quantiser error)
// which gives V = U + NTF * E, i.e. STF = 1 exactly.
// The filter is implemented in transposed direct form II in double
// precision, so coefficient sensitivity (the usual reason hardware uses
// CIFB/CRFB integrator chains) is not a concern here.
//
// Input scale: +-1.0 corresponds to 100% modulation depth, which a 1-bit
// modulator cannot reach.  The converter maps PCM full scale to 0.5
// (the SACD "0 dB SACD = 50% modulation" convention).  A hard limiter on
// the modulator input plus a state reset on overload keep it from locking up.
#pragma once
#include <cstdint>
#include <vector>

#include "ntf.hpp"

namespace deltarec {

class DeltaSigmaModulator {
public:
    struct Stats {
        uint64_t samples = 0;
        uint64_t inputClips = 0;   // input hit the hard limiter
        uint64_t resets = 0;       // modulator was reset after overload
        double peakInput = 0.0;    // |u| before limiting, in modulation units
        double peakY = 0.0;        // |y| at the quantiser
    };

    DeltaSigmaModulator() = default;

    // ntf: designed NTF; inputLimit: hard clip of |u|; dither: TPDF amplitude
    // at the quantiser input (0 disables); resetThreshold: |y| above which the
    // modulator is declared unstable and its state zeroed.
    DeltaSigmaModulator(const NtfDesign& ntf, double inputLimit, double dither,
                        double resetThreshold = 8.0, uint32_t seed = 0x5eed1234u)
        : b_(ntf.b), a_(ntf.a), n_(ntf.order), w_(ntf.order + 1, 0.0),
          inputLimit_(inputLimit), dither_(dither), resetThreshold_(resetThreshold),
          rng_(0x9E3779B97F4A7C15ull ^ (uint64_t(seed) * 0xD1342543DE82EF95ull)) {
        if (rng_ == 0) rng_ = 0x9E3779B97F4A7C15ull;
    }

    // Process `count` input samples; appends the 1-bit output packed 8 samples
    // per byte, oldest sample in the LSB (DSF bit order), to `out`.  A partial
    // byte is carried over to the next call; call finish() at the end.
    void process(const double* u, size_t count, std::vector<uint8_t>& out) {
        out.reserve(out.size() + count / 8 + 1);
        const int n = n_;
        for (size_t i = 0; i < count; ++i) {
            double x = u[i];
            double ax = x < 0 ? -x : x;
            if (ax > stats_.peakInput) stats_.peakInput = ax;
            if (ax > inputLimit_) {
                x = x < 0 ? -inputLimit_ : inputLimit_;
                ++stats_.inputClips;
            }

            double y = x + w_[1];
            double ay = y < 0 ? -y : y;
            if (ay > stats_.peakY) stats_.peakY = ay;
            if (ay > resetThreshold_) {
                // Overload: zero the noise-shaping state and carry on.
                for (double& s : w_) s = 0.0;
                y = x;
                ++stats_.resets;
            }

            double yq = y;
            if (dither_ > 0.0) yq += dither_ * (uniform() - uniform());  // TPDF in [-dither, +dither]
            double v = yq >= 0.0 ? 1.0 : -1.0;
            double e = v - y;
            double outv = e + w_[1];  // full N/D output
            for (int k = 1; k < n; ++k) w_[k] = b_[k] * e + w_[k + 1] - a_[k] * outv;
            w_[n] = b_[n] * e - a_[n] * outv;

            if (v > 0) acc_ |= uint8_t(1u << accBits_);
            if (++accBits_ == 8) { out.push_back(acc_); acc_ = 0; accBits_ = 0; }
        }
        stats_.samples += count;
    }

    // Flush a trailing partial byte (zero padded).  Returns true if one was written.
    bool finish(std::vector<uint8_t>& out) {
        if (accBits_ == 0) return false;
        out.push_back(acc_);
        acc_ = 0; accBits_ = 0;
        return true;
    }

    const Stats& stats() const { return stats_; }

private:
    uint8_t acc_ = 0;
    int accBits_ = 0;
    std::vector<double> b_, a_;
    int n_ = 0;
    std::vector<double> w_;
    double inputLimit_ = 0.6;
    double dither_ = 0.0;
    double resetThreshold_ = 8.0;
    uint64_t rng_ = 1;
    Stats stats_;

    // xorshift64* : fast, more than adequate for dither.  Returns [0, 1).
    inline double uniform() {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        uint64_t r = rng_ * 0x2545F4914F6CDD1Dull;
        return double(r >> 11) * (1.0 / 9007199254740992.0);
    }
};

}  // namespace deltarec
