#!/usr/bin/env python3
# Concert7 - open source true stereo convolution reverb
# Copyright (C) 2026 pschopen, GPL-3.0-or-later
"""
Analyses impulse responses exported with `concert7-dev ir` (or any WAV file):
octave band reverb times (T20 / T30 from Schroeder curves) and an optional plot
with energy decay curves and a spectrogram.

    python3 tools/analyze_ir.py IR.wav [more.wav ...] [--plot out.png]

Needs numpy; matplotlib only for --plot.
"""
import argparse
import struct
import sys

import numpy as np

BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000]


def read_wav(path):
    """Minimal WAV reader (PCM 16/24/32 and float32), returns (channels x samples, rate)."""
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt = 12, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE:  # extensible: real format in the sub format GUID
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, rate, bits)
        elif cid == b"data" and fmt:
            tag, ch, rate, bits = fmt
            if tag == 3 and bits == 32:
                x = np.frombuffer(body, dtype="<f4").astype(np.float64)
            elif bits == 16:
                x = np.frombuffer(body, dtype="<i2") / 32768.0
            elif bits == 24:
                b = np.frombuffer(body, dtype=np.uint8).reshape(-1, 3)
                v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
                v[v >= 1 << 23] -= 1 << 24
                x = v / float(1 << 23)
            elif bits == 32:
                x = np.frombuffer(body, dtype="<i4") / 2147483648.0
            else:
                raise ValueError(f"{path}: unsupported format {fmt}")
            n = len(x) // ch
            return x[: n * ch].reshape(n, ch).T, rate
        pos += 8 + size + (size & 1)
    raise ValueError(f"{path}: no data chunk")


def band_power(x, rate, lo, hi):
    """Zero-phase brick wall band filter via FFT (fine for analysis)."""
    n = 1 << int(np.ceil(np.log2(x.shape[-1] * 2)))
    spec = np.fft.rfft(x, n, axis=-1)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    spec[..., (f < lo) | (f >= hi)] = 0
    y = np.fft.irfft(spec, n, axis=-1)[..., : x.shape[-1]]
    return (y ** 2).sum(axis=0)


def edc_db(power):
    s = np.cumsum(power[::-1])[::-1]
    return 10 * np.log10(np.maximum(s / max(s[0], 1e-30), 1e-30))


def reverb_time(edc, rate, start=-5.0, end=-25.0):
    i0 = np.argmax(edc <= start)
    i1 = np.argmax(edc <= end)
    if i1 <= i0 + 2:
        return float("nan")
    t = np.arange(i0, i1) / rate
    slope = np.polyfit(t, edc[i0:i1], 1)[0]
    return -60.0 / slope if slope < 0 else float("nan")


def analyse(path):
    x, rate = read_wav(path)
    result = {"path": path, "rate": rate, "seconds": x.shape[1] / rate, "bands": {}}
    for fc in BANDS:
        p = band_power(x, rate, fc / np.sqrt(2), fc * np.sqrt(2))
        e = edc_db(p)
        result["bands"][fc] = (reverb_time(e, rate), reverb_time(e, rate, -5, -35), e)
    pb = (x ** 2).sum(axis=0)
    result["broadband"] = edc_db(pb)
    result["power"] = pb
    result["x"] = x
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--plot", help="write a PNG with EDCs and spectrograms")
    args = ap.parse_args()

    results = [analyse(f) for f in args.files]
    print(f"{'file':40s} {'len':>6s} " + " ".join(f"{b:>7d}" for b in BANDS) + "   (T20 seconds per octave band)")
    for r in results:
        name = r["path"].split("/")[-1][:40]
        print(f"{name:40s} {r['seconds']:6.2f} " + " ".join(f"{r['bands'][b][0]:7.2f}" for b in BANDS))

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, axes = plt.subplots(2, len(results), figsize=(6 * len(results), 8), squeeze=False)
        for i, r in enumerate(results):
            ax = axes[0][i]
            t = np.arange(len(r["broadband"])) / r["rate"]
            ax.plot(t, r["broadband"], color="k", lw=1.5, label="broadband")
            for b in BANDS[1::2]:
                ax.plot(t, r["bands"][b][2], lw=0.9, label=f"{b} Hz")
            ax.set_ylim(-90, 3)
            ax.set_xlabel("s")
            ax.set_ylabel("EDC dB")
            ax.set_title(r["path"].split("/")[-1], fontsize=9)
            ax.grid(alpha=0.3)
            ax.legend(fontsize=7)
            ax = axes[1][i]
            mono = r["x"].sum(axis=0)
            ax.specgram(mono + 1e-9, NFFT=1024, Fs=r["rate"], noverlap=768, cmap="magma", vmin=-160)
            ax.set_ylim(0, 16000)
            ax.set_xlabel("s")
            ax.set_ylabel("Hz")
        fig.tight_layout()
        fig.savefig(args.plot, dpi=90)
        print("plot:", args.plot)


if __name__ == "__main__":
    sys.exit(main())
