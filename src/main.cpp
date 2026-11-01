// deltarec - offline PCM (WAV/FLAC) to 1-bit DSD (DSF/DFF) converter.
//
// Signal chain per channel:
//   PCM -> polyphase FIR (stage 1, sharp) -> polyphase FIR (stage 2, relaxed)
//       -> delta-sigma modulator (1-bit, noise shaped) -> DSF/DFF packer
// i.e. exactly what a DSD recorder's ADC does, performed numerically.
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <numeric>
#include <string>
#include <vector>

#include "decoder.hpp"
#include "dsdwriter.hpp"
#include "modulator.hpp"
#include "ntf.hpp"
#include "resampler.hpp"

using namespace deltarec;

namespace {

const char* kVersion = "0.1.0";

struct Options {
    std::string input;
    std::string output;
    int rate = 256;             // DSD64 / 128 / 256 / 512
    uint32_t base = 44100;      // 44100 or 48000 family
    std::string format;         // dsf | dff (default from extension)
    int order = 7;
    double hinf = 1.4;
    double gainDb = 0.0;        // extra gain; 0 dB => PCM FS -> 50% modulation
    double limit = 0.6;         // modulator input hard limit (modulation units)
    double dither = 0.05;       // TPDF dither amplitude at quantiser
    double passband = 0.0;      // Hz, 0 = auto (0.4535 * fs_in)
    double attenuation = 160.0; // dB, anti-image filter stopband
    bool info = false;
    bool quiet = false;
};

void usage() {
    std::fprintf(stderr,
        "deltarec %s - PCM (WAV/FLAC) -> DSD (DSF/DFF) converter\n"
        "\n"
        "usage: deltarec [options] input.(wav|flac) [-o output.dsf]\n"
        "\n"
        "  -o, --output FILE     output path (default: input name with .dsf/.dff)\n"
        "  -r, --rate N          DSD rate multiple: 64, 128, 256 (default), 512\n"
        "  -b, --base HZ         rate family: 44100 (default) or 48000\n"
        "  -f, --format FMT      dsf (default) or dff\n"
        "      --order N         modulator order 2..8 (default 7)\n"
        "      --hinf X          NTF out-of-band gain, Lee's rule (default 1.4;\n"
        "                        higher = more noise shaping, less input headroom)\n"
        "  -g, --gain DB         extra gain in dB (default 0: PCM 0 dBFS -> 50%% modulation,\n"
        "                        the SACD convention; use -3 to leave more headroom)\n"
        "      --limit X         hard limit on modulator input, 0..1 (default 0.6)\n"
        "      --dither X        TPDF dither at quantiser, 0 disables (default 0.05)\n"
        "      --passband HZ     anti-image filter passband edge (default 0.4535*fs_in,\n"
        "                        i.e. 20 kHz for 44.1 kHz material)\n"
        "      --attenuation DB  anti-image filter stopband attenuation (default 160)\n"
        "      --info            print the DSP design for these settings and exit\n"
        "  -q, --quiet           no progress output\n"
        "  -h, --help            this text\n"
        "  -V, --version         print version\n",
        kVersion);
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(); std::exit(0); }
        else if (a == "-V" || a == "--version") { std::printf("deltarec %s\n", kVersion); std::exit(0); }
        else if (a == "-o" || a == "--output") o.output = need("-o");
        else if (a == "-r" || a == "--rate") o.rate = std::atoi(need("-r"));
        else if (a == "-b" || a == "--base") o.base = static_cast<uint32_t>(std::atoi(need("-b")));
        else if (a == "-f" || a == "--format") o.format = need("-f");
        else if (a == "--order") o.order = std::atoi(need("--order"));
        else if (a == "--hinf") o.hinf = std::atof(need("--hinf"));
        else if (a == "-g" || a == "--gain") o.gainDb = std::atof(need("-g"));
        else if (a == "--limit") o.limit = std::atof(need("--limit"));
        else if (a == "--dither") o.dither = std::atof(need("--dither"));
        else if (a == "--passband") o.passband = std::atof(need("--passband"));
        else if (a == "--attenuation") o.attenuation = std::atof(need("--attenuation"));
        else if (a == "--info") o.info = true;
        else if (a == "-q" || a == "--quiet") o.quiet = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
        else if (o.input.empty()) o.input = a;
        else { std::fprintf(stderr, "unexpected argument %s\n", a.c_str()); return false; }
    }
    if (o.input.empty()) return false;
    if (!(o.rate == 64 || o.rate == 128 || o.rate == 256 || o.rate == 512)) { std::fprintf(stderr, "--rate must be 64, 128, 256 or 512\n"); return false; }
    if (!(o.base == 44100 || o.base == 48000)) { std::fprintf(stderr, "--base must be 44100 or 48000\n"); return false; }
    if (o.order < 2 || o.order > 8) { std::fprintf(stderr, "--order must be 2..8\n"); return false; }
    if (o.limit <= 0.0 || o.limit >= 1.0) { std::fprintf(stderr, "--limit must be in (0,1)\n"); return false; }
    if (o.format.empty()) {
        std::string ext = o.output.size() >= 4 ? o.output.substr(o.output.size() - 4) : "";
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        o.format = (ext == ".dff") ? "dff" : "dsf";
    }
    if (o.format != "dsf" && o.format != "dff") { std::fprintf(stderr, "--format must be dsf or dff\n"); return false; }
    if (o.output.empty()) {
        size_t dot = o.input.find_last_of('.');
        size_t slash = o.input.find_last_of("/\\");
        std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? o.input.substr(0, dot) : o.input;
        o.output = stem + "." + o.format;
    }
    return true;
}

uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

struct Plan {
    uint32_t fsIn = 0, fsDsd = 0, fi = 0;
    uint32_t L1 = 1, M1 = 1, L2 = 1;
    double passband = 0, stop1 = 0, stop2 = 0;
    double osr = 0;
    PolyphaseResampler::Spec s1, s2;
    NtfDesign ntf;
};

Plan makePlan(const Options& o, uint32_t fsIn) {
    Plan p;
    p.fsIn = fsIn;
    p.fsDsd = o.base * static_cast<uint32_t>(o.rate);
    // Stage-2 factor: largest power of two with intermediate rate >= 8 * fs_in.
    p.L2 = 1;
    while ((p.L2 * 2) <= 64 && p.fsDsd % (p.L2 * 2) == 0 && p.fsDsd / (p.L2 * 2) >= 8 * fsIn) p.L2 *= 2;
    p.fi = p.fsDsd / p.L2;
    uint32_t g = gcd32(p.fi, fsIn);
    p.L1 = p.fi / g;
    p.M1 = fsIn / g;
    p.passband = o.passband > 0 ? o.passband : 0.4535 * fsIn;
    if (p.passband >= 0.5 * fsIn) p.passband = 0.49 * fsIn;
    p.stop1 = std::min(0.5 * fsIn, p.fi - p.passband);
    p.stop2 = p.fi - p.passband;
    p.s1 = {p.L1, p.M1, p.passband, p.stop1, double(fsIn), o.attenuation};
    p.s2 = {p.L2, 1, p.passband, p.stop2, double(p.fi), o.attenuation};
    // OSR for the NTF: half the DSD rate over the audio band edge.  We fix the
    // band at 20 kHz (or the passband if wider) so hi-res sources keep their
    // ultrasonic content below the noise shelf.
    double band = std::max(20000.0, p.passband);
    p.osr = p.fsDsd / (2.0 * band);
    p.ntf = designNtf(o.order, p.osr, o.hinf);
    return p;
}

void printPlan(const Plan& p, const Options& o, uint32_t channels) {
    PolyphaseResampler r1(p.s1);
    std::fprintf(stderr, "  input      : %u Hz, %u ch\n", p.fsIn, channels);
    std::fprintf(stderr, "  output     : %u Hz 1-bit (DSD%d, %s family), %s\n", p.fsDsd, o.rate,
                 o.base == 44100 ? "44.1k" : "48k", o.format.c_str());
    std::fprintf(stderr, "  stage 1    : x%u/%u -> %u Hz, %u taps/phase (%u total), pass %.0f Hz, stop %.0f Hz\n",
                 p.L1, p.M1, p.fi, r1.tapsPerPhase(), r1.totalTaps(), p.passband, p.stop1);
    if (p.L2 > 1) {
        PolyphaseResampler r2(p.s2);
        std::fprintf(stderr, "  stage 2    : x%u -> %u Hz, %u taps/phase (%u total), stop %.0f Hz\n",
                     p.L2, p.fsDsd, r2.tapsPerPhase(), r2.totalTaps(), p.stop2);
    }
    std::fprintf(stderr, "  modulator  : order %d, H_inf %.2f, OSR %.1f, theoretical in-band SNR %.0f dB @ -6 dBFS\n",
                 o.order, o.hinf, p.osr, theoreticalSnrDb(p.ntf, 0.5));
    std::fprintf(stderr, "  level      : PCM 0 dBFS -> %.1f%% modulation (gain %+.1f dB), limiter at %.0f%%, dither %.3f\n",
                 50.0 * std::pow(10.0, o.gainDb / 20.0), o.gainDb, 100.0 * o.limit, o.dither);
    std::fprintf(stderr, "  NTF b      :");
    for (double v : p.ntf.b) std::fprintf(stderr, " %.9g", v);
    std::fprintf(stderr, "\n  NTF a      :");
    for (double v : p.ntf.a) std::fprintf(stderr, " %.9g", v);
    std::fprintf(stderr, "\n");
}

struct ChannelPipe {
    PolyphaseResampler s1, s2;
    bool twoStage = true;
    DeltaSigmaModulator mod;
    std::vector<double> buf1, buf2;
    std::vector<uint8_t> bits;

    void run(const double* in, size_t n, bool flush) {
        buf1.clear(); buf2.clear(); bits.clear();
        s1.process(in, n, buf1);
        if (flush) s1.flush(buf1);
        const std::vector<double>* src = &buf1;
        if (twoStage) {
            s2.process(buf1.data(), buf1.size(), buf2);
            if (flush) s2.flush(buf2);
            src = &buf2;
        }
        mod.process(src->data(), src->size(), bits);
        if (flush) mod.finish(bits);
    }
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) { usage(); return 2; }

    try {
        PcmDecoder dec(o.input);
        const uint32_t ch = dec.channels();
        const uint32_t fsIn = dec.sampleRate();
        if (ch == 0 || ch > 6) throw std::runtime_error("1..6 channels supported");
        Plan plan = makePlan(o, fsIn);

        if (!o.quiet || o.info) {
            std::fprintf(stderr, "deltarec %s\n", kVersion);
            std::fprintf(stderr, "  source     : %s, %u-bit, %s\n", o.input.c_str(), dec.bitsPerSample(), dec.formatName());
            printPlan(plan, o, ch);
        }
        if (o.info) return 0;

        const double gain = 0.5 * std::pow(10.0, o.gainDb / 20.0);

        std::vector<ChannelPipe> pipes(ch);
        for (uint32_t c = 0; c < ch; ++c) {
            pipes[c].s1.init(plan.s1);
            pipes[c].twoStage = plan.L2 > 1;
            if (pipes[c].twoStage) pipes[c].s2.init(plan.s2);
            pipes[c].mod = DeltaSigmaModulator(plan.ntf, o.limit, o.dither, 8.0, 0x5eed1234u + c * 7919u);
        }

        DsdWriter writer(o.output, o.format == "dff" ? DsdWriter::Format::DFF : DsdWriter::Format::DSF, ch, plan.fsDsd);

        const size_t blockFrames = 8192;
        std::vector<double> inter;
        std::vector<std::vector<double>> chan(ch);
        std::vector<std::vector<uint8_t>> outBits(ch);
        uint64_t done = 0;
        const uint64_t total = dec.totalFrames();
        auto t0 = std::chrono::steady_clock::now();
        int lastPct = -1;
        bool eof = false;

        while (!eof) {
            size_t got = dec.read(blockFrames, inter);
            eof = got < blockFrames;
            for (uint32_t c = 0; c < ch; ++c) {
                chan[c].resize(got);
                for (size_t i = 0; i < got; ++i) chan[c][i] = inter[i * ch + c] * gain;
            }
            std::vector<std::future<void>> jobs;
            for (uint32_t c = 0; c < ch; ++c) {
                jobs.push_back(std::async(std::launch::async, [&, c] { pipes[c].run(chan[c].data(), got, eof); }));
            }
            for (auto& j : jobs) j.get();
            for (uint32_t c = 0; c < ch; ++c) outBits[c].swap(pipes[c].bits);
            writer.write(outBits);
            done += got;
            if (!o.quiet && total > 0) {
                int pct = static_cast<int>(100.0 * done / total);
                if (pct != lastPct) {
                    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    std::fprintf(stderr, "\r  converting : %3d%%  (%.1fx realtime)   ", pct, (done / double(fsIn)) / std::max(el, 1e-9));
                    std::fflush(stderr);
                    lastPct = pct;
                }
            }
        }
        writer.finish(pipes[0].mod.stats().samples);
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        if (!o.quiet) {
            std::fprintf(stderr, "\r  converting : done in %.1f s (%.1fx realtime)      \n", el, (done / double(fsIn)) / std::max(el, 1e-9));
            double peak = 0; uint64_t clips = 0, resets = 0;
            for (auto& p : pipes) {
                peak = std::max(peak, p.mod.stats().peakInput);
                clips += p.mod.stats().inputClips;
                resets += p.mod.stats().resets;
            }
            std::fprintf(stderr, "  peak level : %.1f%% modulation (%.2f dBFS PCM equivalent)\n", 100.0 * peak, 20.0 * std::log10(std::max(peak, 1e-12) / 0.5));
            if (clips) std::fprintf(stderr, "  WARNING    : %llu samples hit the limiter - consider --gain -3\n", static_cast<unsigned long long>(clips));
            if (resets) std::fprintf(stderr, "  WARNING    : modulator reset %llu times (overload)\n", static_cast<unsigned long long>(resets));
            std::fprintf(stderr, "  wrote      : %s\n", o.output.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
