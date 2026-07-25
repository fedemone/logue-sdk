#!/usr/bin/env python3
"""
Detailed time-domain and spectral comparison for 4 presets against reference samples.
Uses numpy/scipy where available for speed; falls back to pure stdlib otherwise.
"""

import wave
import struct
import math
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly
from math import gcd

# ─────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────

def load_wav(path):
    """Returns (samples_float_ndarray, sample_rate)."""
    with wave.open(path, 'rb') as wf:
        n_channels = wf.getnchannels()
        sampwidth  = wf.getsampwidth()
        framerate  = wf.getframerate()
        n_frames   = wf.getnframes()
        raw        = wf.readframes(n_frames)

    if sampwidth == 2:
        samples = np.frombuffer(raw, dtype='<i2').astype(np.float64)
        scale   = 32768.0
    elif sampwidth == 3:
        # 24-bit manual unpack
        arr = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        i32 = arr[:, 0].astype(np.int32) | (arr[:, 1].astype(np.int32) << 8) | (arr[:, 2].astype(np.int32) << 16)
        i32[i32 >= (1 << 23)] -= (1 << 24)
        samples = i32.astype(np.float64)
        scale   = 8388608.0
    elif sampwidth == 1:
        samples = np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0
        scale   = 128.0
    else:
        raise ValueError(f"Unsupported sample width {sampwidth}")

    # Mix to mono
    if n_channels == 2:
        samples = samples.reshape(-1, 2).mean(axis=1)

    return samples / scale, framerate


def resample_to(samples, orig_sr, target_sr):
    """Resample using scipy resample_poly for quality."""
    if orig_sr == target_sr:
        return samples.copy()
    g = gcd(int(orig_sr), int(target_sr))
    up   = target_sr // g
    down = orig_sr   // g
    return resample_poly(samples, up, down).astype(np.float64)


def pitch_shift_cents(samples, cents):
    """Pitch-shift by `cents` cents using linear resampling.
    Positive cents = raise pitch (reference pitched up to match synth).
    """
    if cents == 0:
        return samples.copy()
    # factor > 1 means higher pitch
    factor = 2.0 ** (cents / 1200.0)
    # To pitch UP: compress time (read faster), so we resample from
    # virtual_sr = orig_sr * factor to orig_sr. That means up/down = 1/factor
    # We keep output length the same as input.
    n   = len(samples)
    # New index positions in original
    pos = np.arange(n, dtype=np.float64) / factor
    lo  = np.floor(pos).astype(np.int64)
    hi  = lo + 1
    frac = pos - lo
    lo   = np.clip(lo, 0, n - 1)
    hi   = np.clip(hi, 0, n - 1)
    return samples[lo] * (1.0 - frac) + samples[hi] * frac


def find_onset(samples, threshold=0.05):
    """First sample index exceeding threshold * peak."""
    peak = np.max(np.abs(samples))
    thr  = peak * threshold
    idx  = np.argmax(np.abs(samples) >= thr)
    return int(idx)


def normalize_peak(samples):
    """Scale so peak amplitude = 1.0."""
    peak = np.max(np.abs(samples))
    if peak == 0:
        return samples.copy()
    return samples / peak


def cross_correlate_lag(ref, synth, max_lag_samples, window_samples):
    """Find lag in [-max_lag, +max_lag] that maximises cross-correlation
    using numpy (fast)."""
    n = min(len(ref), len(synth), window_samples)
    r = ref[:n]
    s = synth[:n]
    # full cross-correlation
    full = np.correlate(r, s, mode='full')  # length 2n-1, centre at n-1
    # lags from -(n-1) to +(n-1)
    centre = n - 1
    lo_idx = centre - max_lag_samples
    hi_idx = centre + max_lag_samples + 1
    lo_idx = max(lo_idx, 0)
    hi_idx = min(hi_idx, len(full))
    sub    = full[lo_idx:hi_idx]
    best   = np.argmax(sub) + lo_idx - centre
    return int(best)


def apply_lag(samples, lag, total_len):
    """Shift samples by lag (positive = delay synth), return array of total_len."""
    out = np.zeros(total_len, dtype=np.float64)
    if lag >= 0:
        src_len = min(len(samples), total_len - lag)
        out[lag:lag + src_len] = samples[:src_len]
    else:
        src_start = -lag
        src_len   = min(len(samples) - src_start, total_len)
        if src_len > 0:
            out[:src_len] = samples[src_start:src_start + src_len]
    return out


def per_sample_error(ref, synth, n_samples):
    n    = min(len(ref), len(synth), n_samples)
    errs = np.abs(ref[:n] - synth[:n])
    mean_e = float(np.mean(errs)) * 100.0
    rms_e  = float(np.sqrt(np.mean(errs**2))) * 100.0
    max_e  = float(np.max(errs)) * 100.0
    return mean_e, rms_e, max_e


def rms_windows(samples, sr, window_ms=10):
    """Return arrays (t_ms, rms) with 10ms windows."""
    win = int(sr * window_ms / 1000)
    if win == 0:
        return np.array([]), np.array([])
    n_windows = len(samples) // win
    chunks = samples[:n_windows * win].reshape(n_windows, win)
    rms = np.sqrt(np.mean(chunks**2, axis=1))
    t   = (np.arange(n_windows) + 0.5) * window_ms   # centre of each window in ms
    return t, rms


def t60_from_envelope(t_ms, rms, peak_rms):
    """Return T60 in seconds."""
    target = peak_rms * (10.0 ** (-60.0 / 20.0))
    idx = np.argmax(rms <= target)
    if rms[idx] > target:
        return None  # never reaches -60dB
    return float(t_ms[idx]) / 1000.0


def rms_at_time(t_ms, rms, query_ms):
    """Interpolated RMS at query_ms."""
    if len(t_ms) == 0:
        return 0.0
    idx = np.searchsorted(t_ms, query_ms)
    if idx == 0:
        return float(rms[0])
    if idx >= len(t_ms):
        return float(rms[-1])
    t0, r0 = t_ms[idx-1], rms[idx-1]
    t1, r1 = t_ms[idx],   rms[idx]
    frac = (query_ms - t0) / (t1 - t0)
    return float(r0 + frac * (r1 - r0))


def spectral_analysis(samples, sr, n_fft=8192):
    """FFT-based spectral analysis. Returns (mag, freqs, f0, f0_bin, centroid)."""
    n = min(len(samples), n_fft)
    x = np.zeros(n_fft)
    x[:n] = samples[:n]
    # Hann window
    window = np.hanning(n_fft)
    xw = x * window
    mag = np.abs(np.fft.rfft(xw))
    freqs = np.fft.rfftfreq(n_fft, d=1.0/sr)

    # F0: ignore DC (bin 0)
    f0_bin = int(np.argmax(mag[1:]) + 1)
    f0 = float(freqs[f0_bin])

    # Spectral centroid
    total = np.sum(mag)
    centroid = float(np.dot(freqs, mag) / total) if total > 0 else 0.0

    return mag, freqs, f0, f0_bin, centroid


def harmonic_magnitudes(mag, freqs, f0_bin, n_harmonics=5):
    """Return magnitude at each harmonic bin (1..n_harmonics) interpolated."""
    result = []
    for h in range(1, n_harmonics + 1):
        k = f0_bin * h
        if k < len(mag):
            result.append(float(mag[k]))
        else:
            result.append(0.0)
    return result


# ─────────────────────────────────────────────
# Dkay suggestions
# ─────────────────────────────────────────────

def t60_theory(dkay, f0, sr=48000):
    g = 0.85 + (dkay / 200.0) * 0.149
    N = sr / f0
    if g <= 0 or g >= 1:
        return None
    return -3.0 * N / (sr * math.log10(g))


def invert_dkay_for_t60(t60_target, f0, sr=48000):
    """Binary search Dkay in [0, 200] -> target T60."""
    lo, hi = 0.0, 200.0
    for _ in range(80):
        mid = (lo + hi) / 2.0
        t   = t60_theory(mid, f0, sr)
        if t is None:
            break
        if t < t60_target:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2.0


# ─────────────────────────────────────────────
# Main analysis per preset
# ─────────────────────────────────────────────

def analyse_preset(name, synth_path, ref_path, cents_offset, params):
    SR = 48000
    print("=" * 70)
    print(f"PRESET: {name}")
    print("=" * 70)

    # A) Load
    synth_raw, synth_sr = load_wav(synth_path)
    ref_raw,   ref_sr   = load_wav(ref_path)
    print(f"  Synth : {len(synth_raw)} samples @ {synth_sr} Hz  ({len(synth_raw)/synth_sr:.3f}s)")
    print(f"  Ref   : {len(ref_raw)} samples @ {ref_sr} Hz  ({len(ref_raw)/ref_sr:.3f}s)")

    # Resample reference to 48 kHz if needed
    if ref_sr != SR:
        ref_raw = resample_to(ref_raw, ref_sr, SR)
        print(f"  Ref resampled to {SR} Hz -> {len(ref_raw)} samples ({len(ref_raw)/SR:.3f}s)")

    # B) Pitch-shift reference by cents_offset
    ref_shifted = pitch_shift_cents(ref_raw, cents_offset)
    print(f"  Ref pitch-shifted by {cents_offset:+d} cents (factor={2**(cents_offset/1200):.6f})")

    # C) Onset detection
    synth_onset = find_onset(synth_raw)
    ref_onset   = find_onset(ref_shifted)
    synth_trimmed = synth_raw[synth_onset:]
    ref_trimmed   = ref_shifted[ref_onset:]
    print(f"  Synth onset : sample {synth_onset}  ({synth_onset/SR*1000:.2f} ms)")
    print(f"  Ref   onset : sample {ref_onset}  ({ref_onset/SR*1000:.2f} ms)")

    # D) Amplitude normalization
    synth_norm = normalize_peak(synth_trimmed)
    ref_norm   = normalize_peak(ref_trimmed)

    # E) Cross-correlation alignment (search ±500 samples = ±10.4ms at 48kHz)
    max_lag     = 500
    n_200ms     = int(0.200 * SR)   # 9600
    lag = cross_correlate_lag(ref_norm, synth_norm, max_lag, n_200ms)
    print(f"  Cross-correlation lag: {lag} samples ({lag/SR*1000:.3f} ms)  [search ±{max_lag}]")

    total_len     = max(len(ref_norm), len(synth_norm))
    synth_aligned = apply_lag(synth_norm, lag, total_len)

    # F) Per-sample error (first 200ms)
    mean_e, rms_e, max_e = per_sample_error(ref_norm, synth_aligned, n_200ms)
    print(f"\n--- F) Per-sample error (first 200ms after alignment) ---")
    print(f"  Mean absolute error : {mean_e:.4f}%")
    print(f"  RMS error           : {rms_e:.4f}%")
    print(f"  Max error           : {max_e:.4f}%")

    # G) Energy envelope (10ms windows, up to 3s)
    n_3s = int(3.0 * SR)
    t_s, rms_s = rms_windows(synth_aligned[:n_3s], SR, 10)
    t_r, rms_r = rms_windows(ref_norm[:n_3s],      SR, 10)

    peak_rms_s = float(np.max(rms_s)) if len(rms_s) > 0 else 1e-12
    peak_rms_r = float(np.max(rms_r)) if len(rms_r) > 0 else 1e-12

    print(f"\n--- G) Energy envelope (10ms RMS windows, up to 3s) ---")
    checkpoints_ms = [50, 100, 200, 500, 1000]
    for t_ms in checkpoints_ms:
        rs = rms_at_time(t_s, rms_s, t_ms)
        rr = rms_at_time(t_r, rms_r, t_ms)
        ratio = rs / rr if rr > 1e-12 else float('inf')
        rs_db = 20*math.log10(rs) if rs > 1e-12 else -999
        rr_db = 20*math.log10(rr) if rr > 1e-12 else -999
        print(f"  t={t_ms:5d}ms  synth_rms={rs:.5f} ({rs_db:+.1f}dBFS)  ref_rms={rr:.5f} ({rr_db:+.1f}dBFS)  ratio={ratio:.4f}")

    t60_s = t60_from_envelope(t_s, rms_s, peak_rms_s) if len(rms_s) > 0 else None
    t60_r = t60_from_envelope(t_r, rms_r, peak_rms_r) if len(rms_r) > 0 else None

    t60s_str = f"{t60_s:.3f}s" if t60_s is not None else ">3s (not reached)"
    t60r_str = f"{t60_r:.3f}s" if t60_r is not None else ">3s (not reached)"
    print(f"  T60 synth : {t60s_str}")
    print(f"  T60 ref   : {t60r_str}")

    if t60_s is not None and t60_r is not None:
        t60_ratio = t60_s / t60_r
        print(f"  T60 ratio (synth/ref): {t60_ratio:.4f}")
    elif t60_s is None and t60_r is None:
        t60_ratio = 1.0   # both long, treat as matched
        print(f"  T60 ratio: both exceed 3s — treating as matched (ratio=1.0)")
    elif t60_s is None:
        t60_ratio = 999.0   # synth way longer
        print(f"  T60 ratio: synth exceeds 3s, ref={t60_r:.3f}s → ratio >> 1")
    else:
        t60_ratio = 0.001   # ref way longer
        print(f"  T60 ratio: ref exceeds 3s, synth={t60_s:.3f}s → ratio << 1")

    # H) Spectral comparison
    n_fft = 8192
    print(f"\n--- H) Spectral comparison (FFT, n_fft={n_fft}, first {n_fft} samples) ---")

    mag_s, freqs_s, f0_s, k0_s, sc_s = spectral_analysis(synth_aligned, SR, n_fft)
    mag_r, freqs_r, f0_r, k0_r, sc_r = spectral_analysis(ref_norm,      SR, n_fft)

    harms_s = harmonic_magnitudes(mag_s, freqs_s, k0_s, 5)
    harms_r = harmonic_magnitudes(mag_r, freqs_r, k0_s, 5)  # same bins for comparison

    print(f"  F0 synth        : {f0_s:.2f} Hz  (bin {k0_s})")
    print(f"  F0 ref          : {f0_r:.2f} Hz  (bin {k0_r})")
    print(f"  Centroid synth  : {sc_s:.2f} Hz")
    print(f"  Centroid ref    : {sc_r:.2f} Hz")
    sc_ratio = sc_s / sc_r if sc_r > 0 else float('inf')
    print(f"  Centroid ratio  (synth/ref): {sc_ratio:.4f}")

    print(f"  Harmonic magnitudes (at F0 synth bins × h, h=1..5):")
    print(f"    {'H':>2}  {'Freq':>8}  {'Synth mag':>10}  {'Ref mag':>10}  {'Ratio':>8}")
    for h in range(5):
        freq_h = f0_s * (h + 1)
        r = harms_s[h] / harms_r[h] if harms_r[h] > 1e-12 else float('inf')
        print(f"    H{h+1}  {freq_h:8.2f}  {harms_s[h]:10.4f}  {harms_r[h]:10.4f}  {r:8.4f}")

    # I) Parameter recommendations
    print(f"\n--- I) Parameter change suggestions ---")
    Dkay     = params['Dkay']
    Mterl    = params['Mterl']
    MlltStif = params['MlltStif']
    f0_use   = f0_s if f0_s > 20 else 100.0

    any_change = False

    # --- T60 / Dkay ---
    if t60_ratio < 0.5:
        any_change = True
        # synth decays faster than ref → need MORE decay → higher Dkay
        t60_target = t60_r if t60_r is not None else 3.0
        new_dkay = invert_dkay_for_t60(t60_target, f0_use)
        new_dkay_int = int(round(min(max(new_dkay, 0), 200)))
        th = t60_theory(new_dkay_int, f0_use)
        print(f"  [DECAY] T60 ratio {t60_ratio:.4f} < 0.5 → synth decays TOO FAST.")
        print(f"          Increase Dkay: {Dkay} → {new_dkay_int}  (theory T60 at new Dkay: {th:.3f}s, target: {t60_target:.3f}s)")
    elif t60_ratio > 2.0:
        any_change = True
        # synth sustains longer → need LESS decay → lower Dkay
        t60_target = t60_r if t60_r is not None else 3.0
        new_dkay = invert_dkay_for_t60(t60_target, f0_use)
        new_dkay_int = int(round(min(max(new_dkay, 0), 200)))
        th = t60_theory(new_dkay_int, f0_use)
        print(f"  [DECAY] T60 ratio {t60_ratio:.4f} > 2.0 → synth decays TOO SLOW.")
        print(f"          Decrease Dkay: {Dkay} → {new_dkay_int}  (theory T60 at new Dkay: {th:.3f}s, target: {t60_target:.3f}s)")
    else:
        th = t60_theory(Dkay, f0_use)
        print(f"  [DECAY] T60 ratio {t60_ratio:.4f} ∈ [0.5, 2.0] — Dkay OK  (current={Dkay}, theory T60={th:.3f}s)")

    # --- Spectral brightness / centroid ---
    if sc_ratio > 1.5:
        any_change = True
        print(f"  [TIMBRE] Centroid ratio {sc_ratio:.4f} > 1.5 → synth TOO BRIGHT.")
        print(f"           Decrease MlltStif by 100: {MlltStif} → {MlltStif-100}")
        print(f"           OR increase Mterl:        {Mterl} → {Mterl+4}")
    elif sc_ratio < 0.67:
        any_change = True
        print(f"  [TIMBRE] Centroid ratio {sc_ratio:.4f} < 0.67 → synth TOO DARK.")
        print(f"           Increase MlltStif by 100: {MlltStif} → {MlltStif+100}")
        print(f"           OR decrease Mterl:        {Mterl} → {Mterl-4}")
    else:
        print(f"  [TIMBRE] Centroid ratio {sc_ratio:.4f} ∈ [0.67, 1.5] — spectral brightness OK")

    if not any_change:
        print(f"  No parameter changes required based on thresholds.")

    print()
    return {
        'f0_synth': f0_s, 'f0_ref': f0_r,
        'sc_synth': sc_s, 'sc_ref': sc_r,
        't60_synth': t60_s, 't60_ref': t60_r,
        't60_ratio': t60_ratio, 'sc_ratio': sc_ratio,
    }


# ─────────────────────────────────────────────
# Preset definitions
# ─────────────────────────────────────────────

BASE = Path(__file__).resolve().parent

presets = [
    {
        'name':       'Clarinet (25)',
        'synth_path': f'{BASE}/rendered/25_Clarinet.wav',
        'ref_path':   f'{BASE}/samples/Clarinet-C-minor.wav',
        'cents':      +6,
        'params':     {'Note':72, 'Dkay':180, 'Mterl':-8, 'MlltStif':10, 'MlltRes':50, 'InHm':9, 'NzMix':12},
    },
    {
        'name':       'StelPan (17)',
        'synth_path': f'{BASE}/rendered/17_StelPan.wav',
        'ref_path':   f'{BASE}/samples/steel-pan-Nova Drum Real C 432.wav',
        'cents':      -33,
        'params':     {'Note':60, 'Dkay':194, 'Mterl':28, 'MlltStif':150, 'MlltRes':600, 'InHm':0, 'NzMix':0},
    },
    {
        'name':       'Koto (10)',
        'synth_path': f'{BASE}/rendered/10_Koto.wav',
        'ref_path':   f'{BASE}/samples/Koto-B5.wav',
        'cents':      -15,
        'params':     {'Note':60, 'Dkay':185, 'Mterl':28, 'MlltStif':395, 'MlltRes':600, 'InHm':3, 'NzMix':0},
    },
    {
        'name':       'TblrBel (4)',
        'synth_path': f'{BASE}/rendered/04_TblrBel.wav',
        'ref_path':   f'{BASE}/samples/tubular-bells.wav',
        'cents':      -149,
        'params':     {'Note':72, 'Dkay':199, 'Mterl':28, 'MlltStif':100, 'MlltRes':900, 'InHm':5, 'NzMix':0},
    },
]

for p in presets:
    analyse_preset(
        name         = p['name'],
        synth_path   = p['synth_path'],
        ref_path     = p['ref_path'],
        cents_offset = p['cents'],
        params       = p['params'],
    )
