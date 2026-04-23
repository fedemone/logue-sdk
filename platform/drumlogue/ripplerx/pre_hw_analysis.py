#!/usr/bin/env python3
"""
pre_hw_analysis.py

Dependency-light, pre-hardware analysis harness for comparing rendered presets
against reference WAV samples with time-domain + frequency-domain metrics.

Designed for environments where numpy/librosa are unavailable.
"""

from __future__ import annotations

import argparse
import array
import json
import math
import os
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


EPS = 1e-12


def hann(n: int) -> List[float]:
    if n <= 1:
        return [1.0]
    return [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1)) for i in range(n)]


def rms(buf: List[float]) -> float:
    if not buf:
        return 0.0
    return math.sqrt(sum(x * x for x in buf) / len(buf))


def normalize(buf: List[float]) -> List[float]:
    peak = max((abs(x) for x in buf), default=0.0)
    if peak < EPS:
        return buf[:]
    return [x / peak for x in buf]


def read_wav_mono(path: Path) -> Tuple[List[float], int]:
    with wave.open(str(path), "rb") as wf:
        channels = wf.getnchannels()
        sr = wf.getframerate()
        sw = wf.getsampwidth()
        n = wf.getnframes()
        raw = wf.readframes(n)

    if sw not in (2, 3, 4):
        raise ValueError(f"Unsupported sample width ({sw} bytes) in {path}")

    if sw == 2:
        data = array.array("h")
        data.frombytes(raw)
        scale = 32768.0
    elif sw == 3:
        # 24-bit PCM little-endian -> signed int32 container.
        data = array.array("i")
        for i in range(0, len(raw), 3):
            b0 = raw[i]
            b1 = raw[i + 1]
            b2 = raw[i + 2]
            v = b0 | (b1 << 8) | (b2 << 16)
            if v & 0x800000:
                v -= 1 << 24
            data.append(v)
        scale = 8388608.0
    else:
        data = array.array("i")
        data.frombytes(raw)
        scale = 2147483648.0

    if channels == 1:
        mono = [x / scale for x in data]
    else:
        mono = []
        for i in range(0, len(data), channels):
            s = 0.0
            for c in range(channels):
                s += data[i + c] / scale
            mono.append(s / channels)

    return normalize(mono), sr


def onset_envelope(sig: List[float], win: int = 64) -> List[float]:
    if not sig:
        return []
    env = [abs(x) for x in sig]
    if win <= 1:
        return env
    out: List[float] = [0.0] * len(env)
    acc = 0.0
    for i, v in enumerate(env):
        acc += v
        if i >= win:
            acc -= env[i - win]
        out[i] = acc / min(i + 1, win)
    return out


def attack_ms(sig: List[float], sr: int) -> float:
    env = onset_envelope(sig, win=max(1, sr // 2000))
    if not env:
        return 0.0
    peak = max(env)
    if peak < EPS:
        return 0.0
    t10 = next((i for i, v in enumerate(env) if v >= 0.1 * peak), 0)
    t90 = next((i for i, v in enumerate(env) if v >= 0.9 * peak), t10)
    return 1000.0 * max(0, (t90 - t10) / sr)


def t60_ms(sig: List[float], sr: int) -> float:
    if not sig:
        return 0.0
    energy = [x * x for x in sig]
    # Schroeder backward integration.
    integ = [0.0] * len(energy)
    acc = 0.0
    for i in range(len(energy) - 1, -1, -1):
        acc += energy[i]
        integ[i] = acc
    ref = integ[0]
    if ref < EPS:
        return 0.0
    db = [10.0 * math.log10(max(x / ref, EPS)) for x in integ]
    i5 = next((i for i, v in enumerate(db) if v <= -5.0), None)
    i35 = next((i for i, v in enumerate(db) if v <= -35.0), None)
    if i5 is None or i35 is None or i35 <= i5:
        return 0.0
    t5 = i5 / sr
    t35 = i35 / sr
    # Extrapolate RT60 from -5..-35 slope.
    return (60.0 / 30.0) * (t35 - t5) * 1000.0


def decimate(sig: List[float], factor: int) -> List[float]:
    if factor <= 1:
        return sig
    if not sig:
        return []
    # Simple anti-alias prefilter: moving average over each decimation block.
    # Keeps implementation dependency-free while reducing high-frequency foldback.
    out: List[float] = []
    for i in range(0, len(sig), factor):
        chunk = sig[i : i + factor]
        if not chunk:
            continue
        out.append(sum(chunk) / len(chunk))
    return out


def stft_mag(sig: List[float], n_fft: int, hop: int, max_frames: int = 80) -> List[List[float]]:
    w = hann(n_fft)
    out: List[List[float]] = []
    if len(sig) < n_fft:
        pad = sig + [0.0] * (n_fft - len(sig))
        sig = pad
    frames = 0
    for start in range(0, len(sig) - n_fft + 1, hop):
        frame = [sig[start + i] * w[i] for i in range(n_fft)]
        bins = []
        for k in range(n_fft // 2 + 1):
            re = 0.0
            im = 0.0
            for n, x in enumerate(frame):
                ph = -2.0 * math.pi * k * n / n_fft
                re += x * math.cos(ph)
                im += x * math.sin(ph)
            bins.append(math.sqrt(re * re + im * im))
        out.append(bins)
        frames += 1
        if frames >= max_frames:
            break
    return out


def spectral_centroid(mag: List[float], sr: int, n_fft: int) -> float:
    num = 0.0
    den = 0.0
    for k, m in enumerate(mag):
        f = sr * k / n_fft
        num += f * m
        den += m
    return num / max(den, EPS)


def spectral_flatness(mag: List[float]) -> float:
    if not mag:
        return 0.0
    g = math.exp(sum(math.log(max(x, EPS)) for x in mag) / len(mag))
    a = sum(mag) / len(mag)
    return g / max(a, EPS)


def spectral_rolloff(mag: List[float], sr: int, n_fft: int, pct: float = 0.85) -> float:
    total = sum(mag)
    thr = total * pct
    acc = 0.0
    for k, m in enumerate(mag):
        acc += m
        if acc >= thr:
            return sr * k / n_fft
    return sr / 2.0


def spectral_flux(spec: List[List[float]]) -> float:
    if len(spec) < 2:
        return 0.0
    vals = []
    for i in range(1, len(spec)):
        s = 0.0
        a = spec[i - 1]
        b = spec[i]
        for x, y in zip(a, b):
            d = y - x
            if d > 0:
                s += d
        vals.append(s)
    return sum(vals) / max(len(vals), 1)


def autocorr_f0(sig: List[float], sr: int, fmin: float = 50.0, fmax: float = 4000.0) -> float:
    if not sig:
        return 0.0
    window = sig[: min(len(sig), int(0.35 * sr))]
    if not window:
        return 0.0
    lo = int(sr / fmax)
    hi = int(sr / fmin)
    hi = min(hi, len(window) // 2)
    if hi <= lo:
        return 0.0
    best_lag = lo
    best = -1.0
    for lag in range(lo, hi):
        s = 0.0
        for i in range(len(window) - lag):
            s += window[i] * window[i + lag]
        if s > best:
            best = s
            best_lag = lag
    if best_lag <= 0:
        return 0.0
    return sr / best_lag


def inharmonicity_ratio(sig: List[float], sr: int, f0: float, n_partials: int = 6) -> float:
    if f0 <= 0:
        return 0.0
    n_fft = 2048
    spec = stft_mag(sig[: min(len(sig), sr)], n_fft=n_fft, hop=n_fft, max_frames=1)
    if not spec:
        return 0.0
    mag = spec[0]
    df = sr / n_fft
    devs = []
    for k in range(2, n_partials + 1):
        target = k * f0
        if target >= sr / 2:
            break
        center = int(target / df)
        lo = max(1, center - 4)
        hi = min(len(mag) - 1, center + 5)
        peak_idx = max(range(lo, hi), key=lambda i: mag[i])
        actual = peak_idx * df
        devs.append(abs(actual - target) / max(target, EPS))
    if not devs:
        return 0.0
    return sum(devs) / len(devs)


def mrstft_log_distance(a: List[float], b: List[float], fft_sizes=(128, 256)) -> float:
    dist = 0.0
    count = 0
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    a = a[:n]
    b = b[:n]
    for n_fft in fft_sizes:
        hop = max(1, n_fft // 4)
        sa = stft_mag(a, n_fft, hop, max_frames=24)
        sb = stft_mag(b, n_fft, hop, max_frames=24)
        m = min(len(sa), len(sb))
        if m == 0:
            continue
        for i in range(m):
            for xa, xb in zip(sa[i], sb[i]):
                da = math.log1p(xa)
                db = math.log1p(xb)
                dist += abs(da - db)
                count += 1
    return dist / max(count, 1)


def resample_linear(sig: List[float], factor: float) -> List[float]:
    """Simple linear resampler.
    factor > 1.0 expands signal in time (lowers pitch),
    factor < 1.0 compresses signal in time (raises pitch).
    """
    if not sig:
        return []
    if factor <= 0:
        return sig[:]
    out_len = max(1, int(len(sig) * factor))
    out = [0.0] * out_len
    for i in range(out_len):
        x = i / factor
        x0 = int(x)
        x1 = min(x0 + 1, len(sig) - 1)
        t = x - x0
        out[i] = sig[x0] * (1.0 - t) + sig[x1] * t
    return out


@dataclass
class FeatureSet:
    f0_hz: float
    attack_ms: float
    t60_ms: float
    centroid_hz: float
    rolloff_hz: float
    flatness: float
    flux: float
    inharm: float


def extract_features(sig: List[float], sr: int) -> FeatureSet:
    # Fast-path for environments without numpy/librosa:
    # limit duration and downsample to keep naive DFT feasible.
    sig = sig[: min(len(sig), int(0.8 * sr))]
    if sr > 8000:
        factor = max(1, sr // 8000)
        sig = decimate(sig, factor)
        sr = sr // factor

    n_fft = 256
    hop = 64
    spec = stft_mag(sig, n_fft=n_fft, hop=hop, max_frames=80)
    if spec:
        centroids = [spectral_centroid(fr, sr, n_fft) for fr in spec]
        rolls = [spectral_rolloff(fr, sr, n_fft) for fr in spec]
        flats = [spectral_flatness(fr) for fr in spec]
        centroid = sum(centroids) / len(centroids)
        rolloff = sum(rolls) / len(rolls)
        flat = sum(flats) / len(flats)
        flux = spectral_flux(spec)
    else:
        centroid = rolloff = flat = flux = 0.0

    f0 = autocorr_f0(sig, sr)
    inh = inharmonicity_ratio(sig, sr, f0)

    return FeatureSet(
        f0_hz=f0,
        attack_ms=attack_ms(sig, sr),
        t60_ms=t60_ms(sig, sr),
        centroid_hz=centroid,
        rolloff_hz=rolloff,
        flatness=flat,
        flux=flux,
        inharm=inh,
    )


def pct_diff(a: float, b: float) -> float:
    den = max(abs(a), abs(b), EPS)
    return 100.0 * abs(a - b) / den


def compare_pair(ref_path: Path, ren_path: Path, auto_note_align: bool = False) -> Dict[str, object]:
    ref_sig, ref_sr = read_wav_mono(ref_path)
    ren_sig, ren_sr = read_wav_mono(ren_path)
    if ref_sr != ren_sr:
        # Lightweight fallback: compare at min duration only, no resample.
        pass
    n = min(len(ref_sig), len(ren_sig))
    ref_sig = ref_sig[:n]
    ren_sig = ren_sig[:n]

    ref_f = extract_features(ref_sig, ref_sr)
    ren_f = extract_features(ren_sig, ren_sr)

    # Pitch-normalized descriptors (robust when sample note != rendered note).
    ref_centroid_n = ref_f.centroid_hz / max(ref_f.f0_hz, EPS)
    ren_centroid_n = ren_f.centroid_hz / max(ren_f.f0_hz, EPS)
    ref_rolloff_n = ref_f.rolloff_hz / max(ref_f.f0_hz, EPS)
    ren_rolloff_n = ren_f.rolloff_hz / max(ren_f.f0_hz, EPS)
    f0_ratio = ren_f.f0_hz / max(ref_f.f0_hz, EPS)
    note_offset_semitones = 12.0 * math.log2(max(f0_ratio, EPS))

    raw_mrstft = mrstft_log_distance(ref_sig, ren_sig)
    aligned_mrstft = raw_mrstft
    if auto_note_align and ref_f.f0_hz > 0 and ren_f.f0_hz > 0:
        ratio = ren_f.f0_hz / max(ref_f.f0_hz, EPS)
        # sanity clamp for pathological f0 estimates
        if 0.25 <= ratio <= 4.0:
            ren_aligned = resample_linear(ren_sig, ratio)
            n2 = min(len(ref_sig), len(ren_aligned))
            aligned_mrstft = mrstft_log_distance(ref_sig[:n2], ren_aligned[:n2])

    metrics = {
        "f0_pct": pct_diff(ref_f.f0_hz, ren_f.f0_hz),
        "f0_ratio": f0_ratio,
        "note_offset_semitones": note_offset_semitones,
        "attack_pct": pct_diff(ref_f.attack_ms, ren_f.attack_ms),
        "t60_pct": pct_diff(ref_f.t60_ms, ren_f.t60_ms),
        "centroid_pct": pct_diff(ref_f.centroid_hz, ren_f.centroid_hz),
        "centroid_pitchnorm_pct": pct_diff(ref_centroid_n, ren_centroid_n),
        "rolloff_pct": pct_diff(ref_f.rolloff_hz, ren_f.rolloff_hz),
        "rolloff_pitchnorm_pct": pct_diff(ref_rolloff_n, ren_rolloff_n),
        "flatness_pct": pct_diff(ref_f.flatness, ren_f.flatness),
        "flux_pct": pct_diff(ref_f.flux, ren_f.flux),
        "inharm_pct": pct_diff(ref_f.inharm, ren_f.inharm),
        "mrstft_log_l1": raw_mrstft,
        "note_aligned_mrstft_log_l1": aligned_mrstft,
    }

    # Weighted scalar score; lower is better.
    centroid_term = min(metrics["centroid_pct"], metrics["centroid_pitchnorm_pct"])
    rolloff_term = min(metrics["rolloff_pct"], metrics["rolloff_pitchnorm_pct"])
    mrstft_term = min(metrics["mrstft_log_l1"], metrics["note_aligned_mrstft_log_l1"])
    score = (
        0.16 * metrics["f0_pct"]
        + 0.14 * metrics["attack_pct"]
        + 0.18 * metrics["t60_pct"]
        + 0.16 * centroid_term
        + 0.10 * rolloff_term
        + 0.08 * metrics["flatness_pct"]
        + 0.08 * metrics["flux_pct"]
        + 0.10 * metrics["inharm_pct"]
        + 8.0 * mrstft_term
    )

    return {
        "reference": str(ref_path),
        "rendered": str(ren_path),
        "ref_features": ref_f.__dict__,
        "ren_features": ren_f.__dict__,
        "metrics": metrics,
        "score": score,
    }


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Pre-HW time/frequency analysis of rendered vs reference WAV files")
    p.add_argument("--reference", type=Path, help="Reference WAV", required=False)
    p.add_argument("--rendered", type=Path, help="Rendered WAV", required=False)
    p.add_argument("--pair", action="append", default=[], help="Pair format: ref.wav::rendered.wav")
    p.add_argument("--output", type=Path, default=Path("pre_hw_report.json"), help="Output JSON path")
    p.add_argument("--auto-note-align", action="store_true", help="Use pitch-aligned MR-STFT for note-mismatched comparisons")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    pairs: List[Tuple[Path, Path]] = []

    if args.reference and args.rendered:
        pairs.append((args.reference, args.rendered))

    for raw in args.pair:
        if "::" not in raw:
            raise SystemExit(f"Invalid --pair value (need ref::rendered): {raw}")
        a, b = raw.split("::", 1)
        pairs.append((Path(a), Path(b)))

    if not pairs:
        raise SystemExit("Provide --reference/--rendered or one/more --pair arguments")

    report = []
    for ref, ren in pairs:
        if not ref.exists():
            raise SystemExit(f"Reference file missing: {ref}")
        if not ren.exists():
            raise SystemExit(f"Rendered file missing: {ren}")
        report.append(compare_pair(ref, ren, auto_note_align=args.auto_note_align))

    summary = {
        "pairs": len(report),
        "mean_score": sum(r["score"] for r in report) / max(len(report), 1),
        "results": sorted(report, key=lambda r: r["score"]),
    }

    args.output.write_text(json.dumps(summary, indent=2))
    print(f"Wrote {args.output} (pairs={summary['pairs']}, mean_score={summary['mean_score']:.3f})")
    for r in summary["results"]:
        print(f"  score={r['score']:.3f}  ref={Path(r['reference']).name}  ren={Path(r['rendered']).name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
