# RipplerX vs loguePADS: Audio Rendering Analysis

## Critical Insight: Output Buffer Writing

### loguePADS Approach (WORKING) ✓

**File**: unit.cc, lines 563-610

```cpp
void unit_render(const float * in, float * out, uint32_t frames) {
  (void)in;
  
  // Key: loguePADS DOES NOT memset() the buffer
  // Instead it DIRECTLY WRITES to output (vst1_f32)
  
  float * __restrict out_p = out;
  const float * out_e = out_p + (frames << 1);  // frames * 2
  for (; out_p != out_e; out_p += 2) {
    // ... compute vOut1 and vOut2 ...
    
    // Write directly to buffer (overwrite, don't accumulate)
    vst1_f32(out_p, sChannelPressure * vpadd_f32(...));
  }
}
```

**Key Points**:
1. **Loop iterates once per stereo frame**: `frames` times, advancing by 2 floats per iteration
2. **Direct write pattern**: `vst1_f32(out_p, result)` overwrites buffer (NOT accumulate)
3. **No buffer initialization**: Trust that caller zeroed or doesn't care
4. **Simple sample playback**: Per-track `sSamplePtr[track]` arrays, sample counter per-track

### RipplerX Issue (NON-WORKING) ✗

**File**: unit.cc, line 59 + ripplerx.h, line 534

```cpp
// unit.cc clears buffer:
memset(out, 0, frames * 2 * sizeof(float));
s_synth_instance.Render(out, frames);

// ripplerx.h tries to accumulate:
float32x4_t old = vld1q_f32(outBuffer);  // Load (zeros)
channels = vaddq_f32(old, channels);     // Add
vst1q_f32(outBuffer, channels);          // Store
outBuffer += 4;                          // Advance by 4 floats
```

**Critical Problems**:
1. **Memset clears entire buffer** - good
2. **Loop iterates frames/2 times**: Should fill `frames * 2 * 4` bytes... wait, let me recalculate
   - frames = 64 (stereo frames, 128 floats total)
   - frames/2 = 32 iterations
   - Each iteration: advance by 4 floats
   - Total: 32 * 4 = 128 floats ✓ (correct!)
3. **BUT it loads and adds old values** - OK if zeros, but adding zeros is redundant
4. **Sample counter is shared** - all voices read from same position

---

## The REAL Issue: Sample Counter Management

### loguePADS: Per-Track Counters (Correct Architecture)

```cpp
// Line 177: Per-track sample counters
static float32x4_t sSampleCounter[VECTOR_COUNT];
static const float *sSamplePtr[VECTOR_COUNT << 2];

// Line 605: Each track has independent counter position
uint32x4_t vSampleOffset1 = vcvtq_u32_f32(sSampleCounter[i]) * sSampleChannels[i];

// Each of the 4 tracks in the NEON vector can have different sample positions:
// vOut1[0] = sSamplePtr[i*4+0][vSampleOffset1[0]]  <- Track 0 reads its position
// vOut1[1] = sSamplePtr[i*4+1][vSampleOffset1[1]]  <- Track 1 reads its position
// vOut1[2] = sSamplePtr[i*4+2][vSampleOffset1[2]]  <- Track 2 reads its position
// vOut1[3] = sSamplePtr[i*4+3][vSampleOffset1[3]]  <- Track 3 reads its position
```

### RipplerX: Shared Sample Counter (Wrong Architecture)

```cpp
// ripplerx.h: Shared sample state at synth level
float* m_samplePointer = nullptr;
size_t m_sampleIndex = 0;         // SHARED across all voices!
size_t m_sampleEnd = 0;

// In Render, all voices share:
if (m_samplePointer != nullptr && m_sampleIndex < m_sampleEnd) {
    audioIn = vld1q_f32(&m_samplePointer[m_sampleIndex]);
    m_sampleIndex += 4;  // One increment for all voices
}

// Then applied to ALL voices:
for (size_t i = 0; i < c_numVoices; ++i) {
    if (voice.isPressed) {
        resOut = vaddq_f32(resOut, audioIn);  // Same sample to every voice!
    }
}
```

---

## Architecture Comparison Table

| Aspect | loguePADS | RipplerX | Issue |
|--------|-----------|----------|-------|
| **Sample Pointers** | Per-track array `sSamplePtr[track]` | Shared synth-level `m_samplePointer` | ⚠️ All tracks share one position |
| **Sample Counters** | Per-track NEON vectors `sSampleCounter[i]` | Shared `m_sampleIndex` | ⚠️ All tracks synchronized |
| **Output Writing** | Direct `vst1_f32()` overwrite | Load+Add+Store accumulate | ✓ Both work (memset zeros helps) |
| **Buffer Clearing** | Caller responsibility (unit.cc) | Explicit in unit.cc | ✓ Both clear first |
| **Loop Structure** | `frames` iterations, advance by 2 | `frames/2` iterations, advance by 4 | ✓ Both correct |
| **Sample Loading** | Per-frame, all tracks independently | Per-frame, shared load | ⚠️ Shared load is wrong |

---

## ROOT CAUSE: Sample Architecture Mismatch

**loguePADS Design**: Sample playback is **track/voice-based**
- Each of 16 tracks can play a different sample
- Each track maintains independent playback position
- Track index directly maps to `sSamplePtr[track]` array
- Natural fit for drum machine (each pad plays its own sample)

**RipplerX Design**: Sample playback is **synth-level monophonic**
- All voices share one sample
- Designed as "audio input excitation" to resonators
- Different architecture than loguePADS
- Works like drum resonators where same excitation hits multiple bodies

---

## Recommended Fixes for RipplerX

### Option 1: Keep Shared Sample, Fix Initialization (Quick)

If you want monophonic sample playback (intentional design):

```cpp
// In NoteOn:
if (sampleWrapper) {
    m_samplePointer = sampleWrapper->sample_ptr;
    m_sampleChannels = sampleWrapper->channels;
    m_sampleFrames = sampleWrapper->frames;
    m_sampleIndex = (sampleFrames * sampleChannels * sampleStart) / 1000;
    m_sampleEnd = (sampleFrames * sampleChannels * sampleEnd) / 1000;
    m_isPlayingSample = true;  // New flag
}

// In Render, ONLY load sample if it's playing:
if (m_isPlayingSample && m_sampleIndex < m_sampleEnd) {
    // Load sample
} else {
    m_isPlayingSample = false;
}
```

### Option 2: Switch to Per-Voice Sample Playback (Correct)

Follow loguePADS pattern for true polyphony:

```cpp
// In Voice.h
class Voice {
    float* m_samplePointer = nullptr;
    float m_sampleCounter = 0.0f;
    float m_sampleSize = 0.0f;
    uint32_t m_sampleChannels = 0;
    // ... etc
    
    float32x2_t processSample() {
        if (!m_samplePointer || m_sampleCounter >= m_sampleSize) {
            return vdup_n_f32(0.0f);
        }
        uint32_t idx = (uint32_t)m_sampleCounter * m_sampleChannels;
        float32x2_t sample = vdup_n_f32(m_samplePointer[idx]);
        m_sampleCounter += m_sampleIncrement;
        return sample;
    }
};

// In RipplerX::NoteOn:
const sample_wrapper_t* sw = GetSample(m_sampleBank, m_sampleNumber - 1);
if (sw) {
    voice.m_samplePointer = sw->sample_ptr;
    voice.m_sampleSize = sw->frames * sw->channels;
    voice.m_sampleChannels = sw->channels;
    voice.m_sampleCounter = (voice.m_sampleSize * sampleStart) / 1000;
    // ... calculate increment based on pitch ...
}
```

---

## Debugging Checklist for Current RipplerX

✅ **Fixes Already Applied**:
1. Loop iteration count (frames/2 instead of frames)
2. Mono sample loading (load 2 samples per iteration)
3. Frame counter overflow (increment by 2, not frames)

📋 **To Test Now**:

1. **Verify buffer is being written**:
   ```cpp
   // Add temporary in Render after vst1q_f32:
   static int dbg = 0;
   if (dbg++ % 4800 == 0) {
       float test = outBuffer[0];  // After vst1q_f32
       printf("Output buffer sample: %.6f\n", test);
   }
   ```

2. **Check sample loading**:
   ```cpp
   // In NoteOn, add:
   printf("Sample loaded: ptr=%p, size=%zu, channels=%u\n", 
          m_samplePointer, m_sampleEnd, m_sampleChannels);
   ```

3. **Verify resonator enable**:
   ```cpp
   // In Render:
   printf("a_on=%d, b_on=%d, gain=%.3f\n", a_on, b_on, gain);
   ```

4. **Monitor voice state**:
   ```cpp
   // In NoteOn:
   printf("Voice %d initialized: gate=%d, sample_valid=%d\n",
          nvoice, voice.m_gate, voice.m_initialized);
   ```

---

## Key Difference Summary

| Feature | loguePADS | RipplerX |
|---------|-----------|----------|
| **Synth Type** | Drum Machine (16 tracks) | Physical Model Resonator |
| **Sample Role** | Primary sound source | Excitation for resonators |
| **Architecture** | Polyphonic per-track | Monophonic shared |
| **Output** | Direct samples | Samples → resonators → output |

**Bottom Line**: Your three loop bugs were correct fixes. The architecture difference (shared vs per-voice sample) is intentional design, not a bug. Test current fixes first before restructuring to per-voice model.

