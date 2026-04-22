#!/usr/bin/env python3
"""
Detailed time-domain and spectral analysis of 4 RipplerX presets vs reference samples.
Pure stdlib only (wave, struct, math).
"""

import wave
import struct
import math

# ── helpers ────────────────────────────────────────────────────────────────

def read_wav(path):
    """Return (samples_float, sample_rate, n_channels)."""
    with wave.open(path, 'rb') as wf:
        n_ch   = wf.getnchannels()
        sw     = wf.getsampwidth()
        sr     = wf.getframerate()
        n_fr   = wf.getnframes()
        raw    = wf.readframes(n_fr)

    n_samples = n_fr * n_ch
    if sw == 2:
        fmt = f'<{n_samples}h'
        vals = struct.unpack(fmt, raw)
        scale = 32768.0
    elif sw == 3:
        # 24-bit: unpack manually
        vals = []
        for i in range(n_samples):
            b = raw[i*3:(i+1)*3]
            v = struct.unpack('<i', b + (b'\xff' if b[2] & 0x80 else b'\x00'))[0]
            vals.append(v)
        scale = 8388608.0
    elif sw == 1:
        fmt = f'<{n_samples}B'
        vals = struct.unpack(fmt, raw)
        vals = [v - 128 for v in vals]
        scale = 128.0
    else:
        raise ValueError(f"Unsupported sample width {sw}")

    # Mix to mono if stereo
    if n_ch == 2:
        mono = [(vals[i*2] + vals[i*2+1]) / 2.0 for i in range(n_fr)]
    else:
        mono = list(vals)

    floats = [v / scale for v in mono]
    return floats, sr


def resample_linear(samples, src_sr, dst_sr):
    """Linear interpolation resampling."""
    if src_sr == dst_sr:
        return samples[:]
    ratio = src_sr / dst_sr
    n_out = int(len(samples) * dst_sr / src_sr)
    out = []
    for i in range(n_out):
        pos = i * ratio
        lo  = int(pos)
        hi  = min(lo + 1, len(samples) - 1)
        frac = pos - lo
        out.append(samples[lo] * (1 - frac) + samples[hi] * frac)
    return out


def pitch_shift_by_cents(samples, sr, cents):
    """Pitch-shift using linear resampling (positive cents = shift up)."""
    # To shift pitch UP by C cents: resample as if source rate is higher
    # ratio = 2^(cents/1200)
    ratio = 2.0 ** (cents / 1200.0)
    # New "virtual" sample rate:  we read at ratio * sr to get pitched version at sr
    virtual_sr = sr * ratio
    return resample_linear(samples, virtual_sr, sr)


def find_onset(samples, threshold=0.05):
    """Return index of first sample exceeding threshold * peak."""
    peak = max(abs(s) for s in samples)
    thr  = peak * threshold
    for i, s in enumerate(samples):
        if abs(s) >= thr:
            return i
    return 0


def normalize_peak(samples):
    peak = max(abs(s) for s in samples)
    if peak == 0:
        return samples[:]
    return [s / peak for s in samples]


def dot_product(a, b, n):
    return sum(a[i] * b[i] for i in range(n))


def cross_corr_align(ref, synth, sr, max_lag_ms=200):
    """Find lag (in samples) that maximises dot product in first 200ms."""
    n_window = int(sr * max_lag_ms / 1000)
    max_lag  = int(sr * max_lag_ms / 1000)
    best_lag  = 0
    best_val  = -1e18
    # Only test non-negative lags (synth starts at or after ref)
    for lag in range(max_lag):
        n = min(n_window, len(ref), len(synth) - lag)
        if n <= 0:
            break
        val = dot_product(ref, [synth[lag + i] for i in range(n)], n)
        if val > best_val:
            best_val = val
            best_lag = lag
    return best_lag


def rms_windows(samples, sr, window_ms=10, max_s=3.0):
    """Return list of (time_s, rms) for each window up to max_s."""
    win_sz = int(sr * window_ms / 1000)
    n_max  = int(sr * max_s)
    result = []
    i = 0
    t = 0.0
    step = window_ms / 1000.0
    while i < n_max and i < len(samples):
        chunk = samples[i:i+win_sz]
        if len(chunk) == 0:
            break
        rms = math.sqrt(sum(s*s for s in chunk) / len(chunk))
        result.append((t, rms))
        i += win_sz
        t += step
    return result


def rms_at_time(env, t_s):
    """Interpolate RMS envelope at given time."""
    if not env:
        return 0.0
    # Find closest window
    best = min(env, key=lambda x: abs(x[0] - t_s))
    return best[1]


def compute_t60(env):
    """Return T60: time when RMS drops to 10^(-60/20) = ~0.001 of peak."""
    if not env:
        return float('nan')
    peak_rms = max(r for _, r in env)
    if peak_rms == 0:
        return float('nan')
    thr = peak_rms * (10.0 ** (-60.0 / 20.0))
    # Find first window at peak, then track decline
    peak_t = max(env, key=lambda x: x[1])[0]
    for t, r in env:
        if t > peak_t and r <= thr:
            return t
    # Not reached: return last time
    return env[-1][0]


def dft_magnitude(samples, n):
    """Compute DFT magnitude for first n samples (slow but stdlib-only)."""
    N = min(n, len(samples))
    mag = []
    # Only compute up to N//2 bins
    half = N // 2
    for k in range(half):
        re = 0.0
        im = 0.0
        for j in range(N):
            angle = 2.0 * math.pi * k * j / N
            re += samples[j] * math.cos(angle)
            im -= samples[j] * math.sin(angle)
        mag.append(math.sqrt(re*re + im*im))
    return mag


def fft_magnitude(samples, n):
    """Cooley-Tukey radix-2 FFT (n must be power of 2)."""
    N = n
    # Zero-pad or truncate
    x = [complex(samples[i], 0) if i < len(samples) else complex(0, 0) for i in range(N)]

    # Bit-reversal
    j = 0
    for i in range(1, N):
        bit = N >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            x[i], x[j] = x[j], x[i]

    # FFT
    length = 2
    while length <= N:
        half_len = length // 2
        angle_step = -2.0 * math.pi / length
        wlen = complex(math.cos(angle_step), math.sin(angle_step))
        for i in range(0, N, length):
            w = complex(1, 0)
            for k in range(half_len):
                u = x[i + k]
                v = x[i + k + half_len] * w
                x[i + k]           = u + v
                x[i + k + half_len] = u - v
                w *= wlen
        length <<= 1

    return [abs(x[k]) for k in range(N // 2)]


def next_power2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def spectral_centroid(mag, sr, n_fft):
    """Weighted average frequency."""
    total = sum(mag)
    if total == 0:
        return 0.0
    freq_res = sr / n_fft
    return sum(mag[k] * k * freq_res for k in range(len(mag))) / total


def find_f0(mag, sr, n_fft):
    """Return frequency of dominant bin (ignoring DC)."""
    freq_res = sr / n_fft
    best_k = 1
    best_m = mag[1] if len(mag) > 1 else 0
    for k in range(2, len(mag)):
        if mag[k] > best_m:
            best_m = mag[k]
            best_k = k
    return best_k * freq_res


def harmonic_magnitudes(mag, f0_bin, n_harmonics=5, n_fft=4096):
    """Return magnitudes at harmonics 1..n_harmonics."""
    result = []
    for h in range(1, n_harmonics + 1):
        k = int(round(h * f0_bin))
        if k < len(mag):
            # Take max in ±2 bins to handle slight inaccuracy
            lo = max(0, k - 2)
            hi = min(len(mag) - 1, k + 2)
            result.append(max(mag[lo:hi+1]))
        else:
            result.append(0.0)
    return result


# ── T60 → Dkay inversion ───────────────────────────────────────────────────

def t60_to_dkay(f0_hz, t60_target_s):
    """
    g = 0.85 + (Dkay/200)*0.149
    N = 48000 / f0
    T60 = -3*N / (48000 * log10(g))
    Solve for Dkay given T60 target.
    """
    N = 48000.0 / f0_hz
    # T60 = -3*N / (48000 * log10(g))
    # log10(g) = -3*N / (48000 * T60)
    log10g = -3.0 * N / (48000.0 * t60_target_s)
    g = 10.0 ** log10g
    # g = 0.85 + (Dkay/200)*0.149
    dkay = (g - 0.85) / 0.149 * 200.0
    return dkay, g


# ── per-preset analysis ────────────────────────────────────────────────────

PRESETS = [
    {
        'name':      'Clarinet (25)',
        'render':    '/home/user/logue-sdk/platform/drumlogue/ripplerx/rendered/25_Clarinet.wav',
        'reference': '/home/user/logue-sdk/platform/drumlogue/ripplerx/samples/Clarinet-C-minor.wav',
        'cents':     +6,
        'params':    {'Note': 72, 'Dkay': 180, 'Mterl': -8,  'MlltStif': 10,  'MlltRes': 50,  'InHm': 9, 'NzMix': 12},
    },
    {
        'name':      'StelPan (17)',
        'render':    '/home/user/logue-sdk/platform/drumlogue/ripplerx/rendered/17_StelPan.wav',
        'reference': '/home/user/logue-sdk/platform/drumlogue/ripplerx/samples/steel-pan-Nova Drum Real C 432.wav',
        'cents':     -33,
        'params':    {'Note': 60, 'Dkay': 194, 'Mterl': 28,  'MlltStif': 150, 'MlltRes': 600, 'InHm': 0, 'NzMix': 0},
    },
    {
        'name':      'Koto (10)',
        'render':    '/home/user/logue-sdk/platform/drumlogue/ripplerx/rendered/10_Koto.wav',
        'reference': '/home/user/logue-sdk/platform/drumlogue/ripplerx/samples/Koto-B5.wav',
        'cents':     -15,
        'params':    {'Note': 60, 'Dkay': 185, 'Mterl': 28,  'MlltStif': 395, 'MlltRes': 600, 'InHm': 3, 'NzMix': 0},
    },
    {
        'name':      'TblrBel (4)',
        'render':    '/home/user/logue-sdk/platform/drumlogue/ripplerx/rendered/04_TblrBel.wav',
        'reference': '/home/user/logue-sdk/platform/drumlogue/ripplerx/samples/tubular-bells.wav',
        'cents':     -149,
        'params':    {'Note': 72, 'Dkay': 199, 'Mterl': 28,  'MlltStif': 100, 'MlltRes': 900, 'InHm': 5, 'NzMix': 0},
    },
]

TARGET_SR = 48000
FFT_N     = 4096   # power of 2

print("=" * 72)
print("RIPPLERX PRESET ANALYSIS REPORT")
print("=" * 72)

for preset in PRESETS:
    print(f"\n{'─' * 72}")
    print(f"PRESET: {preset['name']}")
    print(f"{'─' * 72}")

    # ── A: Load ──────────────────────────────────────────────────────────
    synth, synth_sr = read_wav(preset['render'])
    ref,   ref_sr   = read_wav(preset['reference'])
    print(f"[LOAD] Synth: {len(synth)} samples @ {synth_sr} Hz")
    print(f"[LOAD] Ref  : {len(ref)} samples @ {ref_sr} Hz")

    # ── A: Resample ref to 48 kHz ────────────────────────────────────────
    if ref_sr != TARGET_SR:
        ref = resample_linear(ref, ref_sr, TARGET_SR)
        print(f"[RESAMPLE] Ref resampled {ref_sr} → {TARGET_SR} Hz  ({len(ref)} samples)")
    ref_sr = TARGET_SR

    # ── B: Pitch-shift reference ─────────────────────────────────────────
    cents = preset['cents']
    # We want the reference to match synth pitch.
    # If synth is +6¢ above ref, shift ref up by +6¢.
    ref = pitch_shift_by_cents(ref, ref_sr, cents)
    print(f"[PITCH SHIFT] Ref shifted by {cents:+d}¢  → {len(ref)} samples")

    # ── C: Onset detection ───────────────────────────────────────────────
    onset_s = find_onset(synth)
    onset_r = find_onset(ref)
    synth = synth[onset_s:]
    ref   = ref[onset_r:]
    print(f"[ONSET] Synth onset @ sample {onset_s}  ({onset_s/TARGET_SR*1000:.1f} ms)")
    print(f"[ONSET] Ref   onset @ sample {onset_r}  ({onset_r/TARGET_SR*1000:.1f} ms)")

    # ── D: Amplitude normalisation ───────────────────────────────────────
    synth = normalize_peak(synth)
    ref   = normalize_peak(ref)

    # ── E: Cross-correlation alignment ───────────────────────────────────
    # Test synth shifted against ref (ref is anchor)
    lag = cross_corr_align(ref, synth, TARGET_SR, max_lag_ms=200)
    synth_aligned = synth[lag:]
    print(f"[XCORR] Best lag = {lag} samples ({lag/TARGET_SR*1000:.2f} ms)")

    # ── F: Per-sample error (first 200ms after alignment) ────────────────
    n_200ms = int(TARGET_SR * 0.200)
    n_win   = min(n_200ms, len(ref), len(synth_aligned))
    abs_err = [abs(ref[i] - synth_aligned[i]) for i in range(n_win)]
    mean_ae = sum(abs_err) / n_win * 100
    rms_err = math.sqrt(sum(e*e for e in abs_err) / n_win) * 100
    max_err = max(abs_err) * 100
    print(f"\n[ERROR — first 200ms, {n_win} samples]")
    print(f"  Mean Absolute Error : {mean_ae:.3f} %")
    print(f"  RMS Error           : {rms_err:.3f} %")
    print(f"  Max Error           : {max_err:.3f} %")

    # ── G: Energy envelope ───────────────────────────────────────────────
    env_s = rms_windows(synth_aligned, TARGET_SR, window_ms=10, max_s=3.0)
    env_r = rms_windows(ref,           TARGET_SR, window_ms=10, max_s=3.0)

    print(f"\n[ENERGY ENVELOPE — 10ms RMS windows]")
    check_times = [0.05, 0.10, 0.20, 0.50, 1.00]
    for t in check_times:
        rs = rms_at_time(env_s, t)
        rr = rms_at_time(env_r, t)
        ratio = rs / rr if rr > 1e-9 else float('nan')
        rs_db = 20*math.log10(rs) if rs > 1e-9 else -999
        rr_db = 20*math.log10(rr) if rr > 1e-9 else -999
        print(f"  t={t*1000:5.0f}ms  synth_rms={rs:.5f} ({rs_db:+.1f}dB)  ref_rms={rr:.5f} ({rr_db:+.1f}dB)  ratio={ratio:.3f}")

    t60_s = compute_t60(env_s)
    t60_r = compute_t60(env_r)
    t60_ratio = t60_s / t60_r if t60_r > 0 else float('nan')
    print(f"\n  T60 synth = {t60_s:.3f} s")
    print(f"  T60 ref   = {t60_r:.3f} s")
    print(f"  T60 ratio (synth/ref) = {t60_ratio:.3f}")

    # ── H: Spectral comparison ───────────────────────────────────────────
    n_fft_actual = FFT_N  # 4096
    seg_s = synth_aligned[:n_fft_actual]
    seg_r = ref[:n_fft_actual]

    # Apply simple Hann window
    hann = [0.5 - 0.5 * math.cos(2 * math.pi * i / (n_fft_actual - 1)) for i in range(n_fft_actual)]
    seg_s = [seg_s[i] * hann[i] if i < len(seg_s) else 0.0 for i in range(n_fft_actual)]
    seg_r = [seg_r[i] * hann[i] if i < len(seg_r) else 0.0 for i in range(n_fft_actual)]

    print(f"\n[SPECTRAL — FFT size {n_fft_actual}, freq resolution {TARGET_SR/n_fft_actual:.2f} Hz]")
    mag_s = fft_magnitude(seg_s, n_fft_actual)
    mag_r = fft_magnitude(seg_r, n_fft_actual)

    f0_s = find_f0(mag_s, TARGET_SR, n_fft_actual)
    f0_r = find_f0(mag_r, TARGET_SR, n_fft_actual)
    cent_s = spectral_centroid(mag_s, TARGET_SR, n_fft_actual)
    cent_r = spectral_centroid(mag_r, TARGET_SR, n_fft_actual)
    cent_ratio = cent_s / cent_r if cent_r > 0 else float('nan')

    print(f"  F0 synth           = {f0_s:.2f} Hz")
    print(f"  F0 ref             = {f0_r:.2f} Hz")
    print(f"  Spectral centroid synth = {cent_s:.2f} Hz")
    print(f"  Spectral centroid ref   = {cent_r:.2f} Hz")
    print(f"  Centroid ratio (synth/ref) = {cent_ratio:.4f}")

    f0_bin = f0_s / (TARGET_SR / n_fft_actual)
    harm_s = harmonic_magnitudes(mag_s, f0_bin, n_harmonics=5, n_fft=n_fft_actual)
    f0_bin_r = f0_r / (TARGET_SR / n_fft_actual)
    harm_r = harmonic_magnitudes(mag_r, f0_bin_r, n_harmonics=5, n_fft=n_fft_actual)
    print(f"  Harmonic magnitudes (H1..H5):")
    print(f"    synth : {['%.4f' % h for h in harm_s]}")
    print(f"    ref   : {['%.4f' % h for h in harm_r]}")
    print(f"    ratio (synth/ref): {['%.4f' % (harm_s[i]/harm_r[i] if harm_r[i]>1e-9 else float('nan')) for i in range(5)]}")

    # ── I: Parameter suggestions ─────────────────────────────────────────
    print(f"\n[PARAMETER SUGGESTIONS]")
    params = preset['params']

    # Use synth F0 for decay formula
    f0_for_decay = f0_s if f0_s > 20 else 261.63  # fallback

    suggestions = []

    # T60 / Dkay
    if not math.isnan(t60_ratio):
        if t60_ratio < 0.5:
            new_dkay, new_g = t60_to_dkay(f0_for_decay, t60_r)
            new_dkay = min(199, max(0, round(new_dkay)))
            suggestions.append(
                f"  → T60 ratio = {t60_ratio:.3f} (synth too SHORT). "
                f"Need longer decay.\n"
                f"    Increase Dkay: {params['Dkay']} → {new_dkay}  "
                f"(target T60 = {t60_r:.3f}s, f0 = {f0_for_decay:.1f}Hz, g ≈ {new_g:.5f})"
            )
        elif t60_ratio > 2.0:
            new_dkay, new_g = t60_to_dkay(f0_for_decay, t60_r)
            new_dkay = min(199, max(0, round(new_dkay)))
            suggestions.append(
                f"  → T60 ratio = {t60_ratio:.3f} (synth too LONG). "
                f"Need shorter decay.\n"
                f"    Decrease Dkay: {params['Dkay']} → {new_dkay}  "
                f"(target T60 = {t60_r:.3f}s, f0 = {f0_for_decay:.1f}Hz, g ≈ {new_g:.5f})"
            )
        else:
            suggestions.append(f"  → T60 ratio = {t60_ratio:.3f}  (within ×0.5–×2.0 → Dkay OK)")

    # Spectral centroid / brightness
    if not math.isnan(cent_ratio):
        if cent_ratio > 1.5:
            new_stif = params['MlltStif'] - 100
            suggestions.append(
                f"  → Centroid ratio = {cent_ratio:.4f} (synth too BRIGHT).\n"
                f"    Decrease MlltStif: {params['MlltStif']} → {new_stif}  "
                f"OR increase Mterl: {params['Mterl']} → {params['Mterl']+1}"
            )
        elif cent_ratio < 0.67:
            new_stif = params['MlltStif'] + 100
            suggestions.append(
                f"  → Centroid ratio = {cent_ratio:.4f} (synth too DARK).\n"
                f"    Increase MlltStif: {params['MlltStif']} → {new_stif}  "
                f"OR decrease Mterl: {params['Mterl']} → {params['Mterl']-1}"
            )
        else:
            suggestions.append(f"  → Centroid ratio = {cent_ratio:.4f}  (within 0.67–1.5 → brightness OK)")

    for s in suggestions:
        print(s)

print(f"\n{'=' * 72}")
print("END OF REPORT")
print("=" * 72)
