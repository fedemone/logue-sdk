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
# A bending boom also moves the S-curve's hold and length (ms) and its depth (Hz).
GLIDE_FREE = dict(glide_hold_ms=(0, 80, "lin"), glide_ms=(30, 600, "log"), pitch_amount=(1, 120, "log"))


def bend(hz_from, hz_to, hold_ms, len_ms):
    """A held, then S-shaped bend from hz_from to hz_to (brc::GlideCurve):
    the tracked boom rests on hz_to, so glide is +1 for a fall, -1 for a rise."""
    return dict(boom_hz=hz_to, glide=1.0 if hz_from > hz_to else -1.0, pitch_env=1.0,
                pitch_decay=1.0, pitch_amount=abs(hz_from - hz_to),
                glide_hold_ms=hold_ms, glide_ms=len_ms)


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
                      boom(129.4, 750, 0.8, attack_ms=3), bend(98.0, 129.4, 40, 260),
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
                      boom(217.5, 970, 0.8, attack_ms=2), bend(150.0, 217.5, 20, 180),
                      dict(MlSt=35, Hit=30, NzMx=12, NzRs=400, NzFq=350)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(120, 260)),
    dict(name="TalkDn", enum="k_TalkDown", ref=os.path.join(P, "026_TalkDrumDn.wav"),
         note="talking drum released as it rings: down about a fifth to D3",
         values=merge(SKIN, bank(50, [(146.8, .2), (212.4, .4), (275.8, .3)], [80, 80, 70], 0.15),
                      boom(150.0, 970, 0.8, attack_ms=2), bend(217.5, 150.0, 20, 200),
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
                      boom(112.0, 610, 0.9, attack_ms=5), bend(89.6, 112.0, 20, 120),
                      dict(MlSt=8, MlRs=300, Hit=0, NzMx=15, NzRs=300, NzFq=90)),
         free=free(MEMBRANE_FREE, BOOM_FREE, GLIDE_FREE), track=(70, 130)),
]}


# ── BrcBar: wood, bamboo, clay and the kalimba's tine ───────────────────────
BAR = dict(Mdl=1, Ptls=2, Dky=160, TbRd=10, Gain=0, MlRs=800, Hit=0, Mtr=0)
BAR_FREE = dict(modal_mix=(0.02, 1.5, "log"), NzMx=(0, 100, "lin"), NzRs=(50, 1000, "lin"),
                NzFq=(30, 1800, "log"), Rel=(0, 20, "lin"), MlSt=(2, 50, "lin"),
                MlRs=(100, 1000, "lin"), t1=(20, 5000, "log"), t2=(15, 3000, "log"),
                onset_ms=(0, 3, "lin"))
SPECS["bar"] = [
    dict(name="WdBlkHi", enum="k_WoodBlockHi", ref=os.path.join(P, "044_WoodBlockHi.wav"),
         note="high wood block: 1.15 kHz over a 1.11 kHz partner",
         values=merge(BAR, bank(85, [(1108.7, .38), (1152.5, 1.0), (1230.5, .2), (2955.0, .07),
                                     (5106.0, .07)], [200, 190, 90, 50], 0.2),
                      dict(MlSt=45, NzMx=5, NzRs=420, NzFq=900)), free=BAR_FREE),
    dict(name="TmplBlk", enum="k_TempleBlock", ref=os.path.join(P, "046_TempleBlock.wav"),
         note="temple block (mokugyo): a hollow carved wood bell around C5",
         values=merge(BAR, bank(72, [(523.3, 1.0), (417.5, .31), (487.4, .13), (790.0, .35),
                                     (1209.0, .18), (1883.0, .06)], [270, 232, 257, 120], 0.2),
                      dict(MlSt=40, NzMx=5, NzRs=420, NzFq=800)), free=BAR_FREE),
    dict(name="SltDrHi", enum="k_SlitDrumHi", ref=os.path.join(P, "047_SlitDrumHi.wav"),
         note="log-drum tongue, E4, rubber mallet, over the trunk's cavity",
         values=merge(BAR, bank(64, [(329.6, 1.0), (164.8, .25), (962.5, .40), (1269.0, .22),
                                     (2011.0, .10), (2868.0, .05)], [717, 300, 220, 120], 0.2),
                      dict(MlSt=20, NzMx=5, NzRs=300, NzFq=300)), free=BAR_FREE),
    dict(name="SltDrLo", enum="k_SlitDrumLo", ref=os.path.join(P, "048_SlitDrumLo.wav"),
         note="log-drum tongue, A3",
         values=merge(BAR, bank(57, [(220.0, 1.0), (110.0, .25), (642.4, .40), (847.0, .22),
                                     (1342.0, .10), (1914.0, .05)], [693, 300, 220, 120], 0.2),
                      dict(MlSt=20, NzMx=5, NzRs=300, NzFq=300)), free=BAR_FREE),
    dict(name="Bamboo", enum="k_Bamboo", ref=os.path.join(P, "049_BambooKnock.wav"),
         note="bamboo tube knocked with a stick: odd air-column harmonics over the wall",
         values=merge(BAR, bank(64, [(329.6, .09), (340.9, 1.0), (1022.7, .35), (1450.0, .55),
                                     (1704.5, .15), (3480.0, .2)], [184, 184, 100, 60], 0.2),
                      dict(MlSt=45, NzMx=8, NzRs=420, NzFq=900)), free=BAR_FREE),
    dict(name="Coconut", enum="k_Coconut", ref=os.path.join(P, "050_Coconut.wav"),
         note="two coconut half-shells clapped: a hollow clop",
         values=merge(BAR, bank(74, [(587.3, .51), (544.2, .60), (562.9, .59), (613.0, .24),
                                     (1181.4, 1.0), (1205.2, .26)], [250, 250, 280, 300], 0.2),
                      dict(MlSt=40, NzMx=8, NzRs=420, NzFq=700)), free=BAR_FREE),
    dict(name="UduTone", enum="k_UduTone", ref=os.path.join(P, "052_UduTone.wav"),
         note="udu: a fingertip on the clay belly over the pot's 112 Hz air",
         values=merge(BAR, bank(45, [(110.0, 1.0), (410.9, .66), (797.0, .35), (1169.0, .2),
                                     (1681.0, .1)], [317, 184, 90, 60], 0.2),
                      dict(MlSt=30, NzMx=5, NzRs=300, NzFq=400)), free=BAR_FREE),
    dict(name="KlmbE4", enum="k_KalimbaE4", ref=os.path.join(P, "055_KalimbaE4.wav"),
         note="kalimba tine, E4: a clamped-free bar, its 'ping' 6.37x up",
         values=merge(BAR, bank(64, [(329.6, 1.0), (2099.7, .9), (5867.0, .08)], [3200, 300, 80], 0.15),
                      dict(MlSt=39, NzMx=3, NzRs=260, NzFq=720)), free=BAR_FREE),
    dict(name="KlmbA4", enum="k_KalimbaA4", ref=os.path.join(P, "056_KalimbaA4.wav"),
         note="kalimba tine, A4",
         values=merge(BAR, bank(69, [(440.0, 1.0), (2802.8, .9), (7832.0, .08)], [3200, 300, 80], 0.15),
                      dict(MlSt=39, NzMx=3, NzRs=260, NzFq=720)), free=BAR_FREE),
]

# ── BrcPlate: cone bells and finger cymbals ─────────────────────────────────
PLATE = dict(Mdl=4, Ptls=2, Dky=175, TbRd=10, Gain=0, MlRs=900, Hit=0, Mtr=20, noise_gain=5.0)
PLATE_FREE = dict(modal_mix=(0.02, 1.5, "log"), NzMx=(0, 100, "lin"), NzRs=(50, 1000, "lin"),
                  NzFq=(30, 1800, "log"), MlSt=(2, 50, "lin"), t1=(50, 5000, "log"),
                  t2=(30, 5000, "log"), chirp_hz=(100, 6000, "log"), shimmer_mix=(0.0, 0.5, "lin"))
AGOGO = [(1.0, 1.0), (1.006, .22), (2.47, .40), (3.92, .25), (5.43, .12), (7.1, .06)]
SPECS["plate"] = [
    dict(name="AgogoHi", enum="k_AgogoHi", ref=os.path.join(P, "053_AgogoHi.wav"),
         note="agogo, the high cone bell, A#5",
         values=merge(PLATE, bank(82, [(932.3 * r, a) for r, a in AGOGO], [1100, 1000, 550, 350], 0.2),
                      dict(MlSt=45, NzMx=10, NzRs=300, NzFq=1200, chirp_hz=900)), free=PLATE_FREE),
    dict(name="AgogoLo", enum="k_AgogoLo", ref=os.path.join(P, "054_AgogoLo.wav"),
         note="agogo, the low cone bell, F5",
         values=merge(PLATE, bank(77, [(698.5 * r, a) for r, a in AGOGO], [1100, 1000, 550, 350], 0.2),
                      dict(MlSt=45, NzMx=10, NzRs=300, NzFq=1200, chirp_hz=700)), free=PLATE_FREE),
    dict(name="Zills", enum="k_Zills", ref=os.path.join(P, "059_Zills.wav"),
         note="finger cymbals clashed: slowly beating partials from 2.5 kHz",
         values=merge(PLATE, bank(99, [(2489.0, .65), (2470.1, 1.0), (3610.1, .41), (3639.8, .14),
                                       (5289.9, .18), (6420.0, .1)], [2300, 2500, 2300, 2500], 0.2),
                      dict(MlSt=50, NzMx=15, NzRs=300, NzFq=1500, chirp_hz=2500, shimmer_mix=0.1,
                           shimmer_decay=0.9998)), free=PLATE_FREE),
]


# ── BrcNoise: seeds, beads and the guiro's ridges ───────────────────────────
# The noise unit has no body: a filtered noise burst, and a gate (an enveloped
# LFO on its level) for grains and ridges.  The rattles' accents land in their
# first ~20 ms, as the samples' do.  Not here: the seed pods and the guiro.
# Both were searched and stayed far from their samples (score ~1.0): the pods'
# few large random clicks and the guiro's 1 kHz gourd ringing under its ridge
# clicks need a body or random grains, which this engine does not have.
NOISE = dict(MlRs=500, MlSt=40, Mdl=6, Hit=50, Rel=18, Gain=5, NzMx=95, NzRs=900, NzFl=1,
             NzFq=550, Root=84, Dky=20, band_mix=0.7, split=0.5)
NOISE_FREE = dict(NzRs=(100, 1000, "lin"), NzFq=(50, 1999, "log"), NzFl=(0, 2, "int"),
                  Rel=(0, 20, "lin"), band_mix=(0.05, 1.0, "lin"), split=(0.05, 0.95, "lin"),
                  attack_rate=(0.0005, 0.9, "log"))
GATE_FREE = dict(gate_hz=(5, 300, "log"), gate_depth=(0.0, 0.98, "lin"))
SPECS["noise"] = [
    dict(name="ShkFwd", enum="k_ShakerFwd", ref=os.path.join(P, "033_ShakerFwd.wav"),
         note="shaker, strong stroke: the seeds hit the end wall on the beat",
         values=merge(NOISE, dict(NzFq=650, attack_rate=0.3, gate_hz=30, gate_depth=0.5, gate_decay=0.9995)),
         free=free(NOISE_FREE, GATE_FREE)),
    dict(name="ShkBack", enum="k_ShakerBack", ref=os.path.join(P, "034_ShakerBack.wav"),
         note="shaker, weak stroke: a softer hit, more slide",
         values=merge(NOISE, dict(NzFq=600, attack_rate=0.05, gate_hz=25, gate_depth=0.7, gate_decay=0.9998)),
         free=free(NOISE_FREE, GATE_FREE)),
    dict(name="Maracas", enum="k_Maracas", ref=os.path.join(P, "035_Maracas.wav"),
         note="a pair of maracas: a bright seed cloud",
         values=merge(NOISE, dict(NzFl=2, NzFq=400, attack_rate=0.02, gate_hz=40, gate_depth=0.3, gate_decay=0.9995)),
         free=free(NOISE_FREE, GATE_FREE)),
    dict(name="Caxixi", enum="k_Caxixi", ref=os.path.join(P, "036_Caxixi.wav"),
         note="caxixi: seeds on the basket's hard gourd base, a sharp chk",
         values=merge(NOISE, dict(NzFq=400, NzRs=600, Rel=16, attack_rate=0.5)),
         free=free(NOISE_FREE)),
    dict(name="Cabasa", enum="k_Cabasa", ref=os.path.join(P, "037_Cabasa.wav"),
         note="cabasa: a steel bead chain twisted on a ridged cylinder",
         values=merge(NOISE, dict(NzFl=2, NzFq=450, attack_rate=0.05, gate_hz=60, gate_depth=0.4, gate_decay=0.9995)),
         free=free(NOISE_FREE, GATE_FREE)),
]

# ── BrcSnare: frame drum and cajón, whose snares are their identity ─────────
SNR = dict(Mdl=5, Ptls=2, Dky=168, TbRd=8, Gain=5, MlRs=300, MlSt=28, Hit=40, Rel=9,
           NzMx=50, NzRs=900, NzFl=2, NzFq=300, band_mix=0.45, split=0.85, wire_mix=0.6,
           wire_onset_env=1.0, wire_onset_attack=0.0026, wire_a1=1.76, wire_a2=0.918)
SNR_FREE = dict(modal_mix=(0.02, 1.5, "log"), NzMx=(0, 100, "lin"), NzRs=(100, 1000, "lin"),
                NzFq=(30, 1999, "log"), Rel=(0, 20, "lin"), MlSt=(2, 50, "lin"),
                t1=(30, 3000, "log"), t2=(30, 3000, "log"), wire_mix=(0.0, 1.0, "lin"))
SPECS["snare"] = [
    dict(name="BndDoum", enum="k_BendirDoum", ref=os.path.join(P, "020_BendirDoum.wav"),
         note="bendir, centre: a large thin head on 88 Hz over its gut snares",
         values=merge(SNR, bank(41, [(87.3, .89), (140.6, .58), (202.1, 1.0), (258.5, .29),
                                     (317.5, .10), (372.8, .04)], [464, 458, 431, 390], 0.3),
                      dict(MlSt=15, Hit=20)), free=SNR_FREE),
    dict(name="BndTek", enum="k_BendirTek", ref=os.path.join(P, "021_BendirTek.wav"),
         note="bendir, edge: the head's upper modes, the snares lighter",
         values=merge(SNR, bank(66, [(370.0, .6), (406.5, 1.0), (431.8, .5), (487.1, .71),
                                     (524.4, .59), (547.1, .72)], [330, 360, 347, 347], 0.3),
                      dict(MlSt=40, Hit=70, wire_mix=0.35)), free=SNR_FREE),
    dict(name="CajBass", enum="k_CajonBass", ref=os.path.join(P, "023_CajonBass.wav"),
         note="cajon bass: the box's port air and the tapa's low modes",
         values=merge(SNR, bank(40, [(82.4, 1.0), (95.2, .31), (116.5, .34), (139.5, .23),
                                     (58.6, .15)], [292, 298, 231, 224], 0.35),
                      dict(MlSt=8, MlRs=200, Hit=0, NzMx=20, wire_mix=0.1)), free=SNR_FREE),
    dict(name="CajSlap", enum="k_CajonSlap", ref=os.path.join(P, "024_CajonSlap.wav"),
         note="cajon slap: the tapa's edge with the snare wires",
         values=merge(SNR, bank(65, [(349.2, .63), (378.3, .29), (569.5, .27), (590.3, 1.0),
                                     (791.7, .71), (828.4, .22)], [250, 272, 182, 166], 0.3),
                      dict(MlSt=45, Hit=70, wire_mix=0.7)), free=SNR_FREE),
]
