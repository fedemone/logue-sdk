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
| 2 | Depth | 0-100% | Arrival-time spread between clones |
| 3 | Rate | 0.0-10.0 Hz | Wobble rate / decorrelation motion |
| 4 | Spread | 0-100% | Stereo width of the ensemble |
| 5 | Mix | 0-100% | Wet/dry blend |
| 6 | Wobble | 0-100% | Micro detune / timing wobble depth |
| 7 | Scatter | 0-100% | Detachment / chaos / ensemble looseness |
| 8 | SoftAtk | 0-100% | Softens later clone attacks |

### Mode behavior

- **Tribal**: circular placement, warm, ring-like
- **Military**: linear placement, tighter, more ordered
- **Angel**: scattered placement, diffuse, airy

### Spatial grammar

Each mode controls three things at once:

1. **Pan law**
   - Tribal uses a curved arc with a softer power law
   - Military uses a straighter and tighter law
   - Angel uses a wider, more diffuse law

2. **Clone placement**
   - Tribal distributes players around a stereo ring
   - Military arranges them in a row
   - Angel scatters them per hit

3. **Stereo scatter**
   - Tribal adds gentle motion
   - Military keeps scatter minimal
   - Angel injects stronger random spatial variation

### Scatter control

Scatter is the new detachment parameter.

It increases:
- time jitter
- spatial randomness
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
