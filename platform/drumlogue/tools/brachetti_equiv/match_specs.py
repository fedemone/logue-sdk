"""match_specs.py -- the Brachetti presets voiced after the Pelle e Legno samples.

Read by match_samples.py:  python3 match_samples.py match_specs.py tom

Each spec is a preset: `values` are its rows in the unit's tables as the
search starts (everything left out takes match_samples.py's defaults), `free`
the values the search may move, with their ranges, and `ref` the Pelle e Legno
sample it is voiced after (../../pelle_legno/Samples, rendered by
pelle_legno/render_kit.py from the same physical description).

The modal banks come from measurement, not from the search: each sample's
partials (frequency, level, T60, measured on the sample) are the bank's
modes.  Two constraints of the bank shape how they are written:
  * mode 1 sounds AT the root note (an integer MIDI note), so where a sample's
    main partial falls between notes, mode 1 is a weak partner just below it
    and the main partial is mode 2 at its exact ratio (CngOpen: 261.6 + 271 Hz);
  * the bank weights its modes 1, .6, .45, .28, .18, .12 at the output, so a
    mode's env is its measured level divided by that weight (bank() does it).
"""
import math
import os

SR = 48000
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pelle_legno", "Samples")
W = [1.0, 0.6, 0.45, 0.28, 0.18, 0.12]


def note_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def dec(t60_ms):
    """Per-sample decay for a T60."""
    return math.exp(math.log(0.001) / (t60_ms * 1e-3 * SR))


def pole(hz):
    """One-pole low-pass coefficient for a corner frequency."""
    return 1.0 - math.exp(-2.0 * math.pi * hz / SR)


def bank(root, partials, t60, mix):
    """A modal bank from measured partials [(Hz, level), ...]; the first is
    mode 1 and sounds at the root (only its level is used).  t60: modes 1-4."""
    f0 = note_hz(root)
    r = [1.0] + [hz / f0 for hz, _ in partials[1:]]
    a = [lvl / W[i] for i, (_, lvl) in enumerate(partials)]
    m = max(a)
    a = [x / m for x in a]
    n = len(partials)
    r += [0.0] * (6 - n)
    a += [0.0] * (6 - n)
    t = list(t60) + [0.0] * (4 - len(t60))
    return dict(Root=root, r2=r[1], r3=r[2], r4=r[3], r5=r[4], r6=r[5],
                e1=a[0], e2=a[1], e3=a[2], e4=a[3], e5=a[4], e6=a[5],
                t1=t[0], t2=t[1], t3=t[2], t4=t[3], count=n, mix=mix, modal_mix=mix)


def slap(gain, t60_ms, lo_hz, hi_hz):
    return dict(slap_gain=gain, slap_decay=dec(t60_ms), slap_a_lo=pole(lo_hz), slap_a_hi=pole(hi_hz))


def boom(hz, t60_ms, mix, attack_ms=3.0, glide=0.0, glide_hz=0.0, glide_ms=40.0):
    """A tracked boom at hz; glide +1 settles down onto hz from hz + glide_hz,
    -1 rises up to hz from hz - glide_hz (exponential, time constant glide_ms)."""
    d = dict(boom_hz=hz, boom_env=1.0, boom_decay=dec(t60_ms), boom_mix=mix,
             boom_attack_env=0.0, boom_attack_inc=1.0 / (attack_ms * 1e-3 * SR), glide=glide)
    if glide:
        d.update(pitch_env=1.0, pitch_decay=math.exp(-1.0 / (glide_ms * 1e-3 * SR)), pitch_amount=glide_hz)
    return d


def merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


# The values the search moves on a hand drum, and their ranges.
MEMBRANE_FREE = dict(modal_mix=(0.005, 1.5, "log"), NzMx=(0, 100, "lin"), NzRs=(50, 1000, "lin"),
                     NzFq=(30, 1500, "log"), Rel=(0, 20, "lin"), MlSt=(2, 50, "lin"),
                     t1=(20, 4000, "log"), t2=(20, 4000, "log"), onset_ms=(0, 6, "lin"))
SLAP_FREE = dict(slap_gain=(0.1, 8.0, "log"), slap_a_lo=(0.02, 0.6, "log"), slap_a_hi=(0.1, 0.95, "log"))
# Bright strokes (rim, slap) also move the noise filter's mode and the burst's
# body / click balance.
BRIGHT_FREE = dict(NzFl=(0, 2, "int"), band_mix=(0.05, 1.0, "lin"))
BOOM_FREE = dict(boom_mix=(0.02, 2.0, "log"))
# A gliding boom also moves the glide's time constant (ms) and depth (Hz).
GLIDE_FREE = dict(pitch_decay=(15, 400, "tau"), pitch_amount=(1, 120, "log"))


def free(*ds):
    return merge(*ds)


SKIN = dict(Mdl=5, Ptls=2, Dky=160, TbRd=10, Gain=0, MlRs=600, Hit=20, band_mix=0.5, split=0.3)

SPECS = {"tom": [
    # ── West Africa: djembe ────────────────────────────────────────────────
    dict(name="DjmTone", enum="k_DjembeTone", ref=os.path.join(P, "002_DjembeTone.wav"),
         note="tone: the head's 262 Hz pitch with its 0.6-1.9 kHz ring",
         values=merge(SKIN, bank(60, [(261.6, .59), (587.0, .60), (825.1, .84), (1069.0, 1.0),
                                      (1137.5, .78), (1889.6, .28)], [320, 240, 313, 483], 0.35),
                      slap(2.0, 25, 2500, 8000), dict(MlSt=40, NzMx=30, NzRs=500, NzFl=1, NzFq=400)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="DjmSlap", enum="k_DjembeSlap", ref=os.path.join(P, "003_DjembeSlap.wav"),
         note="slap: a skin crack over a 1.07 kHz ring",
         values=merge(SKIN, bank(84, [(1046.5, .08), (1065.3, 1.0), (1133.8, .10), (1289.4, .05),
                                      (1507.3, .04), (1726.7, .05)], [430, 432, 331, 216], 0.35),
                      slap(4.0, 30, 2500, 7000), dict(MlSt=48, Hit=60, NzMx=40, NzRs=500, NzFl=2, NzFq=250)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="DjmBass", enum="k_DjembeBass", ref=os.path.join(P, "001_DjembeBass.wav"),
         note="bass: the goblet's 74 Hz air, pushed by the palm, settling from 7 % sharp",
         values=merge(SKIN, bank(38, [(73.4, .10), (94.8, .07), (204.7, .04)], [370, 370, 225], 0.10),
                      boom(74.3, 370, 0.8, attack_ms=4, glide=1, glide_hz=5.2, glide_ms=30),
                      slap(1.0, 40, 200, 1800), dict(MlSt=10, MlRs=300, NzMx=10, NzRs=300, NzFq=180)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BOOM_FREE)),
    # ── the Caribbean: conga, tumba, bongo ─────────────────────────────────
    dict(name="CngOpen", enum="k_CongaOpen", ref=os.path.join(P, "004_CongaOpen.wav"),
         note="open tone at 271 Hz (mode 1 is its weak partner at the C4 root)",
         values=merge(SKIN, bank(60, [(261.6, .07), (271.0, 1.0), (325.2, .43), (464.4, .23),
                                      (643.4, .04), (509.0, .03)], [470, 474, 378, 241], 0.4),
                      dict(MlSt=35, NzMx=15, NzRs=500, NzFq=450, onset_ms=1.0)),
         free=free(MEMBRANE_FREE)),
    dict(name="CngMute", enum="k_CongaMute", ref=os.path.join(P, "005_CongaMute.wav"),
         note="muted tone: 657 Hz, closed by the hand",
         values=merge(SKIN, bank(76, [(659.3, 1.0), (626.6, .35), (718.5, .23), (1070.1, .48),
                                      (1042.6, .27), (574.6, .18)], [65, 55, 50, 45], 0.4),
                      dict(MlSt=40, NzMx=15, NzRs=500, NzFq=600)),
         free=free(MEMBRANE_FREE, BRIGHT_FREE)),
    dict(name="CngSlap", enum="k_CongaSlap", ref=os.path.join(P, "006_CongaSlap.wav"),
         note="open slap: the head's edge-mode cloud under a skin crack",
         values=merge(SKIN, bank(72, [(523.3, .55), (698.0, .69), (728.0, 1.0), (789.6, .97),
                                      (843.0, .64), (1040.0, .73)], [180, 214, 160, 149], 0.35),
                      slap(3.0, 35, 1500, 8000), dict(MlSt=48, Hit=60, NzMx=35, NzRs=500, NzFl=2, NzFq=150)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="CngHeel", enum="k_CongaHeel", ref=os.path.join(P, "007_CongaHeel.wav"),
         note="heel of the palm: a closed low thud",
         values=merge(SKIN, bank(51, [(155.6, 1.0), (107.7, .31), (181.3, .21), (78.7, .23),
                                      (66.3, .23), (318.2, .07)], [110, 110, 100, 90], 0.4),
                      dict(MlSt=8, MlRs=300, Hit=0, NzMx=10, NzRs=300, NzFq=150)),
         free=free(MEMBRANE_FREE)),
    dict(name="Tumba", enum="k_Tumba", ref=os.path.join(P, "008_TumbaOpen.wav"),
         note="the big conga's open tone, G3",
         values=merge(SKIN, bank(55, [(196.0, 1.0), (234.7, .27), (335.1, .24), (361.8, .10),
                                      (180.2, .07), (465.1, .06)], [458, 407, 352, 331], 0.4),
                      dict(MlSt=35, NzMx=15, NzRs=500, NzFq=400, onset_ms=1.0)),
         free=free(MEMBRANE_FREE)),
    dict(name="BngMach", enum="k_BongoMacho", ref=os.path.join(P, "009_BongoMacho.wav"),
         note="the small bongo",
         values=merge(SKIN, bank(70, [(466.2, .40), (607.2, .17), (651.5, 1.0), (817.0, .21),
                                      (962.4, .25), (993.9, .31)], [224, 185, 197, 198], 0.35),
                      dict(MlSt=42, NzMx=20, NzRs=500, NzFq=800)),
         free=free(MEMBRANE_FREE, BRIGHT_FREE)),
    dict(name="BngHmbr", enum="k_BongoHembra", ref=os.path.join(P, "010_BongoHembra.wav"),
         note="the large bongo",
         values=merge(SKIN, bank(69, [(440.0, .59), (469.5, .68), (533.2, .73), (588.9, .86),
                                      (692.1, 1.0), (807.5, .67)], [238, 228, 242, 206], 0.35),
                      dict(MlSt=40, NzMx=20, NzRs=500, NzFq=800)),
         free=free(MEMBRANE_FREE, BRIGHT_FREE)),
    # ── India: tabla (dayan, bayan) ────────────────────────────────────────
    dict(name="TblaNa", enum="k_TablaNa", ref=os.path.join(P, "011_TablaNa.wav"),
         note="Na: the dayan's harmonic series on C#4, 2nd and 3rd harmonics ringing",
         values=merge(SKIN, dict(Mdl=0), bank(61, [(277.2, .08), (554.4, 1.0), (831.5, .70),
                                                   (1108.7, .18), (1385.9, .13), (1663.1, .12)],
                                              [150, 2300, 1900, 1200], 0.35),
                      slap(2.5, 120, 1000, 5000), dict(MlSt=45, Hit=60, NzMx=20, NzRs=500, NzFl=1, NzFq=250)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="TblaTin", enum="k_TablaTin", ref=os.path.join(P, "012_TablaTin.wav"),
         note="Tin: harmonics of A#3, the 3rd strongest",
         values=merge(SKIN, dict(Mdl=0), bank(58, [(233.1, .9), (466.2, .76), (699.2, 1.0),
                                                   (932.3, .29), (1165.4, .45), (1398.5, .19)],
                                              [120, 2400, 2800, 1500], 0.35),
                      slap(2.0, 150, 900, 3200), dict(MlSt=42, Hit=50, NzMx=15, NzRs=500, NzFl=1, NzFq=200)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="TblaTun", enum="k_TablaTun", ref=os.path.join(P, "013_TablaTun.wav"),
         note="Tun: the open centre stroke, the C4 fundamental rings",
         values=merge(SKIN, dict(Mdl=0), bank(60, [(261.6, 1.0), (523.3, .26), (784.9, .16),
                                                   (1046.5, .05)], [1145, 703, 781, 510], 0.35),
                      dict(MlSt=30, Hit=10, NzMx=8, NzRs=400, NzFq=250)),
         free=free(MEMBRANE_FREE)),
    dict(name="TblaGe", enum="k_TablaGe", ref=os.path.join(P, "014_TablaGe.wav"),
         note="Ge: the bayan's open bass on G2, settling from 6 % sharp",
         values=merge(SKIN, dict(Mdl=0), bank(43, [(98.0, .10), (198.1, .45), (295.9, .22),
                                                   (387.1, .10), (480.2, .05)], [500, 500, 350, 220], 0.25),
                      boom(98.0, 750, 0.8, attack_ms=3, glide=1, glide_hz=5.9, glide_ms=50),
                      dict(MlSt=15, MlRs=400, Hit=20, NzMx=8, NzRs=300, NzFq=150)),
         free=free(MEMBRANE_FREE, BOOM_FREE)),
    dict(name="TblGeBd", enum="k_TablaGeBend", ref=os.path.join(P, "015_TablaGeBend.wav"),
         note="Ge with the heel sliding toward the syahi: the bass bends up ~5 semitones",
         values=merge(SKIN, dict(Mdl=0), bank(43, [(98.0, .10), (198.1, .30), (295.9, .10)],
                                              [150, 150, 120], 0.15),
                      boom(133.3, 750, 0.8, attack_ms=3, glide=-1, glide_hz=35.3, glide_ms=90),
                      dict(MlSt=15, MlRs=400, Hit=20, NzMx=8, NzRs=300, NzFq=150)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(80, 150)),
    dict(name="TblaKe", enum="k_TablaKe", ref=os.path.join(P, "016_TablaKe.wav"),
         note="Ke: a flat palm on the bayan, left there",
         values=merge(SKIN, bank(43, [(98.0, 1.0), (89.0, .64), (73.2, .61), (121.2, .57),
                                      (164.8, .72), (193.0, .57)], [60, 60, 60, 55], 0.3),
                      slap(1.5, 40, 300, 3000), dict(MlSt=8, MlRs=300, Hit=0, NzMx=25, NzRs=300, NzFq=200)),
         free=free(MEMBRANE_FREE, SLAP_FREE)),
    # ── Middle East: darbuka ───────────────────────────────────────────────
    dict(name="DrbDoum", enum="k_DarbukaDoum", ref=os.path.join(P, "017_DarbukaDoum.wav"),
         note="doum: the goblet's 96 Hz air under the head",
         values=merge(SKIN, bank(43, [(98.0, .05), (227.8, .15), (243.9, .05), (358.5, .03),
                                      (506.8, .03)], [450, 345, 373, 233], 0.2),
                      boom(95.9, 460, 0.8, attack_ms=3, glide=1, glide_hz=4.8, glide_ms=30),
                      dict(MlSt=20, MlRs=400, Hit=10, NzMx=10, NzRs=300, NzFq=200)),
         free=free(MEMBRANE_FREE, BOOM_FREE)),
    dict(name="DrbTek", enum="k_DarbukaTek", ref=os.path.join(P, "018_DarbukaTek.wav"),
         note="tek: the ring finger on the rim, the bright edge modes ring",
         values=merge(SKIN, bank(84, [(1046.5, .10), (1011.5, .67), (1060.9, 1.0), (1234.9, .64),
                                      (1352.8, .79), (1619.0, 1.0)], [300, 299, 386, 322], 0.35),
                      slap(3.0, 15, 2500, 11000), dict(MlSt=50, Hit=70, NzMx=30, NzRs=500, NzFl=2, NzFq=250)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="DrbKa", enum="k_DarbukaKa", ref=os.path.join(P, "019_DarbukaKa.wav"),
         note="ka: the other hand's rim stroke, a softer tek",
         values=merge(SKIN, bank(84, [(1046.5, .65), (1011.8, 1.0), (774.5, .51), (807.1, .64),
                                      (1213.6, .76), (1352.8, .51)], [290, 284, 293, 328], 0.35),
                      slap(1.5, 15, 2500, 11000), dict(MlSt=45, Hit=60, NzMx=20, NzRs=500, NzFl=2, NzFq=250)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    # ── West Africa: talking drum; Brazil: surdo, tamborim; Nigeria: udu ───
    dict(name="TalkUp", enum="k_TalkUp", ref=os.path.join(P, "025_TalkDrumUp.wav"),
         note="talking drum squeezed as it rings: D3 up about a fifth",
         values=merge(SKIN, bank(50, [(146.8, .2), (212.4, .4), (275.8, .3)], [80, 80, 70], 0.15),
                      boom(225.0, 970, 0.8, attack_ms=2, glide=-1, glide_hz=75.0, glide_ms=60),
                      dict(MlSt=35, Hit=30, NzMx=12, NzRs=400, NzFq=350)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(120, 260)),
    dict(name="TalkDn", enum="k_TalkDown", ref=os.path.join(P, "026_TalkDrumDn.wav"),
         note="talking drum released as it rings: down about a fifth to D3",
         values=merge(SKIN, bank(50, [(146.8, .2), (212.4, .4), (275.8, .3)], [80, 80, 70], 0.15),
                      boom(146.0, 970, 0.8, attack_ms=2, glide=1, glide_hz=71.5, glide_ms=110),
                      dict(MlSt=35, Hit=30, NzMx=12, NzRs=400, NzFq=350)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(120, 260)),
    dict(name="SurdOpn", enum="k_SurdoOpen", ref=os.path.join(P, "027_SurdoOpen.wav"),
         note="surdo, felt mallet, A#1: a long 58 Hz boom settling from 6 % sharp",
         values=merge(SKIN, bank(34, [(58.3, .10), (90.5, .12), (128.2, .23), (160.8, .16),
                                      (191.5, .10), (228.5, .03)], [1077, 1066, 884, 817], 0.25),
                      boom(58.6, 1080, 0.9, attack_ms=4, glide=1, glide_hz=3.5, glide_ms=40),
                      dict(MlSt=12, MlRs=350, Hit=20, NzMx=8, NzRs=300, NzFq=150)),
         free=free(MEMBRANE_FREE, BOOM_FREE)),
    dict(name="SurdMut", enum="k_SurdoMute", ref=os.path.join(P, "028_SurdoMute.wav"),
         note="the surdo damped by the hand",
         values=merge(SKIN, bank(34, [(58.3, .10), (71.0, .45), (94.8, .41), (130.0, .60),
                                      (169.6, .20), (152.7, .09)], [250, 256, 246, 249], 0.3),
                      boom(57.5, 250, 0.9, attack_ms=4, glide=1, glide_hz=3.5, glide_ms=40),
                      dict(MlSt=12, MlRs=350, Hit=20, NzMx=8, NzRs=300, NzFq=150)),
         free=free(MEMBRANE_FREE, BOOM_FREE)),
    dict(name="Tambrim", enum="k_Tamborim", ref=os.path.join(P, "029_Tamborim.wav"),
         note="tamborim: a small tight head under a thin stick",
         values=merge(SKIN, bank(73, [(554.4, .68), (574.2, .86), (886.2, 1.0), (902.0, .66),
                                      (1190.6, .76), (1267.8, .76)], [180, 160, 140, 130], 0.35),
                      slap(2.5, 12, 2000, 12000), dict(MlSt=50, Hit=50, NzMx=25, NzRs=500, NzFl=2, NzFq=300)),
         free=free(MEMBRANE_FREE, SLAP_FREE, BRIGHT_FREE)),
    dict(name="UduBass", enum="k_UduBass", ref=os.path.join(P, "051_UduBass.wav"),
         note="udu: the palm over the side hole, the pot's air rising 85 -> 112 Hz",
         values=merge(SKIN, bank(45, [(110.0, .05), (201.0, .03)], [300, 200], 0.05),
                      boom(114.2, 610, 0.9, attack_ms=5, glide=-1, glide_hz=26.0, glide_ms=50),
                      dict(MlSt=8, MlRs=300, Hit=0, NzMx=15, NzRs=300, NzFq=90)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(70, 130)),
]}
