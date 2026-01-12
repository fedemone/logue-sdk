# RipplerX Audio Fix - Quick Reference

## The Three Bugs (All Fixed ✅)

### 1️⃣ Loop Iteration Bug
```cpp
// BEFORE (filled only half the buffer):
for (size_t frame = 0; frame < frames; frame += 2)  ❌

// AFTER (fills complete buffer):
for (size_t frame = 0; frame < frames/2; frame++)  ✅
```
**Impact**: Silence or dropouts from half-empty buffer

---

### 2️⃣ Mono Sample Loading Bug
```cpp
// BEFORE (loaded 1 sample, needed 4):
float32_t mono_sample = m_samplePointer[m_sampleIndex];
audioIn = vcombine_f32(vdup_n_f32(mono_sample), vdup_n_f32(mono_sample));  ❌

// AFTER (loads 2 samples correctly):
float32_t m1 = m_samplePointer[m_sampleIndex];
float32_t m2 = m_samplePointer[m_sampleIndex + 1];
audioIn = vcombine_f32(vdup_n_f32(m1), vdup_n_f32(m2));  ✅
```
**Impact**: Incorrect stereo sample playback, potential noise

---

### 3️⃣ Frame Counter Overflow Bug  
```cpp
// BEFORE (added 64 × 32 = 2048 per render):
for (size_t frame = 0; frame < frames/2; frame++) {
    voice.m_framesSinceNoteOn += frames;  // ❌ INSIDE loop
}

// AFTER (adds 2 × 32 = 64 correctly):
for (size_t frame = 0; frame < frames/2; frame++) {
    voice.m_framesSinceNoteOn += 2;  // ✅ Per iteration
}
```
**Impact**: Broken voice stealing, envelope corruption

---

## What Was Wrong (The Silence Cause)

✅ Buffer was cleared correctly (memset in unit.cc)  
✅ Render loop structure was mostly correct  
❌ **Only processed half the buffer** → silent alternating frames  
❌ **Frame counter overflowed** → voices "died" instantly  
❌ **Mono samples loaded wrong** → potential corruption  

---

## What You Need To Do

### Build & Test
```bash
# In workspace root
cd docker && ./run_interactive.sh
# Inside container
build drumlogue/ripplerx
# Load to hardware and test
```

### Expected Result
```
✓ Gate trigger produces immediate mallet attack
✓ Resonators ring/decay with proper envelope
✓ Multiple notes play (8 voice polyphony)
✓ No crackling or artifacts
✓ Clean audio level (no clipping or silence)
```

### If Still Quiet
1. Check resonator enable: `Parameters::a_on` and `Parameters::b_on` should be true
2. Check gain: Should be positive, typically 1.0 or higher
3. Check mallet/noise mix: Should have some value > 0
4. Use debug helpers in `DEBUG_RENDER.hpp` to see what's happening

---

## File Locations

| File | Purpose | Lines |
|------|---------|-------|
| `ripplerx.h` | Main synth class | 420, 427-459, 522 |
| `unit.cc` | Runtime interface | 59 (memset) |
| `constants.h` | Default presets | Check first preset |
| `AUDIO_TROUBLESHOOTING.md` | Full debugging guide | - |
| `LOGUEPAD_COMPARISON.md` | Architecture analysis | - |
| `DEBUG_RENDER.hpp` | Debug instrumentation | - |

---

## Architecture (For Reference)

**Your Design** (RipplerX):
```
MIDI Gate → Voice Trigger
    ↓
Mallet + Noise + Samples (excitation)
    ↓
Resonator A & B (physical model)
    ↓
Comb Filter + Limiter (effects)
    ↓
Audio Output
```

**Why Shared Sample Counter is OK**:
- Samples are "excitation" input to resonators
- Like striking a drum head with the same mallet
- All resonators hear the same excitation
- Different from loguePADS (which plays different samples per track)

---

## Testing Procedure

```bash
# 1. Build
docker exec -it logue-sdk-container bash
cd /root/workspace/platform/drumlogue/ripplerx
make clean && make

# 2. Load to device
logue-cli load -u build/resonator.drmlgunit

# 3. Test on hardware
# - Select the unit
# - Trigger a gate or note
# - Listen for audio output
# - Adjust parameters and listen for changes

# 4. Check console output (if debug enabled)
# - Should show parameter states
# - Should show voice activity
# - Should show output levels
```

---

## Success Criteria

| Test | Expected | Status |
|------|----------|--------|
| Gate trigger | Immediate mallet attack | ? |
| Multiple notes | All 8 voices play | ? |
| Parameter sweep | Gain changes level | ? |
| Resonator toggle | a_on/b_on changes tone | ? |
| Voice stealing | Oldest voice stolen | ? |
| Sample playback | Excitation audible | ? |

---

## Quick Debugging

### No Sound At All
- Check a_on or b_on is true
- Check gain > 0
- Check mallet_mix or noise_mix > 0

### Very Quiet
- Increase gain parameter
- Increase mallet_mix or noise_mix

### Crackling/Noise
- Decrease gain (clipping)
- Check sample loading (bounds)
- Verify voice allocation

### Envelope Issues
- Check resonator decay parameters
- Verify frame counter is incrementing correctly
- Use debug output to monitor voice lifetime

---

## Don't Forget!

```cpp
// This is critical - WITHOUT IT, NO BUFFER CLEARING:
__unit_callback void unit_render(const float * in, float * out, uint32_t frames) {
  memset(out, 0, frames * 2 * sizeof(float));  // ← MUST BE HERE
  s_synth_instance.Render(out, frames);
}
```

---

**Last Updated**: January 10, 2026  
**Fixes Applied**: ✅ All 3 bugs fixed  
**Ready**: Yes, ready for testing

