#!/usr/bin/env python3
"""
PortaCassette measurements, from test tones rendered by probe.cc.

Run through run.sh, which builds the probe for the drumlogue's Cortex-A7 and
runs it under qemu-arm:

    ./run.sh all              # everything below
    ./run.sh calib            # THD per machine at the reference level
    ./run.sh onset            # where each frequency starts to saturate
    ./run.sh alias            # inharmonic (aliased) products of a 10 kHz tone
    ./run.sh track            # dbx: tape compression vs what reaches the output
    ./run.sh burst            # a hit after silence through the compander
    ./run.sh pump             # how far a quiet treble tone moves with each kick
    ./run.sh modes            # dbx EncOnly / DecOnly against Off
    ./run.sh console          # Trim: console clipping
    ./run.sh xtalk            # channel separation
    ./run.sh noise            # noise floor
    ./run.sh table            # the README's per-machine table
    ./run.sh response         # small-signal frequency response, dbx Off / Active

Levels are sine PEAK levels in dBFS: -15 dBFS peak is -18 dBFS RMS, the
compander's reference ("0 VU").  Unless a test says otherwise, tones run with
wow & flutter off (a fixed delay) so the spectra stay clean, and through the
"_nc" build, whose output ceiling is out of reach, so the numbers describe the
tape and not the safety limiter behind it.

Requires numpy.
"""
import concurrent.futures as cf
import os
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ImportError:
    sys.exit("measure.py needs numpy: pip install numpy")

FS = 48000
NFFT = 1 << 16
QEMU = os.environ.get("QEMU", "qemu-arm")
PROBE = os.environ.get("PROBE", "probe")
PROBE_NC = os.environ.get("PROBE_NC", PROBE)
POOL = cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 2)

MODELS = {0: "T-244", 1: "T-424", 2: "T-488", 3: "Vinyl", 4: "T-424N"}
DBX = {0: "Active", 1: "EncOnly", 2: "Off", 3: "DecOnly"}


def _bh4(n):
    a = (0.35875, 0.48829, 0.14128, 0.01168)
    t = 2 * np.pi * np.arange(n) / n
    return a[0] - a[1] * np.cos(t) + a[2] * np.cos(2 * t) - a[3] * np.cos(3 * t)


WIN = _bh4(NFFT)


def render(nc=True, **kw):
    """Render with probe.cc; keyword arguments are its key=value pairs."""
    kw.setdefault("nowf", 1)
    fd, path = tempfile.mkstemp(suffix=".raw")
    os.close(fd)
    try:
        args = [QEMU, PROBE_NC if nc else PROBE, path] + [f"{k}={v}" for k, v in kw.items()]
        subprocess.run(args, check=True)
        return np.fromfile(path, dtype=np.float32).astype(np.float64)
    finally:
        os.unlink(path)


def render_many(jobs):
    return list(POOL.map(lambda kw: render(**kw), jobs))


def fold(f):
    f = f % FS
    return FS - f if f > FS / 2 else f


def band(P, f, hw=6):
    b = int(round(f * NFFT / FS))
    lo, hi = max(b - hw, 0), min(b + hw + 1, len(P))
    return P[lo:hi].sum(), lo, hi


def spectrum(x):
    seg = x[-NFFT:]
    return np.abs(np.fft.rfft(seg * WIN)) ** 2, seg


def amplitude(p1):
    """Peak amplitude of a sine whose Blackman-Harris main-lobe power is p1."""
    return np.sqrt(4 * p1 / (NFFT * (WIN ** 2).sum()))


def analyse(x, f1, kmax=300):
    """Fundamental, in-band harmonics, and aliased (inharmonic) products."""
    P, seg = spectrum(x)
    used = np.zeros(len(P), bool)
    used[: int(15 * NFFT / FS)] = True
    p1, lo, hi = band(P, f1)
    used[lo:hi] = True
    harm, alias, worst = {}, 0.0, (0.0, 0, 0.0)
    for k in range(2, kmax):
        ff = fold(k * f1)
        if ff < 15:
            continue
        p, lo, hi = band(P, ff)
        if used[lo:hi].any():
            continue
        used[lo:hi] = True
        if k * f1 < FS / 2:
            harm[k] = p
        else:
            alias += p
            if p > worst[0]:
                worst = (p, k, ff)
    hsum = sum(harm.values())
    db = lambda p: 10 * np.log10(max(p, 1e-30) / p1)
    return dict(amp=amplitude(p1), peak=np.abs(seg).max(),
                h2=db(harm.get(2, 0)), h3=db(harm.get(3, 0)), h5=db(harm.get(5, 0)),
                thd=100 * np.sqrt(hsum / p1), alias=db(alias),
                worst_alias=(db(worst[0]), worst[1], worst[2]))


def sine_fit(y, f, n0, n):
    """Least-squares amplitude of a sine at f over y[n0:n0+n], plus THD+N %."""
    seg = y[n0:n0 + n]
    t = np.arange(n0, n0 + n)
    M = np.stack([np.sin(2 * np.pi * f * t / FS), np.cos(2 * np.pi * f * t / FS), np.ones(n)], 1)
    c, *_ = np.linalg.lstsq(M, seg, rcond=None)
    fit = M[:, :2] @ c[:2]
    res = seg - M @ c
    return np.hypot(c[0], c[1]), 100 * np.sqrt((res ** 2).mean() / max((fit ** 2).mean(), 1e-30))


def dB(a):
    return 20 * np.log10(max(a, 1e-30))


# ----------------------------------------------------------------------------

def calib():
    print("THD at the reference level: 997 Hz, -15 dBFS peak (-18 dBFS RMS),")
    print("default patch (Trim 20 %, Drive 40 %), shipped build, wow & flutter on")
    spec = {0: "1.5 %", 1: "1.0 % (424 MkII)", 2: "1.3 %", 4: "-"}
    jobs = [dict(nc=False, nowf=0, secs=4, model=m, dbx=d, f1=997, a1=-15)
            for d in (0, 2) for m in (0, 1, 2, 4)]
    out = iter(render_many(jobs))
    for d in (0, 2):
        for m in (0, 1, 2, 4):
            r = analyse(next(out), 997)
            print(f"  dbx {DBX[d]:7s} {MODELS[m]:6s}: THD {r['thd']:5.2f} %  "
                  f"(H2 {r['h2']:6.1f}, H3 {r['h3']:6.1f} dBc)   published {spec[m]}")


REF_DB = -36.0


def onset(models=(0, 4)):
    print("Input level (dBFS peak) at which the fundamental is compressed by 1 dB,")
    print("Drive 40 %, Trim 0 %.  Cassette saturates its treble first.")
    freqs = (61, 211, 997, 4993, 9973)
    levels = np.arange(-36, 9.1, 1.5)
    for m in models:
        for d in (2, 0):
            # The small-signal reference sits inside the compander's 2:1 range
            # (above -42 dBFS RMS): below it the gain clamps and the decoder
            # stops doubling the tape's response, which would read as a level
            # change rather than as compression.
            jobs = [dict(model=m, dbx=d, drive=40, trim=0, f1=f, a1=REF_DB) for f in freqs]
            jobs += [dict(model=m, dbx=d, drive=40, trim=0, f1=f, a1=L) for f in freqs for L in levels]
            res = render_many(jobs)
            ref = {f: dB(analyse(res[i], f)["amp"]) - REF_DB for i, f in enumerate(freqs)}
            cells = []
            for i, f in enumerate(freqs):
                comp = []
                for j, L in enumerate(levels):
                    r = analyse(res[len(freqs) + i * len(levels) + j], f)
                    comp.append((L, ref[f] - (dB(r["amp"]) - L)))
                x = float("nan")
                for (La, ca), (Lb, cb) in zip(comp, comp[1:]):
                    if ca < 1.0 <= cb:
                        x = La + (1.0 - ca) * (Lb - La) / (cb - ca)
                        break
                cells.append(f"{x:6.1f}")
            print(f"  {MODELS[m]:6s} dbx {DBX[d]:6s}: " +
                  "  ".join(f"{f} Hz {c}" for f, c in zip(freqs, cells)))


def alias():
    print("Strongest inharmonic (aliased) product of a 9973 Hz tone at -12 dBFS peak, T-244")
    for d, drive in ((0, 40), (2, 40), (0, 100), (2, 100)):
        r = analyse(render(model=0, dbx=d, drive=drive, f1=9973, a1=-12), 9973)
        w = r["worst_alias"]
        print(f"  dbx {DBX[d]:6s} Drive {drive:3d} %: H{w[1]} -> {w[2]:6.0f} Hz at {w[0]:6.1f} dBc "
              f"(all aliases {r['alias']:6.1f} dBc)")


def track():
    print("dbx tracking: compression of a 997 Hz tone against its THD, T-244, Drive 40 %, Trim 0 %.")
    print("A 1:2 expander doubles in dB whatever level error the tape makes.")
    levels = list(range(-24, 7, 3))
    for d in (2, 0):
        res = render_many([dict(model=0, dbx=d, drive=40, trim=0, f1=997, a1=L) for L in [REF_DB] + levels])
        g0 = dB(analyse(res[0], 997)["amp"]) - REF_DB
        print(f"  dbx {DBX[d]}:")
        for L, x in zip(levels, res[1:]):
            r = analyse(x, 997)
            print(f"    {L:4d} dBFS: THD {r['thd']:5.2f} %  compression {g0 - (dB(r['amp']) - L):5.2f} dB")


def burst():
    print("997 Hz burst at -6 dBFS peak after 1 s of silence, T-244, default patch.")
    print("Level and THD+N in 2 ms windows, against the steady state.")
    for d in (2, 0):
        y = render(model=0, dbx=d, f1=997, a1=-6, start=1.0, secs=2)
        ss, ss_t = sine_fit(y, 997, 48000 + 24000, 384)
        rows = []
        for ms in (0, 2, 4, 8, 16, 32):
            a, t = sine_fit(y, 997, 48000 + ms * 48, 96)
            rows.append(f"{ms:2d} ms {dB(a / ss):+6.2f} dB {t:5.1f} %")
        print(f"  dbx {DBX[d]:6s} (steady THD+N {ss_t:4.1f} %): " + " | ".join(rows))


def pump():
    print("Kicks (55 Hz, -6 dBFS peak, 80 ms decay, every 500 ms) over a steady 8 kHz tone")
    print("at -36 dBFS, T-244, default patch: how far the 8 kHz tone moves with each kick.")
    for d in (2, 0):
        y = render(model=d and 0, dbx=d, f1=55, a1=-6, hit=0.5, tau=0.08, f2=8000, a2=-36, secs=4)
        lv = []
        for n0 in range(2 * FS, 4 * FS - 240, 240):          # 5 ms windows over 2 s
            lv.append(dB(sine_fit(y, 8000, n0, 240)[0]))
        lv = np.array(lv)
        ref = dB(10 ** (-36 / 20))
        print(f"  dbx {DBX[d]:6s}: 8 kHz level {lv.min() - ref:+6.2f} .. {lv.max() - ref:+6.2f} dB re input "
              f"(swing {lv.max() - lv.min():5.2f} dB)")


def modes():
    print("Output against dbx Off (dB), T-244, Drive 0 %, Trim 0 %")
    levels = (-45, -30, -15, -3)
    freqs = (100, 1000, 10000)
    jobs = [dict(model=0, dbx=d, drive=0, trim=0, f1=f, a1=L)
            for d in (2, 1, 3) for L in levels for f in freqs]
    res = iter(render_many(jobs))
    amp = {}
    for d in (2, 1, 3):
        for L in levels:
            for f in freqs:
                amp[d, L, f] = dB(analyse(next(res), f)["amp"])
    print("  input  |  EncOnly: 100 Hz  1 kHz  10 kHz |  DecOnly: 100 Hz  1 kHz  10 kHz")
    for L in levels:
        e = [amp[1, L, f] - amp[2, L, f] for f in freqs]
        q = [amp[3, L, f] - amp[2, L, f] for f in freqs]
        print(f"  {L:5d}  |          {e[0]:+6.1f} {e[1]:+6.1f} {e[2]:+6.1f}  |          "
              f"{q[0]:+6.1f} {q[1]:+6.1f} {q[2]:+6.1f}")


def console():
    print("Console clip: 997 Hz at -15 dBFS peak (nominal), dbx Off, Drive 0 %, against Trim")
    trims = (0, 20, 40, 60, 70, 80, 90, 100)
    res = render_many([dict(model=0, dbx=2, drive=0, trim=t, f1=997, a1=-15) for t in trims])
    for t, x in zip(trims, res):
        r = analyse(x, 997)
        print(f"  Trim {t:3d} % (+{t * 0.3:4.1f} dB): THD {r['thd']:6.2f} %  H3 {r['h3']:6.1f}  "
              f"H5 {r['h5']:6.1f} dBc  out {dB(r['amp']) + 15:+5.1f} dB")
    r = analyse(render(model=0, dbx=2, drive=0, trim=100, f1=4993, a1=-15), 4993)
    w = r["worst_alias"]
    print(f"  Trim 100 %, 4993 Hz: worst alias H{w[1]} -> {w[2]:.0f} Hz at {w[0]:.1f} dBc")


def xtalk():
    print("Channel separation, left input only, -15 dBFS peak, dbx Off, Drive 0 %")
    jobs = []
    for m in (0, 1, 2, 4):
        for xt in (1, 5):
            for f in (1000, 10000):
                jobs.append(dict(model=m, dbx=2, drive=0, xt=0, f1=f, a1=-15))
                jobs.append(dict(model=m, dbx=2, drive=0, xt=xt, f1=f, a1=-15, lonly=1, outch=1))
    res = iter(render_many(jobs))
    for m in (0, 1, 2, 4):
        cells = []
        for xt in (1, 5):
            for f in (1000, 10000):
                d, b = next(res), next(res)
                sep = dB(sine_fit(d, f, len(d) - 48000, 48000)[0]) - dB(sine_fit(b, f, len(b) - 48000, 48000)[0])
                cells.append(f"X-talk {xt} {f // 1000:2d} kHz {sep:5.1f} dB")
        print(f"  {MODELS[m]:6s}: " + " | ".join(cells))


def noise():
    print("Noise floor (dBFS RMS, 20 Hz - 20 kHz), silence in, Hiss 15 %, Age 10 %, wow & flutter on")
    jobs = [dict(nc=False, nowf=0, model=m, dbx=d, hiss=15, f1=0, secs=3)
            for m in (0, 1, 2, 4) for d in (0, 2)]
    res = iter(render_many(jobs))
    for m in (0, 1, 2, 4):
        cells = []
        for d in (0, 2):
            x = next(res)[-NFFT:]
            X = np.fft.rfft(x * np.hanning(len(x)))
            fr = np.fft.rfftfreq(len(x), 1 / FS)
            P = np.abs(X) ** 2 / (np.hanning(len(x)) ** 2).sum() * 2 / len(x)
            sel = (fr >= 20) & (fr <= 20000)
            cells.append(f"dbx {DBX[d]} {10 * np.log10(P[sel].sum() + 1e-30):6.1f}")
        print(f"  {MODELS[m]:6s}: " + "  ".join(cells))


def table():
    print("Per-machine figures at Age 10 %, dbx Off, Drive 0 % (README table)")
    for m in (0, 1, 2, 3, 4):
        lo = render(model=m, dbx=2, drive=0, f1=997, a1=-30)
        hi = render(model=m, dbx=2, drive=0, f1=14000, a1=-30)
        r14 = dB(analyse(hi, 14000)["amp"]) - dB(analyse(lo, 997)["amp"])
        # wow & flutter: instantaneous frequency of a 3150 Hz tone, unweighted
        # -40 dBFS: at higher levels the tone's own distortion products
        # disturb the phase estimate.  Vinyl's dust does too, so it is skipped.
        if m == 3:
            print(f"  {MODELS[m]:6s}: 14 kHz {r14:+5.1f} dB re 1 kHz | W&F n/a (dust)")
            continue
        y = render(model=m, dbx=2, drive=0, f1=3150, a1=-40, nowf=0, secs=4)[-3 * FS:]
        Y = np.fft.fft(y)
        Y[len(Y) // 2:] = 0
        ph = np.unwrap(np.angle(np.fft.ifft(2 * Y)))
        inst = np.diff(ph) * FS / (2 * np.pi)
        inst = inst[FS // 4:-FS // 4]
        dev = (inst - inst.mean()) / 3150.0 * 1e6
        print(f"  {MODELS[m]:6s}: 14 kHz {r14:+5.1f} dB re 1 kHz | W&F peak {np.abs(dev).max():6.0f} ppm, "
              f"RMS {dev.std():5.0f} ppm (unweighted)")


def response():
    print("Small-signal frequency response (dB re 1 kHz), T-244, Drive 40 %, Trim 0 %")
    freqs = (40, 61, 100, 211, 1000, 5000, 10000, 14000, 16000)
    for d in (2, 0):
        for L in (-15, -45):
            res = render_many([dict(model=0, dbx=d, drive=40, trim=0, f1=f, a1=L) for f in freqs])
            amps = [dB(sine_fit(x, f, len(x) - 48000, 48000)[0]) for f, x in zip(freqs, res)]
            ref = amps[freqs.index(1000)]
            print(f"  dbx {DBX[d]:6s} {L:4d} dBFS: " + "  ".join(f"{f}:{a - ref:+5.1f}" for f, a in zip(freqs, amps)))


TESTS = dict(calib=calib, onset=onset, alias=alias, track=track, burst=burst, modes=modes,
             console=console, xtalk=xtalk, noise=noise, table=table, pump=pump,
             response=response)


def main(argv):
    names = argv[1:] or ["all"]
    if names == ["all"]:
        names = list(TESTS)
    for n in names:
        if n not in TESTS:
            sys.exit(f"unknown test {n}; one of: all {' '.join(TESTS)}")
        TESTS[n]()
        print()


if __name__ == "__main__":
    main(sys.argv)

