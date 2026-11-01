# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/) and the project uses [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-01

### Added
- WAV / FLAC (dr_wav / dr_flac) input, 1–6 channels, integer and float PCM.
- DSD64 / DSD128 / DSD256 / DSD512 output in the 44.1 kHz or 48 kHz family.
- DSF and DFF (DSDIFF 1.5) writers with 64-bit sizes (files > 4 GB).
- Two-stage polyphase Kaiser-windowed-sinc upsampler (rational L/M in stage 1, 160 dB stopband, integer group delay removed).
- Run-time designed delta-sigma NTF (orders 2–8, Gauss–Legendre zeros, Butterworth poles bisected to H_inf), error-feedback 1-bit modulator with TPDF dither, input limiter and overload reset.
- SACD level convention (PCM 0 dBFS → 50 % modulation) with `--gain`.
- `--info` design dump, progress / statistics output, per-channel threading.
- CMake (presets for VS "Open Folder", macOS, Linux), Visual Studio 2022/2026 solution in `vs/`, GitHub Actions build and release workflows.
- `tools/verify_dsf.py` measurement script.
