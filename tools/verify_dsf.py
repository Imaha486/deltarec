#!/usr/bin/env python3
"""verify_dsf.py - measure a deltarec output (or any DSF/DFF file).

    python3 tools/verify_dsf.py file.dsf [--tone HZ] [--band HZ] [--seconds S]

Reads the 1-bit stream, takes a segment from the middle, applies a Kaiser
window (beta 38, sidelobes < -300 dB) and reports the in-band SNR relative to
the given test tone (default 1 kHz) and the noise density at a few
frequencies.  Requires numpy + scipy.
"""
import argparse
import struct
import sys

import numpy as np
from scipy.signal.windows import kaiser


def read_dsf(path):
    d = open(path, "rb").read()
    if d[:4] != b"DSD ":
        raise ValueError("not a DSF file")
    ch = struct.unpack("<I", d[52:56])[0]
    rate = struct.unpack("<I", d[56:60])[0]
    ns = struct.unpack("<Q", d[64:72])[0]
    blk = struct.unpack("<I", d[72:76])[0]
    if d[80:84] != b"data":
        raise ValueError("data chunk not found")
    dsz = struct.unpack("<Q", d[84:92])[0]
    data = np.frombuffer(d[92:92 + dsz - 12], dtype=np.uint8)
    nblk = len(data) // (blk * ch)
    data = data[: nblk * blk * ch].reshape(nblk, ch, blk).transpose(1, 0, 2).reshape(ch, -1)
    bits = np.unpackbits(data, axis=1, bitorder="little")[:, :ns]
    return rate, bits.astype(np.float64) * 2 - 1


def read_dff(path):
    d = open(path, "rb").read()
    if d[:4] != b"FRM8" or d[12:16] != b"DSD ":
        raise ValueError("not a DSDIFF file")
    pos, rate, ch = 16, None, None
    while pos < len(d):
        tag = d[pos:pos + 4]
        sz = struct.unpack(">Q", d[pos + 4:pos + 12])[0]
        body = pos + 12
        if tag == b"PROP":
            p = body + 4
            while p < body + sz:
                t = d[p:p + 4]
                s = struct.unpack(">Q", d[p + 4:p + 12])[0]
                if t == b"FS  ":
                    rate = struct.unpack(">I", d[p + 12:p + 16])[0]
                if t == b"CHNL":
                    ch = struct.unpack(">H", d[p + 12:p + 14])[0]
                p += 12 + s
        if tag == b"DSD ":
            data = np.frombuffer(d[body:body + sz], dtype=np.uint8).reshape(-1, ch).T
            return rate, np.unpackbits(data, axis=1, bitorder="big").astype(np.float64) * 2 - 1
        pos = body + sz
    raise ValueError("DSD chunk not found")


def analyze(rate, x, tone, band, seconds):
    n = int(min(len(x), rate * seconds))
    n = 1 << int(np.log2(n))
    start = (len(x) - n) // 2
    x = x[start:start + n]
    w = kaiser(n, 38.0)
    X = np.fft.rfft(x * w)
    P = np.abs(X) ** 2
    fr = np.fft.rfftfreq(n, 1 / rate)
    cg = w.sum() / 2  # coherent gain for a sine of amplitude 1
    k = int(round(tone * n / rate))
    half = 30
    sig = P[k - half:k + half + 1].sum()
    mask = fr <= band
    mask[:half] = False
    mask[k - half:k + half + 1] = False
    harm = 0.0
    for h in range(2, 40):
        kh = h * k
        if h * tone >= band:
            break
        harm += P[kh - half:kh + half + 1].sum()
        mask[kh - half:kh + half + 1] = False
    noise = P[mask].sum()
    level = 20 * np.log10(np.sqrt(sig) / cg)
    print(f"  tone {tone:.0f} Hz : {level:7.2f} dB re 100% modulation ({level + 6.02:+.2f} dBFS PCM at the 50% convention)")
    print(f"  SNR 0-{band/1000:.0f} kHz : {10*np.log10(sig/noise):7.1f} dB (harmonics excluded)")
    print(f"  THD           : {10*np.log10(max(harm,1e-300)/sig):7.1f} dB")
    print(f"  noise density (per {rate/n:.2f} Hz bin, re 100% modulation):")
    for f in (1000, 5000, 10000, 20000, 30000, 50000, 100000, 200000):
        if f >= rate / 2:
            break
        j = int(f * n / rate)
        if abs(j - k) < 200:
            continue
        print(f"    {f/1000:6.0f} kHz : {10*np.log10(P[j-100:j+100].mean()/cg**2):7.1f} dB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--tone", type=float, default=1000.0)
    ap.add_argument("--band", type=float, default=20000.0)
    ap.add_argument("--seconds", type=float, default=0.5)
    ap.add_argument("--channel", type=int, default=0)
    a = ap.parse_args()
    rate, bits = (read_dff if a.file.lower().endswith(".dff") else read_dsf)(a.file)
    print(f"{a.file}: {rate} Hz, {bits.shape[0]} ch, {bits.shape[1]/rate:.2f} s, mean {bits[a.channel].mean():+.5f}")
    analyze(rate, bits[a.channel], a.tone, a.band, a.seconds)


if __name__ == "__main__":
    sys.exit(main())
