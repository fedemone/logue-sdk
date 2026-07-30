#!/usr/bin/env python3
"""
analyze_samples.py — Extract physical-model parameters from reference WAV files.

For each sample it reports:
  • fundamental frequency  → MIDI note   → which preset Note to use
  • T_60 (energy decay)    → Dkay value  (feedback_gain g: T60 = 3N/(fs*|log10 g|))
  • spectral centroid decay → Mterl      (lowpass_coeff: coeff = 0.01 + (Mterl+10)/40*0.99)
  • inharmonicity ratio     → InHm       (ap_coeff = InHm/2000)
  • noise fraction          → NzMix      (0-100)
  • attack time             → MlltStif   (mallet stiffness)

All conversions are the INVERSE of what synth_engine.h computes in setParameter().
"""

import librosa
import numpy as np
import scipy.signal as signal
import os, sys
from pathlib import Path

SAMPLES_DIR = Path(__file__).parent / "samples"
SR_TARGET   = 48000   # Drumlogue sample rate (resample everything to this)

# ── helpers ───────────────────────────────────────────────────────────────────

def hz_to_midi(hz):
    if hz <= 0: return 0
    return 69 + 12 * np.log2(hz / 440.0)

def midi_to_note_name(midi):
    names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
    n = int(round(midi))
    return f"{names[n%12]}{n//12 - 1}({n})"

def t60_to_dkay(t60_s, note_midi):
    """
    Inverse of:  T60 = 3*N / (fs * |log10(g)|)
    where N = fs/f0,  g = 0.85 + (Dkay/200)*0.149
    Clamp to [0,200].
    """
    f0   = 440.0 * 2**((note_midi - 69) / 12.0)
    N    = SR_TARGET / f0
    # |log10(g)| = 3*N / (fs * T60)
    log10g = 3.0 * N / (SR_TARGET * t60_s)
    g    = 10.0 ** (-log10g)
    g    = np.clip(g, 0.85, 0.999)
    dkay = (g - 0.85) / 0.149 * 200.0
    return int(np.clip(round(dkay), 0, 200)), g

def centroid_to_mterl(centroid_hz, f0_hz):
    """
    Spectral centroid as multiple of f0 → lowpass_coeff → Mterl.
    Bright (centroid >> f0) → high coeff → high Mterl.
    Dark  (centroid ≈ f0)   → low  coeff → low  Mterl.
    Heuristic: coeff ≈ centroid_ratio / (centroid_ratio + 10)   (sigmoid-ish)
    coeff = 0.01 + mterl_norm*0.99,  mterl_norm = (Mterl+10)/40
    → Mterl = mterl_norm*40 - 10
    """
    ratio = centroid_hz / max(f0_hz, 1.0)
    # map ratio [1..40] → coeff [0.01..0.99]
    coeff = np.clip(ratio / 40.0, 0.01, 0.99)
    mterl_norm = (coeff - 0.01) / 0.99
    mterl = mterl_norm * 40.0 - 10.0
    return int(np.clip(round(mterl), -10, 30))

def inharmonicity_to_inharm(inh_coefficient):
    """
    Inharmonicity coefficient B (physics) maps roughly to ap_coeff = InHm/2000.
    B = 0        → InHm = 0     (string / membrane)
    B = 0.001    → InHm ~ 2     (marimba bar)
    B = 0.01     → InHm ~ 20    (kalimba tine)
    B = 0.1      → InHm ~ 200   (stiff bar)
    B = 1.0      → InHm = 1999  (tabla / very stiff)
    Using: InHm = round(B * 2000) clamped.
    """
    return int(np.clip(round(inh_coefficient * 2000), 0, 1999))

def estimate_t60(y, sr, db_drop=60):
    """
    Estimate T_60 from energy envelope.
    Uses the Schroeder backward integration method (ISO 3382-style).
    """
    # Energy envelope via Hilbert
    analytic = signal.hilbert(y)
    envelope = np.abs(analytic) ** 2

    # Smooth with 5ms window
    win = int(0.005 * sr)
    if win > 1:
        envelope = np.convolve(envelope, np.ones(win)/win, mode='same')

    # Find peak
    peak_idx = np.argmax(envelope)
    env_after = envelope[peak_idx:]
    if len(env_after) < 10:
        return None

    db = 10 * np.log10(np.maximum(env_after, 1e-12))
    db -= db[0]   # normalise to 0 dB at peak

    # Find where it drops by db_drop
    below = np.where(db <= -db_drop)[0]
    if len(below) == 0:
        # Didn't drop fully — extrapolate from whatever we have
        # Use linear fit on the last 20 dB of available data
        valid = np.where(db <= -5)[0]
        if len(valid) < 2:
            return None
        x = valid / sr
        y_db = db[valid]
        slope, intercept = np.polyfit(x, y_db, 1)
        if slope >= 0:
            return None
        return -db_drop / slope
    return below[0] / sr

def estimate_fundamental(y, sr, fmin=50, fmax=5000):
    """
    Estimate dominant fundamental using autocorrelation (YIN-like),
    then refine with parabolic interpolation.
    """
    # Use first 0.3 s for pitch
    chunk = y[:int(0.3 * sr)]
    f0s = librosa.yin(chunk, fmin=fmin, fmax=fmax, sr=sr,
                      frame_length=2048, hop_length=512)
    # Median of voiced frames (exclude unvoiced ~NaN-like outliers)
    f0s = f0s[(f0s > fmin) & (f0s < fmax)]
    if len(f0s) == 0:
        return None
    return float(np.median(f0s))

def estimate_inharmonicity(y, sr, f0, n_partials=8):
    """
    Fit the partial series to f_k = k*f0*sqrt(1 + B*k^2).
    Returns B (inharmonicity coefficient).
    """
    if f0 is None or f0 <= 0:
        return 0.0

    # FFT of first 0.5 s
    chunk = y[:int(0.5 * sr)]
    N = len(chunk)
    spec = np.abs(np.fft.rfft(chunk * np.hanning(N))) ** 2
    freqs = np.fft.rfftfreq(N, 1.0/sr)

    B_estimates = []
    for k in range(2, n_partials + 1):
        expected_hz = k * f0
        search_lo = int((expected_hz * 0.85) / (sr / N))
        search_hi = int((expected_hz * 1.15) / (sr / N))
        search_lo = max(1, min(search_lo, len(spec)-2))
        search_hi = max(search_lo+1, min(search_hi, len(spec)-1))
        if search_lo >= search_hi:
            continue
        peak_bin = search_lo + np.argmax(spec[search_lo:search_hi])
        actual_hz = freqs[peak_bin]
        if actual_hz <= 0:
            continue
        # f_k = k*f0*sqrt(1 + B*k^2)  →  B = ((f_k/(k*f0))^2 - 1) / k^2
        ratio = actual_hz / (k * f0)
        b = (ratio**2 - 1.0) / (k**2)
        if b > 0:
            B_estimates.append(b)

    return float(np.median(B_estimates)) if B_estimates else 0.0

def estimate_noise_fraction(y, sr, f0):
    """
    Ratio of aperiodic (noise) energy to total energy in first 0.1 s.
    Uses harmonic/residual decomposition via HPSS.
    """
    chunk = y[:int(0.1 * sr)]
    harmonic, percussive = librosa.effects.hpss(chunk)
    noise_frac = np.sum(percussive**2) / max(np.sum(chunk**2), 1e-12)
    return float(np.clip(noise_frac, 0, 1))

def estimate_attack_time(y, sr):
    """10%→90% of peak energy envelope."""
    envelope = np.abs(signal.hilbert(y))
    peak = np.max(envelope)
    t10 = np.argmax(envelope >= 0.10 * peak) / sr
    t90 = np.argmax(envelope >= 0.90 * peak) / sr
    return max(0, t90 - t10)

def noise_frac_to_nzmix(frac):
    return int(np.clip(round(frac * 100), 0, 100))

def attack_to_mllt_stif(attack_s):
    """
    Attack time 0 (instant) → MlltStif=500 (max stiffness)
    Attack time 0.05 s      → MlltStif=50
    """
    stif = int(np.clip(round(500 * (1.0 - attack_s / 0.05)), 10, 500))
    return stif

# ── per-file analysis ─────────────────────────────────────────────────────────

def analyze(wav_path):
    y, sr = librosa.load(str(wav_path), sr=SR_TARGET, mono=True)
    y = y / max(np.max(np.abs(y)), 1e-12)   # normalise

    result = {"file": wav_path.name}

    # Fundamental
    f0 = estimate_fundamental(y, sr)
    result["f0_hz"]   = round(f0, 1) if f0 else None
    result["midi"]    = round(hz_to_midi(f0), 1) if f0 else None
    result["note"]    = midi_to_note_name(hz_to_midi(f0)) if f0 else "?"

    # T_60
    t60 = estimate_t60(y, sr)
    result["t60_s"]   = round(t60, 3) if t60 else None

    # Dkay (requires MIDI note — use rounded midi)
    if t60 and f0:
        midi_int = int(round(hz_to_midi(f0)))
        dkay, g  = t60_to_dkay(t60, midi_int)
        result["dkay"]  = dkay
        result["g"]     = round(g, 4)
    else:
        result["dkay"]  = None
        result["g"]     = None

    # Spectral centroid (average over first 0.3 s, excluding first 10 ms transient)
    skip = int(0.01 * sr)
    chunk = y[skip:skip + int(0.3 * sr)]
    centroids = librosa.feature.spectral_centroid(y=chunk, sr=sr, n_fft=2048, hop_length=512)[0]
    centroid_hz = float(np.mean(centroids))
    result["centroid_hz"] = round(centroid_hz, 0)
    result["mterl"] = centroid_to_mterl(centroid_hz, f0 if f0 else 440)

    # Inharmonicity
    B = estimate_inharmonicity(y, sr, f0)
    result["B_inharm"] = round(B, 6)
    result["inharm"]   = inharmonicity_to_inharm(B)

    # Noise fraction
    nz = estimate_noise_fraction(y, sr, f0)
    result["noise_frac"] = round(nz, 3)
    result["nzmix"]      = noise_frac_to_nzmix(nz)

    # Attack
    atk = estimate_attack_time(y, sr)
    result["attack_s"]   = round(atk, 4)
    result["mllt_stif"]  = attack_to_mllt_stif(atk)

    return result

# ── centroid decay: how fast does brightness drop? → Mterl sign ───────────────

def spectral_centroid_slope(wav_path):
    """
    Compute slope of spectral centroid over time (Hz/s).
    Negative → centroid falling → darkening → negative Mterl (natural instrument).
    """
    y, sr = librosa.load(str(wav_path), sr=SR_TARGET, mono=True)
    y = y / max(np.max(np.abs(y)), 1e-12)
    centroids = librosa.feature.spectral_centroid(y=y, sr=sr, n_fft=2048, hop_length=512)[0]
    hop_s = 512 / sr
    times  = np.arange(len(centroids)) * hop_s
    # Exclude first 20 ms (attack transient) and last 10% (noise floor)
    start  = int(0.02 / hop_s)
    end    = int(len(times) * 0.9)
    if end <= start + 4:
        return 0.0
    slope, _ = np.polyfit(times[start:end], centroids[start:end], 1)
    return float(slope)

# ── main ──────────────────────────────────────────────────────────────────────

def main():
    wavs = sorted(SAMPLES_DIR.glob("*.wav"))
    if not wavs:
        print(f"No WAV files found in {SAMPLES_DIR}")
        sys.exit(1)

    print(f"\n{'='*110}")
    print(f"{'File':<42} {'Note':>7} {'T60':>6} {'Dkay':>4} {'Mterl':>5} {'InHm':>5} {'NzMix':>5} {'MlltSt':>6}  Centroid  Slope(Hz/s)")
    print(f"{'='*110}")

    results = []
    for wav in wavs:
        r = analyze(wav)
        slope = spectral_centroid_slope(wav)

        t60_str  = f"{r['t60_s']:.2f}s" if r['t60_s'] else "  ?"
        dkay_str = str(r['dkay'])        if r['dkay'] is not None else "?"
        note_str = r['note']

        print(f"{r['file']:<42} {note_str:>7} {t60_str:>6} {dkay_str:>4}"
              f" {r['mterl']:>5} {r['inharm']:>5} {r['nzmix']:>5}"
              f" {r['mllt_stif']:>6}  {r['centroid_hz']:>6.0f}Hz  {slope:>+.0f}")

        results.append((r, slope))

    print(f"{'='*110}")
    print()

    # Preset mapping advice
    print("PRESET CALIBRATION RECOMMENDATIONS")
    print("─" * 80)
    preset_map = {
        "marimba":   ("Marimba",  1,  60),
        "kalimba":   ("Kalimba", 16,  72),
        "koto":      ("Koto",    10,  72),
        "gong":      ("Gong",    15,  36),
        "chinese":   ("Gong",    15,  36),
        "tabla":     ("Djambe",   6,  48),
        "djambe":    ("Djambe",   6,  48),
        "taiko":     ("Taiko",    7,  36),
        "timpani":   ("Timpani",  5,  40),
        "triangle":  ("Triangle",20,  84),
        "snare":     ("MrchSnr",  8,  65),
        "marching":  ("MrchSnr",  8,  65),
        "woodblock": ("Wodblk",  12,  76),
        "flute":     ("Flute",   24,  72),
        "clarinet":  ("Clarinet",25,  72),
        "pipe":      ("Cowbell", 19,  67),
    }

    for r, slope in results:
        name_lc = r['file'].lower()
        matched = None
        for key, (preset_name, preset_idx, preset_note) in preset_map.items():
            if key in name_lc:
                matched = (preset_name, preset_idx, preset_note)
                break
        if not matched:
            continue

        preset_name, preset_idx, preset_note = matched
        dkay = r['dkay']
        mterl = r['mterl']
        inharm = r['inharm']
        nzmix = r['nzmix']
        mllt = r['mllt_stif']

        # Mterl sign correction: if centroid is falling, instrument darkens naturally
        # → negative damp in reference → lower Mterl value
        if slope < -200 and mterl > 0:
            mterl = max(mterl - 10, -10)

        t60_str = f"{r['t60_s']:.2f}s" if r['t60_s'] else "?"
        print(f"  [{preset_idx:2d}] {preset_name:<10}  ←  {r['file']}")
        print(f"       Note={r['note']} | T60={t60_str} → Dkay={dkay} | "
              f"Mterl={mterl} | InHm={inharm} | NzMix={nzmix} | MlltStif={mllt}")

    print()
    return results

if __name__ == "__main__":
    main()
