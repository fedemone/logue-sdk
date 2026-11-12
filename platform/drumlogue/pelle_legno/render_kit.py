#!/usr/bin/env python3
"""render_kit.py -- Pelle e Legno: organic, skin and ethnic percussion for the
KORG drumlogue USER sample bank.

Every sound in the kit is SYNTHESISED here from a physical description of the
instrument -- no recording is copied, resampled or layered -- so the output is
free of third-party sample rights and the whole kit can be regenerated, retuned
or extended from this one file.

The models are "physically informed" rather than full simulations: each one
takes the mechanism that makes the instrument sound like itself and renders
that, then the result is calibrated against measured reference recordings
(see README.md, *Calibration*).

  skins   circular-membrane modes (Bessel zeros), a strike position and a hand
          contact patch that decide which modes the stroke reaches, tension
          modulation (the pitch drop of a hard hit), split degenerate modes
          (the slow beating of a real head), goblet / box Helmholtz resonances,
          slap "crack", snare-string and jingle rattles.
  organic PhISEM-style stochastic particle collisions (Cook) for shakers,
          seeds, beads and the rainstick; ridge-by-ridge scrapes for the guiro;
          free-free bar and hollow-block modes for claves, blocks, slit drums
          and bamboo.
  ethnic  Helmholtz "bloop" of the udu, clay shell modes, cone bells,
          cantilever tines, a buzzing gourd-resonated balafon bar, finger
          cymbals, a struck berimbau wire with its moving gourd, the cuica's
          stick-slip friction voice.

Output: 48 kHz, 32-bit float, mono WAV -- the drumlogue's native format, so the
unit does not have to convert anything at boot -- named `NNN_Name.wav` as the
owner's manual requires (NNN = 001..128, the slot in the USER bank).

Usage:
    python3 render_kit.py                     # render the kit into ./Samples
    python3 render_kit.py --first-index 41    # start at slot 041 (keep 001-040)
    python3 render_kit.py --only DjembeSlap TablaNa --out /tmp/try
    python3 render_kit.py --list              # print the slot map and exit
    python3 render_kit.py --manifest          # print the README sample table

Requires numpy and scipy.  Deterministic: the same command always produces
bit-identical files (every random draw is seeded from the instrument name).
"""
import argparse
import os
import struct
import sys
import zlib

import numpy as np
from scipy import signal, special

SR = 48000
MAX_SECONDS = 4.0          # drumlogue plays at most 4 s of a sample (manual, spec *2)
PEAK_CEILING_DB = -1.0     # sample peak ceiling
LOUDNESS_TARGET = -15.0    # max momentary loudness (BS.1770, 400 ms) target, LUFS
NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                 "-_!?#$%&'()*+,:;<=>@")


# ---------------------------------------------------------------------------
# Primitives
# ---------------------------------------------------------------------------

def nsamp(dur):
    return int(round(dur * SR))


def tvec(dur):
    return np.arange(nsamp(dur)) / SR


def t60_decay(t, t60):
    """Amplitude envelope that falls 60 dB in t60 seconds."""
    return np.exp(-6.907755 * t / t60)


def pulse(ms, shape="sine"):
    """Contact-force pulse of a hand, finger, stick or mallet, unit DC gain.

    Convolving a modal impulse response with it is the linear response to that
    force, so a long soft contact (palm, felt) low-passes the strike and a short
    hard one (stick, finger slap) lets the high modes through -- the first null
    of a half-sine of length T sits at 1.5 / T."""
    n = max(1, int(round(ms * 1e-3 * SR)))
    k = (np.arange(n) + 0.5) / n
    if shape == "sine":
        p = np.sin(np.pi * k)
    elif shape == "skew":          # fast rise, slower release (flesh)
        p = np.where(k < 0.3, np.sin(np.pi * k / 0.6), np.cos(np.pi * (k - 0.3) / 1.4))
    else:
        raise ValueError(shape)
    return p / p.sum()


def strike(ir, ms, shape="sine"):
    if ms <= 0:
        return ir
    return signal.fftconvolve(ir, pulse(ms, shape))[: len(ir)]


def sos_filter(x, kind, freq, order=2):
    nyq = SR / 2
    if kind in ("low", "high"):
        f = min(freq, nyq * 0.98)
        sos = signal.butter(order, f, btype=kind, fs=SR, output="sos")
    else:  # band
        lo, hi = freq
        sos = signal.butter(order, [lo, min(hi, nyq * 0.98)], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def lp(x, f, order=2):
    return sos_filter(x, "low", f, order)


def hp(x, f, order=2):
    return sos_filter(x, "high", f, order)


def bp(x, lo, hi, order=2):
    return sos_filter(x, "band", (lo, hi), order)


def reson(x, f, q, gain=1.0):
    """Two-pole resonator (constant peak gain) -- a body or cavity formant."""
    b, a = signal.iirpeak(min(f, SR * 0.45), q, fs=SR)
    return gain * signal.lfilter(b, a, x)


def peak_eq(x, f, q, db):
    """RBJ peaking EQ."""
    A = 10 ** (db / 40)
    w0 = 2 * np.pi * f / SR
    al = np.sin(w0) / (2 * q)
    b = [1 + al * A, -2 * np.cos(w0), 1 - al * A]
    a = [1 + al / A, -2 * np.cos(w0), 1 - al / A]
    return signal.lfilter(b, a, x)


def white(n, rng):
    return rng.standard_normal(n)


def noise_burst(dur, tau, rng, lo=None, hi=None, attack=0.0002):
    """Exponentially decaying noise, optionally band-limited."""
    t = tvec(dur)
    x = white(len(t), rng) * np.exp(-t / tau)
    if attack > 0:
        x *= np.minimum(1.0, t / attack)
    if lo and hi:
        x = bp(x, lo, hi)
    elif hi:
        x = lp(x, hi)
    elif lo:
        x = hp(x, lo)
    return x


def modes(freqs, amps, t60s, dur, glide=None, phase=np.pi / 2):
    """Sum of exponentially decaying sinusoids.

    Cosine phase by default: a strike sets the surface MOVING, and the radiated
    pressure follows its velocity, which starts at its peak.  (Sine phase -- the
    displacement -- sums to a large one-sided bump at the onset, i.e. DC.)

    glide: optional array (len = samples) of a frequency multiplier shared by
    every mode -- the tension modulation of a membrane or a pressed head."""
    t = tvec(dur)
    out = np.zeros_like(t)
    for i, (f, a, T) in enumerate(zip(freqs, amps, t60s)):
        if a == 0 or f <= 0:
            continue
        if glide is None:
            if f >= 0.47 * SR:
                continue
            ph = 2 * np.pi * f * t
        else:
            fi = f * glide
            if fi.max() >= 0.47 * SR:
                continue
            ph = 2 * np.pi * np.cumsum(fi) / SR
        p0 = phase[i] if np.ndim(phase) else phase
        out += a * t60_decay(t, T) * np.sin(ph + p0)
    return out


def exp_glide(dur, depth, tau):
    """1 + depth*exp(-t/tau): a hit that starts sharp and settles (a membrane
    stretched by its own amplitude relaxes as the vibration decays)."""
    t = tvec(dur)
    return 1.0 + depth * np.exp(-t / tau)


def curve_glide(dur, points):
    """Piecewise-smooth frequency multiplier through (time, ratio) points
    (cosine-interpolated) -- a deliberate bend: talking drum, tabla Ge, cuica."""
    t = tvec(dur)
    out = np.empty_like(t)
    pts = sorted(points)
    out[:] = pts[-1][1]
    for (t0, r0), (t1, r1) in zip(pts[:-1], pts[1:]):
        m = (t >= t0) & (t < t1)
        u = (t[m] - t0) / (t1 - t0)
        out[m] = r0 + (r1 - r0) * (0.5 - 0.5 * np.cos(np.pi * u))
    out[t < pts[0][0]] = pts[0][1]
    return out


def mix(*parts):
    n = max(len(p) for _, p in parts)
    out = np.zeros(n)
    for g, p in parts:
        out[: len(p)] += g * p
    return out


def unit(x):
    m = np.max(np.abs(x))
    return x / m if m > 0 else x


# ---------------------------------------------------------------------------
# Membranes
# ---------------------------------------------------------------------------

def _bessel_table(mmax=10, nmax=6):
    tab = []
    for m in range(mmax + 1):
        for n, j in enumerate(special.jn_zeros(m, nmax), start=1):
            tab.append((m, n, float(j)))
    tab.sort(key=lambda r: r[2])
    return tab


BESSEL = _bessel_table()
J01 = BESSEL[0][2]


def membrane(f01, dur, rng, *, strike_r=0.0, contact_r=0.05, contact_ms=1.0,
             t60=0.5, t60_knee=1500.0, t60_pow=1.0, t60_01=None,
             warp=1.0, m_rad=0.75, n_rad=1.0, tilt=0.0, fmax=14000.0, nmodes=40,
             split=0.006, glide=None, mode_floor=0.04, shape="sine"):
    """Struck circular membrane.

    f01        frequency of the (0,1) mode
    strike_r   strike radius, 0 = centre, 1 = rim
    contact_r  radius of the contact patch (palm ~0.3, finger ~0.08, stick ~0.02)
    t60, t60_knee, t60_pow
               per-mode T60 = t60 / (1 + (f/knee)^pow): skin and air damp the
               upper modes faster
    t60_01     separate T60 of the (0,1) mode (it radiates as a monopole and is
               damped hardest by it on an open-shell drum)
    warp       ratio exponent: <1 pulls the overtones toward harmonic (air
               loading / a loaded head), 1 = ideal membrane
    m_rad      radiation weight per nodal diameter (dipoles and quadrupoles
               radiate less than the breathing modes)
    n_rad      radiation weight per extra nodal circle (the rings of a (0,2)
               mode move in opposite directions and partly cancel)
    split      fractional splitting of the degenerate (m>0) mode pairs: a real
               head is never perfectly uniform, and the pair beats
    """
    freqs, amps, t60s = [], [], []
    count = 0
    for m, n, j in BESSEL:
        ratio = (j / J01) ** warp
        f = f01 * ratio
        if f > fmax or count >= nmodes:
            continue
        count += 1
        shape_at = abs(special.jv(m, j * strike_r)) / abs(special.jv(m + 1, j))
        # every real stroke lands a little off its ideal point: never exactly 0
        shape_at = max(shape_at, mode_floor * rng.uniform(0.5, 1.5))
        x = j * contact_r
        patch = abs(2 * special.j1(x) / x) if x > 1e-6 else 1.0
        a = shape_at * patch * (m_rad ** m) * (n_rad ** (n - 1)) * (ratio ** tilt)
        T = t60 / (1.0 + (f / t60_knee) ** t60_pow)
        if (m, n) == (0, 1) and t60_01 is not None:
            T = t60_01
        if m > 0 and split > 0:
            d = split * rng.uniform(0.4, 1.0)
            th = rng.uniform(0.15, 1.42)
            for s, w in ((-0.5, np.cos(th) ** 2), (0.5, np.sin(th) ** 2)):
                freqs.append(f * (1 + s * d))
                amps.append(a * w)
                t60s.append(T * rng.uniform(0.9, 1.1))
        else:
            freqs.append(f)
            amps.append(a)
            t60s.append(T)
    ir = modes(freqs, amps, t60s, dur, glide=glide)
    return strike(ir, contact_ms, shape)


def helmholtz(f, dur, t60, glide=None, contact_ms=4.0, harm2=0.0):
    """Air cavity (goblet, pot, box) breathing through its opening: one damped
    low resonance, optionally with a weak 2nd harmonic from the neck's
    nonlinearity (the 'bloop' of a hard bass stroke)."""
    ir = modes([f, 2 * f], [1.0, harm2], [t60, t60 * 0.5], dur, glide=glide)
    return strike(ir, contact_ms)


def crack(dur, rng, lo=900, hi=6000, tau=0.004, attack=0.0003):
    """Flesh-on-skin slap transient: a short band-limited noise burst."""
    return noise_burst(dur, tau, rng, lo, hi, attack)


def rattle(drive, rng, lo, hi, thresh=0.15, sharp=2.0, smooth_hz=None):
    """Contact rattle: snare strings, cajon wires, mirliton, coin on a wire.

    Noise is gated by how far the vibrating surface pushes past the rest
    position of the rattling element, so it is modulated at the drive's own
    period (each cycle slaps the wires) and dies before the tone does."""
    d = drive / (np.max(np.abs(drive)) + 1e-12)
    g = np.maximum(0.0, d - thresh) ** sharp
    if smooth_hz:
        g = lp(g, smooth_hz, 1)
    return bp(white(len(d), rng) * g, lo, hi)


def ambience(x, rng, amount=0.06, decay=0.035, lpf=5500.0, predelay=0.0025):
    """A little close-room air (diffuse, no discrete echoes: discrete taps would
    comb-filter a percussive sample).  amount is the wet/dry energy ratio."""
    if amount <= 0:
        return x
    n = nsamp(decay * 7)
    t = np.arange(n) / SR
    ir = white(n, rng) * np.exp(-t / decay)
    ir[: nsamp(predelay)] = 0
    ir = lp(ir, lpf)
    ir /= np.sqrt(np.sum(ir ** 2))
    wet = signal.fftconvolve(x, ir)
    out = np.zeros(len(wet))
    out[: len(x)] += x
    out += np.sqrt(amount) * wet
    return out


# ---------------------------------------------------------------------------
# Particles (PhISEM), scrapes, bars
# ---------------------------------------------------------------------------

def particles(rate, energy, rng, *, click_tau=0.0004, bands=(), hp_hz=None,
              lp_hz=None, jitter=None):
    """Stochastic bead / seed collisions (after P. Cook, PhISEM).

    rate    expected collisions per second, per sample (array)
    energy  amplitude scale per sample (array): the gesture's kinetic energy
    bands   [(freq, Q, gain)] body / shell resonances the clicks excite
    jitter  if set, each collision rings its own randomly detuned copy of the
            first band (seeds and nuts are not identical)
    """
    n = len(rate)
    hits = rng.random(n) < (rate / SR)
    amp = hits * energy * rng.uniform(0.2, 1.0, n)
    level = signal.lfilter([1.0], [1.0, -np.exp(-1.0 / (click_tau * SR))], amp)
    src = level * white(n, rng)
    out = np.zeros(n)
    for f, q, g in bands:
        out += reson(src, f, q, g)
    if jitter:
        f0, q0, g0, spread, groups = jitter
        idx = np.nonzero(hits)[0]
        grp = rng.integers(0, groups, len(idx))
        for k in range(groups):
            sel = np.zeros(n)
            sel[idx[grp == k]] = amp[idx[grp == k]]
            lv = signal.lfilter([1.0], [1.0, -np.exp(-1.0 / (click_tau * SR))], sel)
            fk = f0 * 2 ** (rng.uniform(-spread, spread))
            out += reson(lv * white(n, rng), fk, q0, g0)
    if not bands and not jitter:
        out = src
    if hp_hz:
        out = hp(out, hp_hz)
    if lp_hz:
        out = lp(out, lp_hz)
    return out


def bump(t, center, width):
    """Smooth gesture envelope (raised-cosine bump)."""
    u = (t - center) / width
    return np.where(np.abs(u) < 1, 0.5 + 0.5 * np.cos(np.pi * u), 0.0)


def bar(f0, ratios, amps, t60s, dur, contact_ms, glide=None, shape="sine"):
    ir = modes([f0 * r for r in ratios], amps, t60s, dur, glide=glide)
    return strike(ir, contact_ms, shape)


# ---------------------------------------------------------------------------
# The instruments
# ---------------------------------------------------------------------------
# Each takes a seeded Generator and returns raw audio; render() trims, levels
# and writes it.  Frequencies marked "ref" were measured from the reference
# recordings listed in README.md; the rest are from the instrument literature
# or set by the instrument's playing range.

# ---- SKIN ------------------------------------------------------------------

def djembe_bass(rng):
    dur = 0.9
    # The palm in the centre shoves the head like a piston: the goblet's air
    # answers at its Helmholtz frequency.  A hard bass stroke starts a little
    # sharp as the head is stretched.
    air = helmholtz(74, dur, 0.38, glide=exp_glide(dur, 0.07, 0.03), contact_ms=6, harm2=0.08)
    head = membrane(205, dur, rng, strike_r=0.05, contact_r=0.33, contact_ms=5.5,
                    t60=0.22, t60_knee=500, t60_pow=1.4, t60_01=0.09, warp=0.92,
                    glide=exp_glide(dur, 0.05, 0.02), shape="skew")
    slap = crack(dur, rng, 200, 1800, tau=0.006)
    return mix((1.0, unit(air)), (0.42, unit(head)), (0.10, unit(slap)))


def djembe_tone(rng):
    dur = 0.7
    # ref Djambe-A3: partials 280 / 586 / 825 / 1069 / 1137 / 1509 Hz, a third
    # of the energy in 1-3 kHz, nothing under 100 Hz; the (1,1) pair near
    # 260-280 Hz carries the pitch for ~100 ms and the 1-1.5 kHz modes ring
    # longest.  The head's dense mode field gives the attack, the measured
    # partials the ring.
    head = membrane(175, dur, rng, strike_r=0.80, contact_r=0.04, contact_ms=0.25,
                    t60=0.16, t60_knee=6000, t60_pow=0.6, t60_01=0.04, warp=0.95,
                    m_rad=0.92, tilt=1.2, fmax=12000, glide=exp_glide(dur, 0.025, 0.018),
                    shape="skew")
    ring = modes([229, 262, 281, 586, 825, 1069, 1137, 1509, 1890, 2330],
                 [0.32, 0.28, 0.40, 0.80, 0.45, 0.50, 0.60, 0.50, 0.28, 0.18],
                 [0.28, 0.32, 0.32, 0.26, 0.30, 0.45, 0.48, 0.42, 0.32, 0.25], dur)
    ring = strike(ring, 0.3)
    skin = crack(dur, rng, 2500, 8000, tau=0.003)
    return hp(mix((0.8, unit(head)), (0.75, unit(ring)), (0.85, unit(skin))), 120)


def djembe_slap(rng):
    dur = 0.55
    # Fingers whip onto the edge and stay a moment: the low modes are choked,
    # the high ones ring, and the skin cracks.
    # ref Djambe-B3: a ~1.07 kHz ring that carries the slap (t20 180 ms), 61 %
    # of the energy in 1-3 kHz, an attack centroid of 3 kHz
    head = membrane(205, dur, rng, strike_r=0.90, contact_r=0.05, contact_ms=0.3,
                    t60=0.30, t60_knee=3000, t60_pow=1.0, t60_01=0.02, warp=0.95,
                    m_rad=0.92, tilt=0.6, fmax=16000, glide=exp_glide(dur, 0.02, 0.012),
                    shape="skew")
    ring = modes([1066, 1072, 1135, 1289, 1507, 1729, 1945],
                 [1.0, 0.6, 0.17, 0.11, 0.13, 0.19, 0.11],
                 [0.42, 0.38, 0.28, 0.24, 0.22, 0.19, 0.16], dur)
    ring = strike(ring, 0.3)
    skin = crack(dur, rng, 2500, 7000, tau=0.006)
    out = mix((0.7, unit(head)), (1.0, unit(ring)), (0.8, unit(skin)))
    return hp(out, 250, 4)


def _conga_modes(f0, dur, rng, *, amps, t60s, contact_ms, glide=None):
    # ref Bongo_Conga2: 271 Hz with partials at 1.20 / 1.71 / 2.37 x and a fast
    # first 20 dB (60 ms) over a slower body (t40 259 ms).
    ratios = [1.0, 1.20, 1.71, 1.83, 2.37, 2.46, 3.01, 3.42, 3.95, 4.6, 5.3]
    fr, am, tt = [], [], []
    for r, a, T in zip(ratios, amps, t60s):
        d = rng.uniform(0.002, 0.006)
        for s, w in ((-0.5, 0.6), (0.5, 0.4)):
            fr.append(f0 * r * (1 + s * d))
            am.append(a * w)
            tt.append(T)
    return strike(modes(fr, am, tt, dur, glide=glide), contact_ms)


def conga_open(rng, f0=271.0):
    dur = 0.8
    g = exp_glide(dur, 0.012, 0.03)
    body = _conga_modes(f0, dur, rng,
                        amps=[1.0, 0.55, 0.52, 0.10, 0.16, 0.05, 0.05, 0.03, 0.025, 0.02, 0.015],
                        t60s=[0.50, 0.46, 0.40, 0.25, 0.24, 0.18, 0.14, 0.11, 0.09, 0.07, 0.05],
                        contact_ms=0.7, glide=g)
    # the broadband part of the hand strike is what drops the first 20 dB
    hit = membrane(f0 * 0.66, dur, rng, strike_r=0.8, contact_r=0.10, contact_ms=0.6,
                   t60=0.03, t60_knee=2000, warp=0.95, shape="skew")
    # the first 20 dB go in ~60 ms: the same modes, damped by the hand leaving
    fast = _conga_modes(f0, dur, rng,
                        amps=[1.0, 0.8, 0.8, 0.2, 0.3, 0.1, 0.1, 0.06, 0.05, 0.04, 0.03],
                        t60s=[0.07] * 11, contact_ms=0.7, glide=g)
    skin = crack(dur, rng, 700, 5000, tau=0.003)
    return mix((0.55, unit(body)), (1.0, unit(fast)), (0.6, unit(hit)), (0.10, unit(skin)))


def tumba_open(rng):
    return conga_open(rng, f0=196.0)


def conga_mute(rng):
    # ref Bongo_Conga_Mute4: 652 Hz with 1042 (0.76) and 1396 (0.30); t60 115 ms
    dur = 0.3
    fr = [358, 399, 652, 661, 966, 991, 1042, 1396, 1454, 1960, 2600]
    am = [0.12, 0.18, 1.0, 0.35, 0.11, 0.16, 0.76, 0.30, 0.11, 0.08, 0.05]
    tt = [0.035, 0.035, 0.065, 0.055, 0.035, 0.04, 0.055, 0.04, 0.035, 0.03, 0.02]
    body = strike(modes(fr, am, tt, dur), 0.35, "skew")
    skin = crack(dur, rng, 1000, 6000, tau=0.002)
    return mix((1.0, unit(body)), (0.12, unit(skin)))


def conga_slap(rng):
    dur = 0.45
    head = membrane(185, dur, rng, strike_r=0.86, contact_r=0.06, contact_ms=0.6,
                    t60=0.22, t60_knee=3000, t60_01=0.02, warp=0.95, m_rad=0.9,
                    tilt=1.0, glide=exp_glide(dur, 0.02, 0.01))
    skin = crack(dur, rng, 1500, 8000, tau=0.007)
    return hp(mix((1.0, unit(head)), (0.55, unit(skin))), 400)


def conga_heel(rng):
    dur = 0.4
    # heel of the palm, pressed: a closed, low thud
    head = membrane(150, dur, rng, strike_r=0.35, contact_r=0.35, contact_ms=6.0,
                    t60=0.09, t60_knee=400, t60_pow=1.5, warp=0.9, shape="skew")
    air = helmholtz(118, dur, 0.10, contact_ms=5)
    return mix((1.0, unit(head)), (0.6, unit(air)))


def bongo(rng, f01, tilt=0.4):
    dur = 0.5
    head = membrane(f01, dur, rng, strike_r=0.82, contact_r=0.09, contact_ms=0.9,
                    t60=0.30, t60_knee=1400, t60_01=0.05, warp=0.93, m_rad=0.85,
                    tilt=tilt, glide=exp_glide(dur, 0.03, 0.015))
    skin = crack(dur, rng, 1500, 8000, tau=0.002)
    return mix((1.0, unit(head)), (0.10, unit(skin)))


def partial_cloud(lo, hi, n, t60_lo, t60_hi, dur, rng, tilt=0.0):
    """n inharmonic partials spread log-uniformly over [lo, hi]: the dense,
    quickly-dying upper modes of a struck edge or shell."""
    fr = np.exp(rng.uniform(np.log(lo), np.log(hi), n))
    am = rng.uniform(0.4, 1.0, n) * (fr / lo) ** tilt
    tt = rng.uniform(t60_lo, t60_hi, n)
    return modes(fr, am, tt, dur, phase=rng.uniform(0, 2 * np.pi, n))


def tabla(rng, f0, amps, t60s, dur, *, body_g=1.0, fast_g=0.0, edge_g=1.0,
          edge_band=(900, 3200), edge_t60=(0.05, 0.14), click=0.25, contact_ms=0.3,
          glide=None):
    """Dayan: the syahi (loaded centre patch) tunes the head's modes onto a
    HARMONIC series (Raman, 1934), which is why a tabla has a pitch.  The
    stroke decides which harmonics it feeds; the kinar (the bare rim between
    syahi and gajra) adds a bright, quickly-dying cloud when it is struck.

    Calibrated on the references' energy over time: three quarters of a stroke
    is spent in its first 30 ms, mostly in 1-3 kHz, and what rings on for a
    second and more is the harmonic body."""
    fr, am, tt = [], [], []
    for k, (a, T) in enumerate(zip(amps, t60s), start=1):
        d = rng.uniform(0.0008, 0.002)
        for s, w in ((-0.5, 0.6), (0.5, 0.4)):
            fr.append(f0 * k * (1 + s * d))
            am.append(a * w)
            tt.append(T)
    body = strike(modes(fr, am, tt, dur, glide=glide), contact_ms, "skew")
    # two-stage decay: each harmonic first loses the energy the strike put into
    # the head's coupling to the shell and air (fast), then rings on (slow)
    fast = strike(modes(fr, am, [0.10] * len(fr), dur, glide=glide), contact_ms, "skew")
    edge = strike(partial_cloud(edge_band[0], edge_band[1], 18, edge_t60[0], edge_t60[1], dur, rng), 0.15)
    rim = crack(dur, rng, 2500, 10000, tau=0.002)
    return mix((body_g, unit(body)), (fast_g, unit(fast)), (edge_g, unit(edge)),
               (click, unit(rim)))


def tabla_na(rng):
    # ref percussion-one-shot-tabla-3 (276 Hz; tuned here to C#4): 2nd harmonic
    # dominant, 3rd at 0.70,
    # attack energy 57 % 300 Hz-1 kHz / 31 % 1-3 kHz / 8 % 3-8 kHz; t40 773 ms.
    # Index finger on the rim, ring finger resting on the syahi's edge damps
    # the fundamental.
    return tabla(rng, 277.18, [0.08, 1.0, 0.70, 0.18, 0.13, 0.12, 0.08, 0.06],
                 [0.15, 2.30, 1.90, 1.20, 0.90, 0.70, 0.55, 0.45], 1.8,
                 body_g=0.28, fast_g=0.9, edge_g=0.75, edge_band=(1000, 5000),
                 edge_t60=(0.12, 0.38), click=0.6)


def tabla_tin(rng):
    # ref Tabla-Drum-Hit-D4_: harmonics of 232 Hz, the 3rd strongest, the 2nd
    # 0.76, the 5th 0.45; the fundamental is in the attack and gone by 100 ms;
    # 47 % of the attack in 1-3 kHz.
    return tabla(rng, 232.0, [1.6, 0.76, 1.0, 0.29, 0.45, 0.19, 0.16, 0.10],
                 [0.12, 2.40, 2.80, 1.50, 1.40, 1.00, 0.80, 0.60], 1.8,
                 body_g=0.32, fast_g=0.7, edge_g=0.55, edge_band=(900, 3200),
                 edge_t60=(0.15, 0.35), click=0.55)


def tabla_tun(rng):
    # index finger on the centre of the syahi, lifted at once: the fundamental
    # rings out
    return tabla(rng, 261.63, [1.0, 0.55, 0.35, 0.22, 0.14, 0.09, 0.05],
                 [1.40, 1.10, 0.85, 0.60, 0.45, 0.35, 0.28], 1.7,
                 body_g=1.0, edge_g=0.25, edge_band=(800, 2500), click=0.06, contact_ms=0.8)


def tabla_ge(rng, bend=None):
    """Bayan: big bass head with an off-centre syahi; the heel of the hand on
    the head sets (and bends) the pitch."""
    dur = 1.1
    if bend is None:
        g = exp_glide(dur, 0.06, 0.05)
    else:
        g = bend
    f0 = 98.0
    fr = [f0, 2.02 * f0, 2.98 * f0, 3.95 * f0, 4.9 * f0]
    am = [1.0, 0.45, 0.22, 0.10, 0.05]
    tt = [0.75, 0.50, 0.35, 0.22, 0.15]
    body = strike(modes(fr, am, tt, dur, glide=g), 2.5)
    thud = membrane(140, dur, rng, strike_r=0.5, contact_r=0.12, contact_ms=2.0,
                    t60=0.06, t60_knee=700)
    return mix((1.0, unit(body)), (0.25, unit(thud)))


def tabla_ge_bend(rng):
    dur = 1.1
    # heel slides toward the syahi: about a fourth up over a quarter second
    g = curve_glide(dur, [(0.0, 1.03), (0.04, 1.0), (0.30, 1.32), (1.1, 1.36)])
    return tabla_ge(rng, bend=g)


def tabla_ke(rng):
    # flat palm on the bayan, left there: closed and dry
    dur = 0.25
    head = membrane(110, dur, rng, strike_r=0.4, contact_r=0.4, contact_ms=3.0,
                    t60=0.05, t60_knee=500, t60_pow=1.5)
    slap = crack(dur, rng, 300, 3000, tau=0.006)
    return mix((1.0, unit(head)), (0.35, unit(slap)))


def darbuka_doum(rng):
    dur = 0.9
    air = helmholtz(96, dur, 0.45, glide=exp_glide(dur, 0.05, 0.03), contact_ms=3.5, harm2=0.06)
    head = membrane(230, dur, rng, strike_r=0.15, contact_r=0.16, contact_ms=2.5,
                    t60=0.35, t60_knee=600, t60_pow=1.3, warp=0.95, m_rad=0.7,
                    glide=exp_glide(dur, 0.03, 0.02))
    return mix((1.0, unit(air)), (0.55, unit(head)))


def darbuka_tek(rng, soft=False):
    dur = 0.6
    # ring finger on the rim of a metal-bodied darbuka: bright, ringing edge modes
    head = membrane(230, dur, rng, strike_r=0.93, contact_r=0.04 if not soft else 0.07,
                    contact_ms=0.45 if not soft else 0.8, t60=0.36, t60_knee=3500,
                    t60_01=0.03, warp=0.97, m_rad=0.95, tilt=1.4 if not soft else 0.9,
                    fmax=16000, split=0.004)
    rim = crack(dur, rng, 2500, 11000, tau=0.0015)
    out = mix((1.0, unit(head)), (0.25 if not soft else 0.12, unit(rim)))
    return hp(out, 500)


def darbuka_ka(rng):
    return darbuka_tek(rng, soft=True)


def bendir(rng, doum=True):
    """North-African frame drum with gut snares under the head."""
    dur = 1.0
    if doum:
        head = membrane(88, dur, rng, strike_r=0.2, contact_r=0.2, contact_ms=3.5,
                        t60=0.55, t60_knee=500, t60_pow=1.2, warp=1.0, m_rad=0.7,
                        glide=exp_glide(dur, 0.04, 0.03))
        buzz_thr, buzz_g = 0.10, 0.55
    else:
        head = membrane(88, dur, rng, strike_r=0.9, contact_r=0.05, contact_ms=0.8,
                        t60=0.40, t60_knee=2500, t60_01=0.05, warp=1.0, m_rad=0.9,
                        tilt=0.6, fmax=12000)
        buzz_thr, buzz_g = 0.22, 0.30
    head = unit(head)
    buzz = rattle(head, rng, 1800, 7000, thresh=buzz_thr, sharp=1.5)
    return mix((1.0, head), (buzz_g, unit(buzz)))


def jingles(n, rng, pairs, f_lo, f_hi, t60=(0.05, 0.18), spread=0.10, bounce=(0.006, 0.02)):
    """Pairs of thin brass discs on a pin: struck together, each pair rings a
    dense set of plate modes and clashes again a few times as it bounces."""
    out = np.zeros(n)
    for _ in range(pairs):
        k_modes = 16
        fs = np.exp(rng.uniform(np.log(f_lo), np.log(f_hi), k_modes))
        ring = modes(fs, rng.uniform(0.2, 1.0, k_modes), rng.uniform(t60[0], t60[1], k_modes),
                     0.4, phase=rng.uniform(0, 2 * np.pi, k_modes))
        clash = noise_burst(0.4, 0.0012, rng, f_lo, min(f_hi * 1.3, 20000))
        hit = unit(ring) + 0.6 * unit(clash)
        t_hit, a = rng.uniform(0.0003, 0.003), 1.0
        while t_hit < spread and a > 0.05:
            k = nsamp(t_hit)
            seg = a * hit[: n - k]
            out[k:k + len(seg)] += seg
            t_hit += rng.uniform(*bounce)
            a *= rng.uniform(0.35, 0.6)
    return out


def riq(rng):
    """Arabic tambourine: a finger 'tek' on the small head shakes five pairs of
    brass jingles that bounce together for a few tens of ms."""
    dur = 0.7
    n = nsamp(dur)
    head = membrane(310, dur, rng, strike_r=0.9, contact_r=0.05, contact_ms=0.6,
                    t60=0.12, t60_knee=2000, warp=1.0, tilt=0.5)
    jing = jingles(n, rng, 5, 4200, 14000, t60=(0.06, 0.22), spread=0.12)
    return mix((0.5, unit(head)), (0.9, unit(jing)))


def pandeiro(rng):
    dur = 0.8
    n = nsamp(dur)
    # thumb on the edge: the head's low modes, then the platinelas (cupped,
    # darker than riq jingles)
    head = membrane(175, dur, rng, strike_r=0.85, contact_r=0.12, contact_ms=2.0,
                    t60=0.30, t60_knee=900, t60_01=0.12, warp=0.97, m_rad=0.8)
    jing = jingles(n, rng, 5, 3000, 10000, t60=(0.05, 0.16), spread=0.10, bounce=(0.008, 0.025))
    return mix((1.0, unit(head)), (0.55, unit(jing)))


def cajon(rng, slap=False):
    """Plywood box: the tapa's plate modes, the box's Helmholtz port, and the
    snare wires behind the tapa."""
    dur = 0.6
    if not slap:
        air = helmholtz(82, dur, 0.22, glide=exp_glide(dur, 0.04, 0.02), contact_ms=6)
        # rectangular plate, centre strike: odd-odd modes (ratios of a 3:4 plate)
        pr = [1.0, 2.44, 3.31, 4.0, 5.44, 6.2]
        plate = bar(118, pr, [1.0, 0.25, 0.30, 0.12, 0.08, 0.05],
                    [0.12, 0.07, 0.06, 0.05, 0.04, 0.03], dur, 5.0, shape="skew")
        body = mix((1.0, unit(air)), (0.7, unit(plate)))
        buzz = rattle(unit(body), rng, 2500, 9000, thresh=0.35, sharp=1.5)
        return mix((1.0, body), (0.10, unit(buzz)))
    pr = [1.0, 1.52, 2.44, 3.31, 4.0, 5.44, 6.2, 7.9, 9.6, 12.1]
    plate = bar(240, pr, [0.25, 0.6, 1.0, 0.9, 0.7, 0.6, 0.5, 0.35, 0.25, 0.15],
                [0.06, 0.07, 0.08, 0.07, 0.06, 0.05, 0.05, 0.04, 0.035, 0.03], dur, 0.8)
    buzz = rattle(unit(plate), rng, 2500, 10000, thresh=0.12, sharp=1.2, smooth_hz=None)
    tail = noise_burst(dur, 0.045, rng, 3000, 10000)
    skin = crack(dur, rng, 600, 4000, tau=0.004)
    return mix((1.0, unit(plate)), (0.55, unit(buzz)), (0.25, unit(tail)), (0.25, unit(skin)))


def talking_drum(rng, up=True):
    """Hourglass drum: squeezing the lacing under the arm tightens both heads,
    so the stick stroke glides -- the drum 'speaks'."""
    dur = 0.9
    if up:
        g = curve_glide(dur, [(0.0, 1.0), (0.02, 1.0), (0.20, 1.45), (0.9, 1.5)])
    else:
        g = curve_glide(dur, [(0.0, 1.45), (0.02, 1.45), (0.22, 1.0), (0.9, 0.97)])
    # struck near the centre with a curved stick: the breathing (0,1) mode
    # carries the pitch, and the two laced heads pull the overtones close to
    # harmonic
    head = membrane(150, dur, rng, strike_r=0.25, contact_r=0.04, contact_ms=1.2,
                    t60=1.1, t60_knee=700, t60_pow=1.3, warp=0.62, m_rad=0.7,
                    n_rad=0.4, glide=g, nmodes=24)
    stick = crack(dur, rng, 600, 5000, tau=0.002)
    return mix((1.0, unit(head)), (0.08, unit(stick)))


def surdo(rng, mute=False):
    dur = 1.6 if not mute else 0.45
    head = membrane(58, dur, rng, strike_r=0.25, contact_r=0.12, contact_ms=4.5,
                    t60=1.25 if not mute else 0.22, t60_knee=260, t60_pow=1.2,
                    warp=0.95, m_rad=0.75, n_rad=0.6, glide=exp_glide(dur, 0.06, 0.04))
    felt = crack(dur, rng, 200, 2500, tau=0.004)
    return mix((1.0, unit(head)), (0.06, unit(felt)))


def tamborim(rng):
    dur = 0.35
    head = membrane(560, dur, rng, strike_r=0.6, contact_r=0.02, contact_ms=0.35,
                    t60=0.14, t60_knee=2500, warp=0.98, m_rad=0.85, tilt=0.4,
                    fmax=16000, glide=exp_glide(dur, 0.02, 0.01))
    stick = crack(dur, rng, 2000, 12000, tau=0.0015)
    return mix((1.0, unit(head)), (0.30, unit(stick)))


def cuica(rng):
    """Friction drum: a wet cloth rubbed along a stick fixed to the head drives
    it by stick-slip, so the head speaks with a VOICE (a harmonic-rich pulse
    train) whose pitch the thumb on the head bends: the 'laugh'."""
    dur = 0.55
    t = tvec(dur)
    f = 430 * curve_glide(dur, [(0.0, 1.0), (0.12, 1.75), (0.22, 1.6), (0.40, 1.15), (0.55, 1.1)])
    ph = np.cumsum(f) / SR
    # band-limited sawtooth-ish slip pulse
    x = np.zeros_like(t)
    for k in range(1, 26):
        fk = f * k
        x += np.where(fk < 0.45 * SR, np.sin(2 * np.pi * k * ph) / k ** 1.15, 0.0)
    env = np.minimum(1.0, t / 0.02) * (0.6 + 0.4 * bump(t, 0.13, 0.13)) * np.exp(-np.maximum(0, t - 0.32) / 0.06)
    x *= env
    # head and shell formants
    y = 0.6 * reson(x, 900, 3.0, 1.0) + 0.4 * reson(x, 1800, 4.0, 1.0) + 0.8 * x
    fric = lp(white(len(t), rng), 3000) * env * 0.06
    return y + fric


def taiko(rng):
    dur = 2.4
    # ref Taiko-Hit: 88 Hz fundamental under a dense cluster at 120-250 Hz from
    # two thick cowhide heads coupled through the barrel.  The CLUSTER, not the
    # fundamental, holds two thirds of the energy for the whole ring (it is
    # 64-70 % of 100-300 Hz in every window from 0 to 3 s), and the barrel adds
    # a little 1-3 kHz wood for the first 300 ms.
    head = membrane(88, dur, rng, strike_r=0.55, contact_r=0.10, contact_ms=2.5,
                    t60=3.6, t60_knee=420, t60_pow=1.4, t60_01=2.8, warp=0.85,
                    m_rad=0.97, split=0.012, nmodes=36, glide=exp_glide(dur, 0.05, 0.05))
    cluster = modes([121, 136, 152, 184, 217, 242, 282],
                    [0.55, 0.32, 0.93, 0.89, 0.85, 0.91, 0.36],
                    [2.8, 2.6, 2.6, 2.3, 2.1, 1.9, 1.5], dur,
                    glide=exp_glide(dur, 0.03, 0.05))
    cluster = strike(cluster, 2.5)
    # the strike itself: the same heads, before the coupling has spread the
    # energy -- what makes the first 30 ms carry almost half the hit
    fast = membrane(88, dur, rng, strike_r=0.55, contact_r=0.10, contact_ms=4.0,
                    t60=0.12, t60_knee=300, t60_pow=1.5, warp=0.85, m_rad=0.97, nmodes=36)
    fund = strike(modes([88.0], [1.0], [3.0], dur, glide=exp_glide(dur, 0.05, 0.05)), 3.0)
    barrel = strike(partial_cloud(900, 3200, 14, 0.30, 0.60, dur, rng), 0.5)
    bachi = crack(dur, rng, 300, 4000, tau=0.004)
    return mix((1.0, unit(head)), (0.6, unit(cluster)), (1.0, unit(fast)),
               (0.25, unit(fund)), (1.0, unit(barrel)), (0.15, unit(bachi)))


# ---- ORGANIC ---------------------------------------------------------------

def onset_env(t, t_acc, tau, pre=0.4):
    """A short swell into an accent at t_acc, then an exponential decay.

    The accent -- the beat -- sits within the first ~10 ms on purpose: a
    sequencer fires the sample at the step, so a shaker whose seeds hit the wall
    70 ms in would always play 70 ms late."""
    rise = np.clip(t / t_acc, 0.0, 1.0) ** 2
    fall = np.exp(-np.maximum(0.0, t - t_acc) / tau)
    return np.where(t < t_acc, pre * rise, fall)


def shaker(rng, back=False):
    """Egg / tube shaker, one stroke: the seeds hit the end wall (the accent,
    on the beat) and then slide and roll back down the tube."""
    dur = 0.45
    t = tvec(dur)
    if not back:
        accent = onset_env(t, 0.008, 0.022)
        roll = onset_env(t, 0.012, 0.07, pre=0.0)
        rate = 9000 * accent + 1800 * roll
        energy = 1.0 * accent + 0.28 * roll
    else:
        # the off-beat stroke: softer wall hit, more slide
        accent = onset_env(t, 0.006, 0.03)
        roll = onset_env(t, 0.02, 0.10, pre=0.2)
        rate = 4000 * accent + 2500 * roll
        energy = 0.55 * accent + 0.35 * roll
    return particles(rate, energy, rng, click_tau=0.00025,
                     bands=[(5200, 1.4, 1.0), (8200, 1.6, 0.8), (3100, 2.5, 0.25)],
                     hp_hz=1800)


def maracas(rng):
    # ref MaracasPair: 31 ms attack, 3-8 kHz 46 %, >8 kHz 54 %, t40 229 ms
    dur = 0.4
    t = tvec(dur)
    accent = onset_env(t, 0.012, 0.05) + 0.30 * onset_env(t, 0.012, 0.10, pre=0.0)  # + settling
    rate = 12000 * accent
    return particles(rate, accent, rng, click_tau=0.00015,
                     bands=[(5600, 1.2, 1.0), (9500, 1.5, 1.1)], hp_hz=3000,
                     jitter=(7000, 6.0, 0.6, 0.4, 6))


def caxixi(rng):
    dur = 0.35
    t = tvec(dur)
    # seeds hit the hard gourd base: a sharp chk, with the wicker rattling
    accent = np.exp(-t / 0.012) * np.minimum(1.0, t / 0.002)
    tail = bump(t, 0.03, 0.05)
    rate = 9000 * accent + 1500 * tail
    energy = accent + 0.25 * tail
    beads = particles(rate, energy, rng, click_tau=0.0003,
                      bands=[(3600, 2.0, 1.0), (6800, 2.0, 0.9), (1500, 3.0, 0.3)], hp_hz=900)
    base = bar(1300, [1.0, 2.1, 3.4], [1.0, 0.4, 0.2], [0.02, 0.015, 0.01], dur, 0.4)
    return mix((1.0, unit(beads)), (0.35, unit(base)))


def cabasa(rng):
    dur = 0.4
    t = tvec(dur)
    # steel bead chain rolled against the ridged cylinder: dense and bright
    twist = onset_env(t, 0.007, 0.045) + 0.15 * onset_env(t, 0.007, 0.12, pre=0.0)
    rate = 20000 * twist
    return particles(rate, twist, rng, click_tau=0.00012,
                     bands=[(4800, 1.5, 1.0), (8800, 1.8, 1.0), (12000, 2.0, 0.6)],
                     hp_hz=2500)


def shekere_hit(rng):
    """Gourd strung with a bead net: heel of the hand on the base drives the
    gourd's air, the net throws the beads."""
    dur = 0.55
    t = tvec(dur)
    gourd = helmholtz(180, dur, 0.12, contact_ms=4)
    shell = bar(620, [1.0, 1.7, 2.6], [1.0, 0.5, 0.3], [0.05, 0.04, 0.03], dur, 2.0)
    throw = bump(t, 0.05, 0.05) + np.exp(-t / 0.015)
    beads = particles(5000 * throw, throw, rng, click_tau=0.0003,
                      bands=[(3300, 6.0, 1.0), (4600, 6.0, 1.0), (2400, 3.0, 0.4)],
                      hp_hz=1200, jitter=(3800, 9.0, 0.7, 0.3, 6))
    return mix((0.8, unit(gourd)), (0.35, unit(shell)), (0.8, unit(beads)))


def seed_pods(rng):
    """Chajchas / nut-shell rattle: hard, hollow, few large pods."""
    # ref ChaChaNut2: 19 ms attack, centroid ~4.5 kHz, peaks near 3.3 and 4.6 kHz
    dur = 0.6
    t = tvec(dur)
    g = np.minimum(1.0, t / 0.02) * (0.6 * np.exp(-np.maximum(0, t - 0.02) / 0.07) +
                                      0.4 * np.exp(-np.maximum(0, t - 0.02) / 0.2))
    return particles(1800 * g, g, rng, click_tau=0.0007,
                     bands=[(3300, 9.0, 1.0), (4600, 9.0, 1.0)], hp_hz=1500,
                     jitter=(3900, 12.0, 1.0, 0.35, 10))


def rainstick(rng):
    dur = 2.6
    t = tvec(dur)
    # pebbles cascading through the thorns of a dried cactus tube
    flow = np.minimum(1.0, t / 0.35) ** 1.5 * np.exp(-np.maximum(0, t - 0.5) / 0.9)
    flow *= 1 + 0.25 * np.sin(2 * np.pi * 1.7 * t + 1.0)     # uneven flow
    x = particles(2600 * flow, flow, rng, click_tau=0.0002,
                  bands=[(2600, 4.0, 0.6), (4200, 3.0, 0.8), (6500, 2.5, 0.7)],
                  hp_hz=1500, jitter=(5000, 10.0, 0.6, 0.6, 12))
    # the tube's air colours it
    for f in (900, 1820, 2730):
        x = peak_eq(x, f, 6.0, 4.0)
    return x


def guiro(rng, long=False):
    """A stick drawn across the gourd's ridges: one click per ridge, at the
    speed of the stroke, ringing the hollow gourd."""
    # ref Guiro1 (a short stroke): 1036 Hz body over 2.8-6 kHz scrape, t40 90 ms
    dur = 0.35 if not long else 0.75
    n = nsamp(dur)
    t = tvec(dur)
    # stroke speed in ridges per second; the stick is already moving when the
    # sample starts, so the first ridge is on the step
    if not long:
        speed = 190 * np.interp(t, [0, 0.02, 0.06, 0.085], [0.6, 1.0, 0.8, 0.0])
    else:   # the classic long-short: a slow down-stroke, a quick flick back
        speed = 190 * np.interp(t, [0, 0.04, 0.20, 0.235, 0.30, 0.32, 0.37, 0.40],
                                [0.35, 0.45, 0.40, 0.0, 0.0, 1.0, 0.8, 0.0])
    pos = np.cumsum(speed) / SR                       # ridges travelled
    ridges = 0.01 + np.concatenate([[0.0], np.cumsum(rng.uniform(0.85, 1.15, int(pos[-1]) + 2))])
    edges = np.searchsorted(pos, ridges[ridges < pos[-1]])
    edges = edges[edges < n]
    clicks = np.zeros(n)
    clicks[edges] = rng.uniform(0.6, 1.0, len(edges)) * np.sqrt(speed[edges] / speed.max() + 0.05)
    exc = signal.lfilter([1.0], [1.0, -np.exp(-1 / (0.0004 * SR))], clicks) * white(n, rng)
    body = (reson(exc, 1036, 12.0, 2.6) + reson(exc, 2830, 8.0, 0.55) +
            reson(exc, 3400, 8.0, 0.55) + reson(exc, 3900, 8.0, 0.45) +
            reson(exc, 5400, 6.0, 0.6))
    scrape = bp(exc, 4000, 13000)
    return body + 0.30 * scrape


def claves(rng):
    # ref wetclave: ~2.65 kHz, t40 180 ms, 97 % of the energy in 1-3 kHz.
    # Free-free cylinder: 1 : 2.756 : 5.404; the cupped hand under it is a
    # resonator tuned near the fundamental.
    dur = 0.4
    x = bar(2640, [1.0, 1.018, 0.988, 2.756, 5.404], [1.0, 0.55, 0.35, 0.25, 0.10],
            [0.26, 0.22, 0.18, 0.03, 0.012], dur, 0.2)
    tick = crack(dur, rng, 3000, 12000, tau=0.0006)
    return mix((1.0, unit(x)), (0.12, unit(tick)))


def woodblock(rng, f0):
    # ref WoodBlock1: 849 Hz with a split partner at 820, faint 2.57x and 4.44x,
    # t60 165 ms
    dur = 0.35
    x = bar(f0, [1.0, 0.966, 1.07, 2.57, 4.44], [1.0, 0.65, 0.2, 0.07, 0.07],
            [0.16, 0.14, 0.09, 0.05, 0.03], dur, 0.35)
    tick = crack(dur, rng, 2500, 10000, tau=0.0008)
    return mix((1.0, x), (0.10, unit(tick)))


def temple_block(rng):
    # mokugyo: a hollow carved bell of wood -- rounder, the cavity rings
    dur = 0.5
    x = bar(520, [1.0, 1.52, 2.31, 3.6], [1.0, 0.35, 0.18, 0.06], [0.22, 0.12, 0.07, 0.04], dur, 0.9)
    air = helmholtz(420, dur, 0.10, contact_ms=1.0)
    tick = crack(dur, rng, 2000, 8000, tau=0.0008)
    return mix((1.0, unit(x)), (0.35, unit(air)), (0.06, unit(tick)))


def slit_drum(rng, f0):
    """Log / slit drum: two wooden tongues over a hollowed trunk, rubber mallet."""
    dur = 1.0
    tongue = bar(f0, [1.0, 2.92, 3.85, 6.1, 8.7], [1.0, 0.40, 0.22, 0.10, 0.05],
                 [0.70, 0.22, 0.12, 0.06, 0.04], dur, 1.6)
    cavity = helmholtz(f0 * 0.5, dur, 0.30, contact_ms=4.0)
    # the trunk's own wood: a short, dense knock under the tongue's note
    wood = strike(partial_cloud(f0 * 1.5, 2600, 16, 0.03, 0.09, dur, rng, tilt=-0.5), 0.8)
    knock = crack(dur, rng, 400, 3000, tau=0.002)
    return mix((1.0, unit(tongue)), (0.25, unit(cavity)), (0.45, unit(wood)), (0.08, unit(knock)))


def bamboo(rng):
    """Bamboo tube knocked with a stick: a closed-open air column (odd
    harmonics) over the short ring of the bamboo wall."""
    dur = 0.45
    f0 = 340
    col = modes([f0, 3 * f0, 5 * f0, 7 * f0], [1.0, 0.35, 0.15, 0.06],
                [0.18, 0.10, 0.06, 0.04], dur)
    wall = bar(1450, [1.0, 2.4, 4.1], [1.0, 0.4, 0.15], [0.06, 0.04, 0.02], dur, 0.4)
    tick = crack(dur, rng, 2000, 9000, tau=0.0007)
    return mix((1.0, unit(strike(col, 0.6))), (0.55, unit(wall)), (0.08, unit(tick)))


def coconut(rng):
    # two half coconut shells clapped: hollow wooden 'clop'
    # ref KnockFoley1 (knock on a hollow body): ~280 Hz, t60 165 ms
    dur = 0.3
    x = bar(1180, [1.0, 1.43, 2.2, 3.1], [1.0, 0.6, 0.3, 0.15], [0.05, 0.04, 0.03, 0.02], dur, 0.5)
    air = helmholtz(560, dur, 0.06, contact_ms=0.8)
    return mix((1.0, unit(x)), (0.7, unit(air)))


# ---- ETHNIC: clay, metal, wire, bars ---------------------------------------

def udu_bass(rng):
    """Udu: the palm slapped over the side hole drives the pot's air; as the
    hand comes away the hole opens and the Helmholtz pitch rises -- the
    water-drop 'bloop'."""
    dur = 1.0
    g = curve_glide(dur, [(0.0, 0.78), (0.02, 0.80), (0.14, 1.0), (1.0, 1.02)])
    air = helmholtz(112, dur, 0.55, glide=g, contact_ms=5.0, harm2=0.10)
    pop = noise_burst(dur, 0.006, rng, 80, 900)
    return mix((1.0, unit(air)), (0.10, unit(pop)))


def udu_tone(rng):
    # fingertip on the belly: clay is lossy, so the shell's modes die fast,
    # and the cavity answers underneath
    dur = 0.5
    shell = bar(410, [1.0, 1.94, 2.85, 4.1, 5.6], [1.0, 0.55, 0.30, 0.15, 0.08],
                [0.14, 0.09, 0.06, 0.04, 0.03], dur, 1.0)
    air = helmholtz(112, dur, 0.30, contact_ms=2.0)
    return mix((1.0, unit(shell)), (0.45, unit(air)))


def agogo(rng, f0):
    """Cone bell struck with a stick: a few strongly inharmonic modes."""
    dur = 1.3
    ratios = [1.0, 1.006, 2.47, 2.49, 3.92, 5.43, 7.1]
    amps = [1.0, 0.22, 0.40, 0.12, 0.25, 0.12, 0.06]
    t60s = [1.10, 1.00, 0.55, 0.50, 0.35, 0.22, 0.15]
    x = bar(f0, ratios, amps, t60s, dur, 0.3)
    stick = crack(dur, rng, 3000, 12000, tau=0.0008)
    return mix((1.0, unit(x)), (0.10, unit(stick)))


def kalimba(rng, f0):
    """Thumb piano tine: a clamped-free bar, so its 2nd mode is ~6.3x up (ref
    kalimba-e_E measures 6.38x); the box adds its air resonance."""
    dur = 2.4
    x = bar(f0, [1.0, 6.37, 17.8], [1.0, 3.0, 0.25], [3.2, 0.25, 0.07], dur, 0.3)
    box = helmholtz(260, dur, 0.15, contact_ms=1.0)
    thumb = crack(dur, rng, 500, 4000, tau=0.002)
    out = mix((1.0, unit(x)), (0.08, unit(box)), (0.04, unit(thumb)))
    return peak_eq(out, 260, 1.5, 3.0)


def balafon(rng, f0):
    """West-African xylophone: a tuned rosewood bar over a gourd with a
    buzzing mirliton (spider-egg-case membrane) on its side."""
    dur = 1.0
    x = bar(f0, [1.0, 3.92, 9.2], [1.0, 0.30, 0.06], [0.55, 0.16, 0.05], dur, 1.6)
    x = peak_eq(x, f0, 6.0, 6.0)            # gourd tuned to the bar
    x = unit(x)
    buzz = rattle(x, rng, 600, 4500, thresh=0.25, sharp=1.3)
    # the mirliton's membrane rings as a nasal formant over the buzz
    buzz = reson(buzz, 1300, 2.0, 1.0) + 0.5 * buzz
    return mix((1.0, x), (0.45, unit(buzz)))


def zills(rng):
    """Finger cymbals, a pair clashed edge-on: a 'ching' of high, slowly
    beating partials (the two are never exactly the same)."""
    dur = 3.0
    base = [2470, 3610, 5290, 6420, 7930, 9810, 11900, 13700]
    am = [1.0, 0.7, 0.55, 0.40, 0.35, 0.25, 0.15, 0.10]
    tt = [2.6, 2.2, 1.8, 1.5, 1.2, 0.9, 0.7, 0.5]
    fr, aa, t6 = [], [], []
    for f, a, T in zip(base, am, tt):
        for d, w in ((1.0, 1.0), (1.0 + rng.uniform(0.004, 0.011), rng.uniform(0.25, 0.5))):
            fr.append(f * d)
            aa.append(a * w)
            t6.append(T * rng.uniform(0.85, 1.1))
    x = strike(modes(fr, aa, t6, dur), 0.2)
    clash = noise_burst(dur, 0.004, rng, 4000, 16000)
    return mix((1.0, unit(x)), (0.15, unit(clash)))


def berimbau(rng, coin=False):
    """Musical bow: a steel wire struck with a thin stick, its gourd held
    against the belly and swung away -- the 'wah'.  The coin (dobrao) pressed
    on the wire shortens it a whole tone."""
    dur = 0.9
    f0 = 117.0 * (2 ** (2 / 12) if coin else 1.0)
    t = tvec(dur)
    k = np.arange(1, 40)
    B = 0.0004                                         # wire stiffness
    fr = f0 * k * np.sqrt(1 + B * k ** 2)
    pos = 0.12                                         # struck near the bridge
    am = np.abs(np.sin(np.pi * k * pos)) / k ** 0.8
    t60 = 0.9 / (1 + (fr / 1500) ** 1.2)
    if coin:
        t60 = t60 * 0.55
    x = strike(modes(fr, am, t60, dur), 0.25)
    # gourd opening: a resonance that sweeps up and back
    fc = 380 + 520 * bump(t, 0.16, 0.16) + 120 * np.clip((t - 0.08) / 0.16, 0.0, 1.0)
    y = np.zeros_like(x)
    hop = 256
    zi = None
    for s in range(0, len(x), hop):
        b, a = signal.iirpeak(fc[s], 2.5, fs=SR)
        if zi is None:
            zi = signal.lfilter_zi(b, a) * 0
        y[s:s + hop], zi = signal.lfilter(b, a, x[s:s + hop], zi=zi)
    stick = crack(dur, rng, 2000, 9000, tau=0.0012)
    return mix((0.75, unit(y)), (0.35, unit(x)), (0.08, unit(stick)))


# ---------------------------------------------------------------------------
# The kit: slot order = USER-bank order.  (Name, family, renderer, max s, room)
# Names are <= 12 characters so they fit the drumlogue display.
# ---------------------------------------------------------------------------

KIT = [
    # skin -- hand drums of West Africa and the Caribbean
    ("DjembeBass",  "skin",    djembe_bass,                    0.9, 0.05),
    ("DjembeTone",  "skin",    djembe_tone,                    0.7, 0.05),
    ("DjembeSlap",  "skin",    djembe_slap,                    0.55, 0.05),
    ("CongaOpen",   "skin",    conga_open,                     0.8, 0.05),
    ("CongaMute",   "skin",    conga_mute,                     0.3, 0.05),
    ("CongaSlap",   "skin",    conga_slap,                     0.45, 0.05),
    ("CongaHeel",   "skin",    conga_heel,                     0.4, 0.05),
    ("TumbaOpen",   "skin",    tumba_open,                     0.8, 0.05),
    ("BongoMacho",  "skin",    lambda r: bongo(r, 300.0),      0.5, 0.05),
    ("BongoHembra", "skin",    lambda r: bongo(r, 215.0, 0.3), 0.5, 0.05),
    # skin -- India
    ("TablaNa",     "skin",    tabla_na,                       1.6, 0.04),
    ("TablaTin",    "skin",    tabla_tin,                      1.6, 0.04),
    ("TablaTun",    "skin",    tabla_tun,                      1.7, 0.04),
    ("TablaGe",     "skin",    tabla_ge,                       1.1, 0.04),
    ("TablaGeBend", "skin",    tabla_ge_bend,                  1.1, 0.04),
    ("TablaKe",     "skin",    tabla_ke,                       0.25, 0.04),
    # skin -- Middle East and North Africa
    ("DarbukaDoum", "skin",    darbuka_doum,                   0.9, 0.05),
    ("DarbukaTek",  "skin",    darbuka_tek,                    0.6, 0.05),
    ("DarbukaKa",   "skin",    darbuka_ka,                     0.6, 0.05),
    ("BendirDoum",  "skin",    lambda r: bendir(r, True),      1.0, 0.05),
    ("BendirTek",   "skin",    lambda r: bendir(r, False),     1.0, 0.05),
    ("Riq",         "skin",    riq,                            0.7, 0.04),
    # skin -- the Americas, West Africa, Japan
    ("CajonBass",   "skin",    lambda r: cajon(r, False),      0.6, 0.06),
    ("CajonSlap",   "skin",    lambda r: cajon(r, True),       0.6, 0.06),
    ("TalkDrumUp",  "skin",    lambda r: talking_drum(r, True),  0.9, 0.05),
    ("TalkDrumDn",  "skin",    lambda r: talking_drum(r, False), 0.9, 0.05),
    ("SurdoOpen",   "skin",    lambda r: surdo(r, False),      1.6, 0.05),
    ("SurdoMute",   "skin",    lambda r: surdo(r, True),       0.45, 0.05),
    ("Tamborim",    "skin",    tamborim,                       0.35, 0.05),
    ("Pandeiro",    "skin",    pandeiro,                       0.8, 0.05),
    ("Cuica",       "skin",    cuica,                          0.55, 0.05),
    ("Taiko",       "skin",    taiko,                          2.4, 0.06),
    # organic -- seeds, beads, gourds, wood, bamboo
    ("ShakerFwd",   "organic", lambda r: shaker(r, False),     0.45, 0.0),
    ("ShakerBack",  "organic", lambda r: shaker(r, True),      0.45, 0.0),
    ("Maracas",     "organic", maracas,                        0.4, 0.0),
    ("Caxixi",      "organic", caxixi,                         0.35, 0.0),
    ("Cabasa",      "organic", cabasa,                         0.4, 0.0),
    ("ShekereHit",  "organic", shekere_hit,                    0.55, 0.03),
    ("SeedPods",    "organic", seed_pods,                      0.6, 0.0),
    ("Rainstick",   "organic", rainstick,                      2.6, 0.0),
    ("GuiroShort",  "organic", lambda r: guiro(r, False),      0.35, 0.03),
    ("GuiroLong",   "organic", lambda r: guiro(r, True),       0.75, 0.03),
    ("Claves",      "organic", claves,                         0.4, 0.03),
    ("WoodBlockHi", "organic", lambda r: woodblock(r, 1150.0), 0.35, 0.03),
    ("WoodBlockLo", "organic", lambda r: woodblock(r, 849.0),  0.35, 0.03),
    ("TempleBlock", "organic", temple_block,                   0.5, 0.04),
    ("SlitDrumHi",  "organic", lambda r: slit_drum(r, 330.0),  1.0, 0.04),
    ("SlitDrumLo",  "organic", lambda r: slit_drum(r, 220.0),  1.0, 0.04),
    ("BambooKnock", "organic", bamboo,                         0.45, 0.04),
    ("Coconut",     "organic", coconut,                        0.3, 0.03),
    # ethnic -- clay, metal, wire, tuned bars
    ("UduBass",     "ethnic",  udu_bass,                       1.0, 0.05),
    ("UduTone",     "ethnic",  udu_tone,                       0.5, 0.05),
    ("AgogoHi",     "ethnic",  lambda r: agogo(r, 932.33),     1.3, 0.04),
    ("AgogoLo",     "ethnic",  lambda r: agogo(r, 698.0),      1.3, 0.04),
    ("KalimbaE4",   "ethnic",  lambda r: kalimba(r, 329.63),   2.4, 0.03),
    ("KalimbaA4",   "ethnic",  lambda r: kalimba(r, 440.0),    2.4, 0.03),
    ("BalafonC4",   "ethnic",  lambda r: balafon(r, 261.63),   1.0, 0.04),
    ("BalafonG4",   "ethnic",  lambda r: balafon(r, 392.0),    1.0, 0.04),
    ("Zills",       "ethnic",  zills,                          3.0, 0.03),
    ("Berimbau",    "ethnic",  lambda r: berimbau(r, False),   0.9, 0.04),
    ("BerimbauHi",  "ethnic",  lambda r: berimbau(r, True),    0.9, 0.04),
]


# ---------------------------------------------------------------------------
# Finishing: trim, level, write
# ---------------------------------------------------------------------------

def k_weight(x):
    """ITU-R BS.1770 K-weighting at 48 kHz."""
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1.0, -1.69065929318241, 0.73248077421585]
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, -1.99004745483398, 0.99007225036621]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x))


def momentary_max_lufs(x):
    y = k_weight(np.concatenate([x, np.zeros(nsamp(0.4))]))
    w = nsamp(0.4)
    c = np.cumsum(np.concatenate([[0.0], y ** 2]))
    ms = (c[w:] - c[:-w]) / w
    return -0.691 + 10 * np.log10(np.max(ms) + 1e-20)


def finish(x, max_s, room, rng):
    x = np.asarray(x, dtype=np.float64).copy()
    k = nsamp(0.0002)                              # 0.2 ms onset ramp: no click
    x[:k] *= np.linspace(0.0, 1.0, k, endpoint=False)
    x = ambience(x, rng, amount=room)
    x = hp(x, 25.0, 2)                             # no subsonic / DC
    peak = np.max(np.abs(x)) + 1e-20
    # end where the envelope has fallen 60 dB below the peak (5 ms frames)
    h = nsamp(0.005)
    nf = len(x) // h
    env = np.sqrt(np.mean(x[: nf * h].reshape(nf, h) ** 2, axis=1))
    above = np.nonzero(env > peak * 10 ** (-60 / 20))[0]
    end = (above[-1] + 2) * h if len(above) else len(x)
    end = min(end, len(x), nsamp(min(max_s, MAX_SECONDS)))
    x = x[:end]
    fade = min(nsamp(0.04), max(1, end // 5))
    x[-fade:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade))
    # level: as loud as the loudness target allows, never over the peak ceiling
    g_peak = 10 ** (PEAK_CEILING_DB / 20) / np.max(np.abs(x))
    g_loud = 10 ** ((LOUDNESS_TARGET - momentary_max_lufs(x)) / 20)
    return (x * min(g_peak, g_loud)).astype(np.float32)


def write_wav_float(path, x):
    """48 kHz mono IEEE-float WAV with the plain 44-byte header (fmt chunk of
    16 bytes, format tag 3) -- the simplest layout every reader accepts."""
    data = np.asarray(x, dtype="<f4").tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, SR, SR * 4, 4, 32)
    hdr += b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(hdr + data)


def seed_of(name):
    return zlib.crc32(name.encode("ascii")) & 0xFFFFFFFF


def check_name(name):
    bad = set(name) - NAME_CHARS
    if bad:
        raise ValueError(f"{name}: characters not allowed by the drumlogue: {sorted(bad)}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "Samples"))
    ap.add_argument("--first-index", type=int, default=1, help="USER-bank slot of the first sample (1..128)")
    ap.add_argument("--only", nargs="*", help="render only these names")
    ap.add_argument("--list", action="store_true", help="print the slot map and exit")
    ap.add_argument("--manifest", action="store_true", help="print a markdown table of the rendered kit")
    a = ap.parse_args(argv)

    last = a.first_index + len(KIT) - 1
    if a.first_index < 1 or last > 128:
        sys.exit(f"slots {a.first_index:03d}..{last:03d} do not fit the USER bank (001..128)")

    if a.list:
        for i, (name, fam, *_r) in enumerate(KIT):
            print(f"{a.first_index + i:03d}_{name}.wav  {fam}")
        return 0

    os.makedirs(a.out, exist_ok=True)
    rows = []
    total = 0
    for i, (name, fam, fn, max_s, room) in enumerate(KIT):
        check_name(name)
        if a.only and name not in a.only:
            continue
        rng = np.random.default_rng(seed_of(name))
        x = finish(fn(rng), max_s, room, rng)
        if not np.all(np.isfinite(x)):
            sys.exit(f"{name}: non-finite samples")
        fname = f"{a.first_index + i:03d}_{name}.wav"
        write_wav_float(os.path.join(a.out, fname), x)
        size = 44 + 4 * len(x)
        total += size
        rows.append((fname, fam, len(x) / SR, 20 * np.log10(np.max(np.abs(x))), size))
        if not a.manifest:
            print(f"{fname:24s} {fam:8s} {len(x) / SR:5.2f} s  peak {rows[-1][3]:6.1f} dBFS")
    if a.manifest:
        print("| Slot | File | Family | Length |")
        print("|---:|---|---|---:|")
        for fname, fam, dur, pk, size in rows:
            print(f"| {fname[:3]} | `{fname}` | {fam} | {dur:.2f} s |")
    print(f"\n{len(rows)} samples, {total / 1e6:.2f} MB", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
