// decoder.hpp - WAV / FLAC input via dr_wav and dr_flac (public domain / MIT-0).
#pragma once
#include <cstdint>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "dr_flac.h"
#include "dr_wav.h"

namespace deltarec {

class PcmDecoder {
public:
    explicit PcmDecoder(const std::string& path) {
        std::string lower = path;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool isFlac = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".flac") == 0;
        if (!isFlac) {
            wav_ = std::make_unique<drwav>();
            if (!drwav_init_file(wav_.get(), path.c_str(), nullptr)) {
                wav_.reset();
                // Not a WAV: maybe FLAC with another extension.
                flac_ = drflac_open_file(path.c_str(), nullptr);
                if (!flac_) throw std::runtime_error("cannot open input (WAV/FLAC expected): " + path);
            }
        } else {
            flac_ = drflac_open_file(path.c_str(), nullptr);
            if (!flac_) throw std::runtime_error("cannot open FLAC: " + path);
        }
        if (wav_) {
            channels_ = wav_->channels;
            rate_ = wav_->sampleRate;
            bits_ = wav_->bitsPerSample;
            frames_ = wav_->totalPCMFrameCount;
            isFloat_ = (wav_->translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT);
        } else {
            channels_ = flac_->channels;
            rate_ = flac_->sampleRate;
            bits_ = flac_->bitsPerSample;
            frames_ = flac_->totalPCMFrameCount;
        }
    }
    ~PcmDecoder() {
        if (wav_) drwav_uninit(wav_.get());
        if (flac_) drflac_close(flac_);
    }
    PcmDecoder(const PcmDecoder&) = delete;
    PcmDecoder& operator=(const PcmDecoder&) = delete;

    uint32_t channels() const { return channels_; }
    uint32_t sampleRate() const { return rate_; }
    uint32_t bitsPerSample() const { return bits_; }
    uint64_t totalFrames() const { return frames_; }  // may be 0 if unknown
    const char* formatName() const { return wav_ ? (isFloat_ ? "WAV (float)" : "WAV") : "FLAC"; }

    // Read up to `frames` frames as interleaved doubles in [-1, 1).
    size_t read(size_t frames, std::vector<double>& out) {
        scratch_.resize(frames * channels_);
        uint64_t got = 0;
        if (wav_) {
            if (isFloat_) {
                fscratch_.resize(frames * channels_);
                got = drwav_read_pcm_frames_f32(wav_.get(), frames, fscratch_.data());
                out.resize(static_cast<size_t>(got) * channels_);
                for (size_t i = 0; i < out.size(); ++i) out[i] = fscratch_[i];
                return static_cast<size_t>(got);
            }
            got = drwav_read_pcm_frames_s32(wav_.get(), frames, scratch_.data());
        } else {
            got = drflac_read_pcm_frames_s32(flac_, frames, scratch_.data());
        }
        out.resize(static_cast<size_t>(got) * channels_);
        const double scale = 1.0 / 2147483648.0;
        for (size_t i = 0; i < out.size(); ++i) out[i] = scratch_[i] * scale;
        return static_cast<size_t>(got);
    }

private:
    std::unique_ptr<drwav> wav_;
    drflac* flac_ = nullptr;
    uint32_t channels_ = 0, rate_ = 0, bits_ = 0;
    uint64_t frames_ = 0;
    bool isFloat_ = false;
    std::vector<int32_t> scratch_;
    std::vector<float> fscratch_;
};

}  // namespace deltarec
