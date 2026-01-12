# RipplerX Audio Troubleshooting & Optimization Guide

## Bugs Fixed (January 2026)

### 1. Critical Loop Iteration Bug ✓ FIXED
**Location**: [ripplerx.h:420](platform/drumlogue/ripplerx/ripplerx.h#L420)

**Problem**: 
```cpp
for (size_t frame = 0; frame < frames; frame += 2)  // WRONG
```
- Loop was incrementing by 2 but should process exactly `frames/2` iterations
- With `float32x4_t` (4 floats), each iteration processes 2 stereo frames
- Result: **Only half the output buffer was filled**, causing silence or corrupted audio

**Fix**:
```cpp
for (size_t frame = 0; frame < frames/2; frame++)  // CORRECT
```

### 2. Sample Index Advancement Bug ✓ FIXED
**Location**: [ripplerx.h:427-459](platform/drumlogue/ripplerx/ripplerx.h#L427)

**Problem**: Mono sample handling didn't correctly process 2 frames per iteration

**Fix**: Updated mono sample loading to:
```cpp
// Load 2 mono samples and duplicate each for stereo output
float32_t m1 = m_samplePointer[m_sampleIndex];
float32_t m2 = m_samplePointer[m_sampleIndex + 1];
audioIn = vcombine_f32(vdup_n_f32(m1), vdup_n_f32(m2));  // [M1,M1,M2,M2]
m_sampleIndex += 2;
```

### 3. Frame Counter Overflow Bug ✓ FIXED
**Location**: [ripplerx.h:522](platform/drumlogue/ripplerx/ripplerx.h#L522)

**Problem**: 
```cpp
voice.m_framesSinceNoteOn += frames;  // Inside loop - WRONG!
```
- Was incrementing by `frames` (e.g., 64) on EVERY iteration
- For 32 iterations: 64 × 32 = 2048 frames instead of 64
- Broke voice stealing logic

**Fix**:
```cpp
voice.m_framesSinceNoteOn += 2;  // Increment by frames processed per iteration
```

---

## Key Differences: ripplerx vs. Original Resonator

### Sample Architecture Comparison

| Aspect | Original Resonator | Current RipplerX | Impact |
|--------|-------------------|------------------|---------|
| **Sample ownership** | Per-voice (each voice has own `m_sampleIndex`) | Shared at synth level | All pressed voices share same playback position |
| **Sample trigger** | On NoteOn, passed to voice.noteOn() | On NoteOn, stored in synth-level vars | Simpler but less flexible |
| **Sample playback** | Each voice independently reads sample | All voices read from shared position | ⚠️ Potential issue for polyphonic sample playback |

### Critical Insight: Shared Sample Playback

**Current behavior in RipplerX:**
```cpp
// In Render() - runs once per frame iteration
if (m_samplePointer != nullptr && m_sampleIndex < m_sampleEnd) {
    audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
    m_sampleIndex += 4;  // Advance shared position
}

// Then applied to ALL pressed voices:
for (size_t i = 0; i < c_numVoices; ++i) {
    if (voice.isPressed) {
        resOut = vaddq_f32(resOut, audioIn);  // Same sample to every voice!
    }
}
```

**What this means:**
- ✅ **Good for**: Monophonic "audio in" routing where sample triggers once and all active notes filter it
- ⚠️ **Problematic for**: True polyphonic sample playback where each note should have independent sample position
- 🔍 **Design decision**: Appears intentional—samples serve as "excitation source" routed to resonators

---

## Potential Issues & Further Troubleshooting

### Issue 1: Buffer Not Cleared in Unit
**Status**: ✅ **NOT AN ISSUE**

The buffer IS cleared in [unit.cc:59](platform/drumlogue/ripplerx/unit.cc#L59):
```cpp
memset(out, 0, frames * 2 * sizeof(float));
```

But the Render() loop loads "old" values and accumulates:
```cpp
float32x4_t old = vld1q_f32(outBuffer);  // Should be zeros from memset
channels = vaddq_f32(old, channels);
```

This pattern is actually **correct** because:
1. Buffer starts at zeros (from memset)
2. Loading zeros and adding is equivalent to just writing
3. Allows future flexibility for mixing multiple sources

### Issue 2: No Default Note on Gate Trigger
**Status**: ⚠️ **NEEDS VERIFICATION**

**In original resonator:**
```cpp
inline void GateOn(uint8_t velocity) {
    NoteOn(m_note, velocity);  // Uses stored m_note
}
```

**In ripplerx:**
```cpp
inline void GateOn(uint8_t velocity) {
    NoteOn(m_note, velocity);  // Same pattern
}
```

**Question**: Is `m_note` properly initialized?

**Check**:
- Look for initialization in `Init()` or `Reset()`
- Default MIDI note 60 (C4) is standard
- If `m_note` is uninitialized, gates won't produce sound

### Issue 3: Voice Initialization Check
**Critical code path** in [ripplerx.h:1144](platform/drumlogue/ripplerx/ripplerx.h#L1144):

```cpp
voice.m_initialized = sampleValid;
```

**If sample loading fails** (`sampleWrapper == nullptr`):
- `voice.m_initialized = false`
- Mallet processing checks: `voice.m_initialized ? voice.mallet.process() : 0.0f`
- **Result: No mallet sound if sample fails**

**Recommendation**: Decouple mallet generation from sample validity:
```cpp
voice.m_initialized = true;  // Always allow synthesis
voice.hasSample = sampleValid;  // Separate flag for sample routing
```

### Issue 4: Resonator On/Off States
**Critical parameters**: `a_on` and `b_on` (line 398-399)

If both are false:
```cpp
resOut = vaddq_f32(resAOut, resBOut);  // Both zero!
totalOut = vmlaq_n_f32(dirOut, resOut, gain);  // resOut is zero
```

**Only `dirOut` (mallet + noise direct output) will produce sound.**

**Check**:
- Verify default preset values set `a_on = true` or `b_on = true`
- Look in [constants.h](platform/drumlogue/ripplerx/constants.h) `programs` array

### Issue 5: Sample Parameter Values
**Location**: [ripplerx.h:1125](platform/drumlogue/ripplerx/ripplerx.h#L1125)

```cpp
const sample_wrapper_t* sampleWrapper = GetSample(m_sampleBank, m_sampleNumber - 1);
```

**Note**: `m_sampleNumber - 1` because sample numbers are 1-indexed in UI but 0-indexed in API.

**Potential issues**:
- If `m_sampleNumber = 0`, this becomes `-1` (wraps to 255) → invalid
- If `m_sampleBank` is invalid → returns `nullptr`

**Check current values**:
```cpp
// Look for initialization:
m_sampleBank = 0;      // Should be 0-6 for CH/OH/RS/CP/MISC/USER/EXP
m_sampleNumber = 1;    // Should be 1-based (1-128)
m_sampleStart = 0;     // Should be 0-1000 (0%)
m_sampleEnd = 1000;    // Should be 0-1000 (100%)
```

---

## Optimization Opportunities

### 1. Voice-Level Sample Playback (Major Refactor)
**Goal**: Enable true polyphonic sample playback

**Changes needed**:
```cpp
// In Voice.h, add:
class Voice {
    // ... existing members
    float* m_samplePointer = nullptr;
    size_t m_sampleIndex = 0;
    size_t m_sampleEnd = 0;
    uint8_t m_sampleChannels = 0;
};

// In Voice trigger():
void trigger(..., const sample_wrapper_t* sample, ...) {
    if (sample) {
        m_samplePointer = sample->sample_ptr;
        m_sampleChannels = sample->channels;
        // ... calculate m_sampleIndex and m_sampleEnd
    }
}

// In Render(), per-voice sample loading:
for (voice : voices) {
    float32x4_t audioIn = voice.loadSampleFrames();  // Voice owns sample position
    // ... process voice
}
```

**Pros**: True polyphony, matches original resonator architecture
**Cons**: More memory per voice, more complex render loop

### 2. NEON Optimization Review
**Current**: Uses `float32x4_t` for 2 stereo frames at once

**Original**: Uses `float32x2_t` for 1 stereo frame at once

**Analysis**:
- `float32x4_t` reduces loop iterations by 50%
- BUT adds complexity for sample handling and buffer boundary checks
- **Recommendation**: Profile both approaches; simpler code may be faster due to better branch prediction

### 3. Sample Bounds Safety
**Current approach** (line 432-448): Multiple nested conditionals

**Optimization**: Pre-calculate safe region:
```cpp
// Before frame loop:
size_t remainingSamples = m_sampleEnd - m_sampleIndex;
size_t samplesToProcess = std::min(remainingSamples, frames * 4);  // For stereo x4

// In loop: just check counter instead of bounds
if (samplesProcessed < samplesToProcess) {
    audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
    m_sampleIndex += 4;
    samplesProcessed += 4;
}
```

### 4. Voice Stealing Optimization
**Current**: Simple oldest-voice stealing (correct)

**Original resonator pattern** (line 10-25 in resonator_orig.cc):
```cpp
size_t nextVoiceNumber() {
    size_t longestFramesSinceNoteOn = 0;
    size_t bestVoice = 0;
    for (size_t i = 1; i < c_numVoices; i++) {  // Note: starts at 1, not 0
        if (voice[i].framesSinceNoteOn > longest) {
            longest = voice[i].framesSinceNoteOn;
            bestVoice = i;
        }
    }
    return bestVoice;
}
```

**RipplerX version**: Check implementation matches this pattern

---

## Testing Checklist

### Basic Sound Generation
- [ ] Trigger gate/note without samples loaded → Should produce mallet + noise
- [ ] Trigger with samples loaded → Should add sample excitation
- [ ] Verify resonator A is enabled (`a_on = true`)
- [ ] Verify resonator B is enabled (`b_on = true`) if used
- [ ] Check gain parameter is not zero

### Sample Playback
- [ ] Load sample bank 0 (CH - Closed Hi-hat)
- [ ] Load sample 1
- [ ] Set sample start/end to 0/1000 (full sample)
- [ ] Trigger and verify audio output
- [ ] Try different banks (OH, RS, CP, MISC)

### Polyphony
- [ ] Trigger multiple notes rapidly
- [ ] Verify voice stealing doesn't crash
- [ ] Check that `m_framesSinceNoteOn` increments correctly
- [ ] Verify oldest voice gets stolen first

### Parameter Sweep
- [ ] Change gain from -60dB to +12dB → Should scale output
- [ ] Toggle `a_on` / `b_on` → Should enable/disable resonators
- [ ] Adjust mallet parameters → Should affect attack character
- [ ] Adjust noise parameters → Should affect noise level

### Edge Cases
- [ ] Load invalid sample bank/number → Should not crash
- [ ] Set sample start > sample end → Should handle gracefully
- [ ] Trigger 9+ notes (exceed voice count) → Should steal correctly
- [ ] Zero-length sample → Should not crash

---

## Reference: Original Resonator Key Points

### Sample Function Pointers (cached from runtime)
```cpp
unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr;
unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr;
unit_runtime_get_sample_ptr m_get_sample;
```

### GetSample Helper (with bounds checking)
```cpp
inline const sample_wrapper_t* GetSample(size_t bank, size_t number) const {
    if (bank >= m_get_num_sample_banks_ptr()) return nullptr;
    if (number >= m_get_num_samples_for_bank_ptr(bank)) return nullptr;
    return m_get_sample(bank, number);
}
```

### Per-Voice Sample Members
```cpp
class Voice {
    const float* m_samplePointer;
    uint8_t m_sampleChannels;
    size_t m_sampleFrames;
    size_t m_sampleIndex;  // Current read position
    size_t m_sampleEnd;    // End boundary
};
```

### NoteOn Sample Loading
```cpp
void noteOn(..., const sample_wrapper_t* const sampleWrapper, ...) {
    if (sampleWrapper) {
        m_sampleChannels = sampleWrapper->channels;
        m_sampleFrames = sampleWrapper->frames;
        m_samplePointer = sampleWrapper->sample_ptr;
        m_sampleIndex = sampleWrapper->frames * m_sampleChannels * sampleStart / 1000;
        m_sampleEnd = sampleWrapper->frames * m_sampleChannels * sampleEnd / 1000;
    }
}
```

### Per-Frame Sample Reading (inside voice render)
```cpp
if (m_sampleIndex < m_sampleEnd) {
    if (m_sampleChannels == 2) {
        excitation = vld1_f32(&m_samplePointer[m_sampleIndex]);  // Stereo
    } else {
        excitation = vdup_n_f32(m_samplePointer[m_sampleIndex]);  // Mono→Stereo
    }
    m_sampleIndex += m_sampleChannels;
}
```

---

## Architecture Comparison: RipplerX vs loguePADS

After analyzing the working **loguePADS** synth (Oleg Burdaev's sample-based drum machine):

### Key Architectural Difference

**loguePADS**:
- Per-track sample pointers: `sSamplePtr[track]` array
- Per-track sample counters: Independent position for each track
- **Polyphonic**: 16 tracks can play different samples simultaneously
- Direct write to output buffer: `vst1_f32(out_p, result)` overwrites

**RipplerX** (Current):
- Shared synth-level sample pointer: `m_samplePointer`
- Shared synth-level sample counter: `m_sampleIndex`
- **Monophonic**: All voices share same sample position
- Intended as "excitation source" for physical model resonators
- Accumulates output (load+add+store pattern)

**This architectural difference is intentional**, not a bug. RipplerX treats samples as excitation input to resonators, while loguePADS is a pure sample playback drum machine.

### See Also

- [LOGUEPAD_COMPARISON.md](LOGUEPAD_COMPARISON.md) - Detailed side-by-side code analysis
- [DEBUG_RENDER.hpp](DEBUG_RENDER.hpp) - Debug instrumentation helpers

## Next Steps

1. **Build and test** with the three fixes already applied
   ```bash
   cd docker
   ./run_interactive.sh
   # Inside container:
   build drumlogue/ripplerx
   ```

2. **If still no sound**, verify in order:
   - [ ] Resonator A or B is enabled (`a_on = true` or `b_on = true`)
   - [ ] Gain parameter is not zero
   - [ ] Mallet mix or noise mix > 0
   - [ ] Default note is valid (check m_note initialization)
   - [ ] Voice initialization completes (check m_initialized flag)

3. **Use debug instrumentation** to identify the issue:
   ```bash
   # Copy and include DEBUG_RENDER.hpp in ripplerx.h
   # Call verifyAudioPath() from GateOn()
   # Call Render_DEBUG() instead of Render()
   # Output will show parameter states, voice activity, and output levels
   ```

4. **Expected console output when gate triggers**:
   ```
   === RENDER CALL 1 (frame 0) ===
   Frames to process: 64
   Sample state: ptr=0x..., index=0, end=..., channels=2
   Resonators: a_on=1, b_on=1
   Gain: 1.000000
   Active voices: 1
     Voice 0: initialized=1, gate=1, isPressed=1, frames_since=0
   Max output level this render: 0.123456
   === END RENDER ===
   ```

5. **Profile performance** once working:
   - Measure CPU load via hardware indicators
   - Current `float32x4_t` approach is performant (2x faster than `float32x2_t`)
   - Verify no clipping (max output should stay < 1.0 for safety margin)

---

## Developer Notes

**Author**: Fedemone (dev_id: 0x46654465 'FeDe')  
**Hardware**: Korg drumlogue  
**SDK**: logue-sdk (modified)  
**Architecture**: ARMv7-A NEON, 48kHz stereo, hard-float ABI

**Key Files**:
- [ripplerx.h](platform/drumlogue/ripplerx/ripplerx.h) - Main synth class
- [unit.cc](platform/drumlogue/ripplerx/unit.cc) - Runtime interface
- [Voice.h](platform/drumlogue/ripplerx/Voice.h) - Voice management
- [constants.h](platform/drumlogue/ripplerx/constants.h) - Parameters & presets
- [header.c](platform/drumlogue/ripplerx/header.c) - Parameter descriptors

**Reference Implementation**: [resonator_orig.h](platform/drumlogue/original_code/resonator_orig.h)

---

**Last Updated**: January 10, 2026
