# delay_tribal

> **Disclaimer:** delay_tribal is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

## Percussion ensemble design

The effect is not a classic chorus. It is a **micro-ensemble** that turns a single percussion hit into the perception of multiple drummers.

### Core idea

The dry hit remains the leader. The delayed copies are followers that are:

- later in time
- quieter
- darker / narrower in spectrum
- slightly decorrelated per hit

This prevents the sound from collapsing into muddy chorus smear.

### Clone philosophy

The current design uses a **five-step clone family**:

- 2
- 4
- 6
- 8
- 10

Ten is the upper density limit, not the default texture. For percussion, the ear reads onset spacing and spectral thinning more strongly than dense modulation.

### Parameter table

| ID | Name | Range | Role |
|----|------|-------|------|
| 0 | Clones | 0-4 | Selects 2 / 4 / 6 / 8 / 10 clones |
| 1 | Mode | 0-2 | 0=Tribal, 1=Military, 2=Angel |
| 2 | Rate | 0.0-10.0 Hz | Wobble rate / decorrelation motion |
| 3 | Spread | 0-100% | Stereo width of the ensemble |
| 4 | Wobble | 0-100% | Micro detune / timing wobble depth |
| 5 | Scatter | 0-100% | Detachment / chaos / ensemble looseness |
| 6 | SoftAtk | 0-100% | Softens later clone attacks |
| 7 | Gap | 0-100% | Arrival spacing between clones |

**Depth and Mix were removed.** Both are fixed at 100% internally: the arrival
spread is always at its widest, and the unit is always fully wet. There is no
dry path — the first clone *is* the leading stroke, which is why it runs with an
essentially open lowpass while the followers darken behind it.

### Mode behavior

- **Tribal**: a ring of players, warm, swaying gently per hit
- **Military**: a row across the front, fixed, ordered
- **Angel**: every voice somewhere new on every hit, diffuse, airy

### Spatial grammar

Each mode controls three things at once:

1. **Clone placement**
   - Tribal seats the players evenly around a ring. Stereo carries only the
     lateral part of a position, so seat *k* sits at sin(θ*k*) with the angles
     evenly spaced over -90..+90 degrees: denser toward the sides than
     Military's row.
   - Military seats them evenly along a row across the front.
   - Angel gives every voice a fresh random position on every hit: a random
     seat, then a uniform offset inside it. Each voice is uniform over the
     width, and every hit still covers the width rather than clumping.

   In Tribal and Military the seats are dealt in arrival order from the centre
   outward, alternating left and right, so the louder early clones sit near
   the middle. Spread scales every formation toward the centre.

2. **Stereo scatter** (per hit)
   - Tribal: Scatter sways each player along the ring, at most halfway to its
     neighbour at Scatter=100, so the ring keeps its shape and its order
   - Military: none — the row never breaks rank
   - Angel: the placement is the randomness; Scatter does not change it

   In every mode Scatter also loosens timing and level (below). The layout
   moves only on a detected hit: turning a knob never re-rolls it.

3. **Pan law**
   Constant power in every mode. The exponent warps position into level
   difference: Military 1.12 is the most decisive, Tribal 0.90 softer, Angel
   0.78 the softest, which pulls Angel's random positions toward the centre.

### Scatter control

Scatter is the new detachment parameter.

It increases:
- time jitter
- spatial randomness (Tribal's sway along the ring)
- follower looseness
- perceived distance between the players

It is the control that turns a tight flam into a more chaotic crowd-like drumming feel.

### Why it works

A convincing ensemble needs:
- a clear leader
- followers with staggered onsets
- progressively darker followers
- little or no feedback
- subtle random variation per hit

That produces the feeling of a group of players, not a chorus effect.

## Building

Standard logue-SDK drumlogue build (`make` in this directory, or the SDK
Docker builder).

**Optimisation level.** `config.mk` sets `OPTIM = -O3` (the SDK default is
`-Os`): 11% fewer ARM instructions per 64-frame render (25.4k -> 22.7k, measured
under `qemu-arm`), output identical to -143 dB. Objects depend on `config.mk`, so changing it
rebuilds them; a tree built before that needs one clean build. To check what
a `.drmlgunit` was built with:

```sh
strings delay_tribal.drmlgunit | grep "build:"
```
