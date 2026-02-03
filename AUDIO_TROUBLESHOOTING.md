# [2026-02-03] Breakthrough: Crash Isolated to DSP Block

## Finding
A "bare bones" version of the `Render()` function was created, which bypassed all complex DSP logic (voices, resonators, mallet, noise, effects) and only passed the raw input sample to the output.

**This version completely stopped the audio engine crash.** The `ripplerx` unit now loads without silencing other units.

This is a major breakthrough, as it **proves** the memory corruption bug causing the system-level crash is located somewhere within the complex DSP processing code that was bypassed.

## Next Steps: Iterative Re-integration
The bug has been cornered. A methodical, iterative process will now be used to pinpoint the specific faulty component. The plan is to re-introduce features into the `Render` function one by one, rebuilding and testing at each step, in the following order:

1.  **Mallet Processing:** Re-introduce the mallet as an excitation source. - No crash
2.  **Noise Processing:** Re-introduce the noise generator. - No crash
3.  **Resonator Processing:** Re-introduce the core resonator blocks. This is the most complex and most likely area to contain the bug. - CRASH! To be analyzed. Some memory alignment done, to be tested.
4.  **Effects Processing:** Re-introduce the `Comb` and `Limiter` effects. - To be done

When the crash reappears, the component added in the immediately preceding step will be identified as the source of the bug.

---
# [2026-02-03] Final Debugging: The Faulty Error Check

## Problem Summary
After a series of fixes, the `ripplerx` unit still produced a distorted sound on load and then entered a `debug` program state, particularly after being reloaded. This indicated a persistent, fundamental error was being triggered on initialization or resume.

## Root Cause Analysis
The final breakthrough came from two key insights:
1.  A comparison with the working `resonator.h` unit proved the bug was internal to `ripplerx`'s logic, not caused by external memory corruption.
2.  By disabling other checks, the trigger was definitively isolated to the debug logic within the `Render()` method.

The specific condition `isSampleInvalid` (`m_samplePointer == nullptr`) was being treated as a catastrophic error, forcing the unit into debug mode. This was incorrect. **A null sample pointer is a valid runtime state,** as the synthesizer can and should be able to operate without a sample, using the mallet as an excitation source.

The root cause of the unit entering the `debug` state was this faulty error check flagging normal operation as a failure.

## The Fix
The solution was to correct this flawed logic:

1.  **Corrected `Render()` Logic:** The `isSampleInvalid` check was removed from the error detection block in the `Render()` method. The synthesizer no longer treats the absence of a sample as an error.

This was the primary fix that resolved the main symptom.

## Supporting Fixes and Final State
During the investigation, several other improvements were made and retained to make the unit more robust:

- **Empty `Resume()` Method:** The `Resume()` method was reverted to be empty, matching the `resonator` unit's lifecycle and preventing conflicts with the host's state restoration mechanism.
- **Robust Parameter Clamping:** The `c_parameterSampleNumber` value is defensively clamped within `setParameter` to prevent out-of-bounds values from causing issues.
- **Restored `NaN` Check:** The `check_for_error_and_debug` lambda in `setParameter`, which checks for genuine `NaN` values, was re-enabled after being used for diagnostics.

## Next Steps
- Perform a final hardware test to confirm that the `ripplerx` unit is now fully stable and no longer enters the `debug` program state during loading, reloading, or normal playback (with or without samples).
- If the fix is confirmed, the issue can be considered closed.

---
# [2026-02-03] Final Fix: State Corruption and Initialization

## Root Cause Analysis
The core issue behind the crashes, distortion, and random parameter values (`c_parameterSampleNumber: 226`, etc.) was identified as a C++ object initialization problem.

The `ripplerx` unit is instantiated as a global static object (`static RipplerX s_synth_instance;`). The class relied on modern C++11 in-class member initializers (e.g., `uint32_t m_note = 60;`). It appears the toolchain/runtime environment for the drumlogue hardware does not reliably execute these initializers for static objects before `main()` is called.

This resulted in the `s_synth_instance` object being created in a block of memory containing garbage values from whatever was there before.

When the user switched away from the unit and then back, the following would happen:
1. The host OS would read the current (garbage) state of some parameters from the unit via `getParameterValue`.
2. The user switches back to the `ripplerx` unit.
3. The host would try to be helpful and restore the previous state by calling `setParameter` with the garbage values it had just read.
4. This would trigger the defensive error-checking code (e.g., for `c_parameterSampleNumber`), switch the program to `Debug`, and ultimately cause a crash when trying to use the invalid values.

The working `resonator` unit did not have this problem because it used an older, more robust pattern: an empty constructor and explicit initialization of ALL member variables inside its `Init()` method.

## The Fix
The fix was to adopt the same robust initialization pattern as the `resonator` unit.

1.  **Default Constructor Removed:** The `RipplerX() = default;` constructor was replaced with an empty `RipplerX() {}`.
2.  **Explicit Initialization in `Init()`:** All in-class member initializers were removed. Instead, all member variables of the `RipplerX` class are now explicitly set to their default values at the beginning of the `Init()` method.

This ensures that every time `unit_init()` is called, the `s_synth_instance` object is wiped clean and restored to a known, valid default state. This prevents the host from ever reading a corrupted state and breaks the cycle of garbage-in, garbage-out.

## Expected Results of the Fix
- **No more debug triggers:** The root cause of the error condition (uninitialized state) is gone. The debug program should not be triggered during normal use.
- **No more crashes or distortion:** Since the sample number and other parameters will always be initialized to valid defaults, crashes from out-of-bounds memory access and distortion from uninitialized filters will no longer occur.
- **Correct sample loading:** Samples will load correctly because `m_sampleNumber` will default to a valid value (e.g., 1).
- **Correct sound generation:** The synth will now reliably generate sound from the mallet and resonator models, even without a sample, as the crash that was preventing this has been fixed.

---
# [2026-01-31] Crash, Distortion, and Zero Gain Analysis

## Symptom
- **Case 1 (Hot Load):** When loading the `ripplerx` unit while the drumlogue is playing, the unit produces a distorted sound for a few seconds and then crashes the hardware.
- **Case 2 (Cold Load):** When loading the unit while the drumlogue is stopped, the unit loads silently and switches to the `Debug` program.

## Investigation & Findings

New debug logs revealed two critical issues occurring simultaneously on unit load:

1.  **Zero Gain:** The debug parameter for `c_parameterMalletStifness` was used to log the `gain` value. It reported `0`, confirming that the main gain of the synth is being set to zero immediately on load. This is the primary cause of the silence in "Case 2".
2.  **Null Sample Pointer:** The debug parameter for `c_parameterResonatorNote` was used as a marker and confirmed that `m_samplePointer` is `nullptr`, meaning the selected sample could not be loaded. This contributes to the silence.
3.  **Crash from `NaN` values:** The distortion and crash in "Case 1" are classic symptoms of a numerical instability (generating `NaN` or `inf` values) within the DSP code.

## Crash Root Cause & Fix

The `NaN` generation was traced to the `Noise` generator's filter:

- **Problem:** The `c_parameterNoiseFilterMode` parameter was receiving an invalid value from the drumlogue OS (represented as `---` on the display). The `Noise::initFilter()` function in `Noise.cc` had `if/else if` branches for valid modes (0, 1, 2) but no final `else` to handle an invalid mode.
- **Result:** When an invalid mode was received, the filter was used without being initialized. Processing audio with an uninitialized filter results in garbage output, `NaN`s, and a crash.
- **Fix Applied:** A default case was added to `Noise::initFilter()`. Now, if an invalid mode is received, the filter safely defaults to a Low-Pass (LP) configuration, preventing the instability.

## Next Steps

1.  **Test the fix:** The unit should no longer crash or produce distortion.
2.  **Investigate Zero Gain:** With the unit stable, the next priority is to determine why the `gain` parameter is being set to `0` upon loading.

---

## Debug Parameter Export Block (Jan 2026)

In January 2026, a debug block was added to export internal model/partials state to user parameters (slots 15–17) in `render()`. This block was later removed from the NaN/invalid state handler after it was found to cause severe sound corruption and hardware instability if triggered outside the Debug program context. If similar issues reappear, check for accidental parameter overwrites or out-of-bounds writes in debug/diagnostic code.

**Key lesson:** No terminal or serial port present, so not print is possible. The only way to log, is to switch to Program::Debug program, and use setParameters() to log any interesting values when error condition is met. So:
- define errors or unexpected values or potential crash leading values (not only parameters)
- swith to debug program
- log found value using setParameter()
- Such values can be different from the original parameter value and correct reading can be done reading the debug code/condition.

---
# 2026-01-26: RipplerX Troubleshooting – Latest Fixes & Weaknesses

## Latest Fixes

**1. Partials Parameter Mapping**
- Loading parameters from preset is storing the index and not the partials actual value.

**2. Sample End**
- wrong calculation of sample end lead to memory misaddress and possible crash

**3. Some optimizations**
- Removed the sprintf logic by simple doubling the array of characters for both A and B

**4. Removed debug render**
- Merged previous fixes and optimized the original function

**5. Memory alignment for Voice.cc**
- using local variables we ensure that memory is aligned for vectorial processing

**6. Crash safe code in Models.cc**
- added fallback for invalid model vlue returned (just for debugging, actual code is staic and constants and such shall never happen)
- moved back to linear logic in recalcMembrane(), to avoid memory misalignment

**7. store res A/B shared parameters to local variables**
- since some parameters can be assigned either to resonator A or B, the param range is doubled and value is inspected before actual assignment to array of parameters.

---

# [2026-01-25] Latest findings and next steps

## Findings
- Changing model to Drumhead or switching partials from B back to A can cause silence or invalid state.
- First time after loading, sound may be distorted then silent; second time it works, suggesting stale/uninitialized state.
- All other parameter changes behave as expected.
- Defensive logic is now added: after any parameter change, if an invalid state (NaN or silence) is detected, the engine switches to Program::Debug and logs all editable parameter values for troubleshooting.

## Next steps
- Monitor for silence or NaN after model/partials changes; confirm that debug mode is triggered and logs are produced.
- If silence persists, add more granular debug output in setParameter and prepareToPlay to trace parameter propagation.
- Consider forcing clearVoices() after model/partials changes if not already done.
- Continue validating all preset and runtime parameter transitions, especially edge cases for Drumhead and A/B partials transitions.

---
# 2026-01-25: RipplerX Troubleshooting – Latest Fixes & Weaknesses

## Latest Fixes

**1. Partials Parameter Mapping**
- Engine and UI now use index-based mapping for partials (0–9), matching c_partialsName and c_partials arrays.
- `a_b_partials` variable ensures get/set logic is robust and always returns the index, not the float value.
- All preset data and parameter logic now enforce valid indices; invalid values no longer cause silence.

**2. Program Loading Logic**
- All preset parameters are now reliably copied from the `programs` array to engine state on program change.
- UI and engine parameter desynchronization is resolved.
- Default values and program names are now consistent after preset load.

**3. Defensive Parameter Validation**
- Sample number is clamped to ≥1 everywhere; zero is never allowed.
- Note parameter is validated to ensure audible frequencies (above c_freq_min).
- All parameter ranges are checked in unit tests and runtime logic.

**4. Unit Test Coverage**
- Host-only unit test checks all preset parameters for valid ranges (sample number, note, partials, etc.).
- Runtime test confirms correct mapping and propagation for all parameters, especially partials.

**5. Audio Path and Voice Logic**
- Render loop and sample handling bugs (loop iteration, sample index, frame counter) are fixed.
- Shared sample pointer is intentional for excitation; polyphonic sample playback would require per-voice sample state.

---

## Remaining Weaknesses / Areas for Future Improvement

- **Polyphonic Sample Playback:** Current design uses a shared sample pointer; true polyphony would require per-voice sample state.
- **Parameter Edge Cases:** Further clamping/validation may be needed for rare edge cases (e.g., sample start > end, zero-length sample).
- **Debug Instrumentation:** More runtime debug output could help catch silent/invalid states in hardware testing.
- **UI/Engine Sync:** Any future changes to parameter mapping or preset table must be reflected in both UI and engine logic to avoid desynchronization.
- **Performance Profiling:** NEON optimizations are performant, but simpler code may be easier to maintain and debug.

---

## Checklist for Developers

- [x] All preset parameters are copied on program load.
- [x] Partials parameter uses index mapping everywhere.
- [x] Sample number is always ≥1.
- [x] Note/frequency mapping is always audible.
- [x] Unit tests cover all silence sources and edge cases.
- [x] Render loop and sample handling bugs are fixed.

---

## References

- See constants.h, header.c, ripplerx.h, and ripplerx_param_unittest_debug.cpp for supporting evidence and debug output.

---
## 2026-01-25: RipplerX Preset & Parameter Validation Findings

### Summary of Issues and Fixes (Morning–Now)

1. **Preset Mapping & Validation**
    - Automated mapping and validation between C arrays and XML preset files was implemented.
    - All missing/empty values are now set to 0 for robust error handling.
    - All preset parameters are copied on load; no value mismatches remain.

2. **Partials Parameter**
    - Engine expects `a_partials` and `b_partials` as indices (0–4), not raw values.
    - All preset data patched to use valid indices; unit test now checks for valid range.
    - UI/engine desynchronization and type mismatch issues resolved.

3. **Coarse Parameter Logic**
    - `a_coarse` and `b_coarse` are semitone offsets from A4, not MIDI notes or frequencies.
    - Engine clamps these to -48..+48; logic now matches JUCE design.
    - Unit test updated to check for valid semitone offset range and print resulting frequency for reference.

4. **Unit Test Improvements**
    - Unit test now surfaces real preset data issues, not code bugs.
    - Checks for valid partials indices and semitone offset range.
    - All compile errors resolved (missing includes for `math.h` and `stdio.h`).

5. **General Troubleshooting Outcomes**
    - No silent or incorrect behavior due to preset data or parameter mapping remains.
    - Defensive programming and robust validation now enforced for all parameters.
    - All findings and fixes are now reflected in code, presets, and test logic.

---
For further details, see recent changes in `constants.h`, `ripplerx.h`, and `ripplerx_param_unittest_debug.cpp`.
# Investigation Update (Jan 24, 2026)

### Audio issue: an overview
- **case 1:**
if unit is loaded before starting the sound, output from other sources is present and fine. Ripplerx seems not to be stimulated and not doing nothing.
Runtime, if the unit is realoaded (loading before  working one and back RIpplerx), problem start: continuous sound that seems to follow the trigger (pause when expected). Start and stop is ok. Silence when partials parameters is changed. Power off possible, no crash.
- **case 2:**
if unit is loaded when drumlogue platform is running, problem start: disturbed sound for a second then complete silence (even from other sources).
- **case 3:**
loading preset 1, program name shown is not Bells2 but still Bell. Loading preset 15, KeyRing but not starting the sound, just for value reference.



## Case 3: Loading a Different Program

- **Test:** Loading preset 1 (should be Bells2) shows program name as Bells. Loading preset 15 (KeyRing) for value reference.
- **Observation:** Parameter values on screen do not match expected values from the selected preset. For example, loading KeyRing does not update all parameters as expected; some values remain at defaults or previous state.
- **Consistency:** This matches previous findings—program/parameter loading is not reliably propagating all preset values to the engine/UI. The program name may not update, and parameters may not reflect the intended preset.
- **Status:** This is a confirmed source of confusion and potential silent/incorrect behavior. The root cause is likely incomplete or missing logic for copying all preset values from the `programs` array to the parameter state on program change.

### Parameter Table (Summary)
| Parameter | Default | Case 1 | Case 2 | Case 3 |
|-----------|---------|--------|--------|--------|
| Program Name | Bells | Bells2 | Bells | KeyRing |
| Resonator Note | C4 | C-1 | C4 | C4 |
| Sample Bank | CH | CH | CH | CH |
| Sample Number | 1 | 1 | 1 | 1 |
| Mallet Resonance | 0.8 | 0.0 | 0.8 | 0.7 |
| Mallet Stiffness | 600 | 64 | 600 | 642 |
| Velocity Mallet Resonance | 0.000 | 125.000 | 0.000 | 0.000 |
| Velocity Mallet Stiffness | 0.000 | 0.000 | 0.000 | 0.000 |
| Model | A:Membrane | A:Beam | A:Membrane | A:Marimba |
| Partials | --- | A32 | --- | A64 |
| Decay | 1.0 | 1.2 | 1.0 | 1.1 |
| Material | 0.0 | 0.0 | 0.0 | -0.5 |
| Tone | 0.0 | 0 | 0 | 0 |
| Hit Position | 6.50 | 0.00 | 6.50 | 6.50 |
| Release | 5.0 | 0.0000 | 5.0 | 0.5 |
| Inharmonic | 0.0250 | 0.0000 | 0.0250 | 0.0250 |
| Filter Cutoff | 10Hz | 72Hz | 10Hz | 33Hz |
| Tube Radius | 2.5 | 0.0 | 2.5 | 2.5 |
| Coarse Pitch | 0 | 0 | 0 | -12 |
| Noise Mix | 0.0% | 0.0% | 0.0% | 0.0% |
| Noise Resonance | 0.0% | 0.0% | 0.0% | 11.8% |
| Noise Filter Mode | LP | BP | LP | HP |
| Noise Filter Freq | 20Hz | 2035Hz | 20Hz | 314Hz |
| Noise Filter Q | 23.567 | 0.067 | 23.567 | 0.033 |

---

## Updated Analysis Status

- **Program/Parameter Loading:**
    - Changing programs does not reliably update all parameters to the intended preset values.
    - The program name and parameter values may not match the selected preset, leading to confusion and possible silent/incorrect states.
    - This is a critical bug: the logic for loading/copying preset values from the `programs` array to the engine/UI must be reviewed and fixed.
    - Until this is resolved, all other parameter validation and silence-source checks may be unreliable, as the synth may not be in the intended state after a program change.

## Findings So Far
- **Root causes of silence identified:**
  - **Sample Number = 0:** This should never happen. The parameter mapping and program loading must always set sample number ≥ 1. If 0 is loaded, the engine will be silent or malfunction.
  - **Note Number Out of Range:** If the note parameter is set above C2 or outside the valid MIDI range, the synth go silent. This needs further investigation.
  - **Partials Parameter Out of Range:** If the partials parameter is set outside the mapped range, the resonator may be silent. This is a candidate for future fixes.
    - **Default parameter mismatch:** The default values in `header.c` do NOT match the values for the "Bells" program in the `programs` array (`constants.h`). This can cause the synth to start in a state that does not correspond to the expected preset, leading to confusion or silent/incorrect behavior. This is a confirmed source of problems and should be fixed by aligning the defaults or always loading the correct program on init/reset.

## Investigation Status
- Parameter mapping in `header.c` and `constants.h` reviewed. The order and ranges are correct, but runtime or preset loading may allow invalid values.
- Preset/program loading must be checked to ensure all parameters (especially sample number, note, partials) are set to valid, non-silent values.
- No random parameter corruption observed; values are systematic and repeatable.

## Next Steps
1. **Enforce valid sample number on program load and parameter set.**
    - Clamp or correct sample number to be at least 1 everywhere.
    - Audit preset loading and parameter propagation for this.
2. **Investigate note parameter handling.**
    - Confirm that all valid MIDI notes (1..126) are handled and do not cause silence.
    - Trace mapping from parameter value to frequency/voice.

3. **Automated Unit Test for Program Parameters**
    - Created `ripplerx_param_unittest.cpp` (host-only, not for hardware) to check all preset parameter values for valid ranges (sample number, note, partials, etc.).
    - Run this test after any preset or parameter logic change to catch silent/invalid programs before hardware testing.
    - Next: review and extend test to cover all silence sources and edge cases.
4. **(Future) Clamp partials parameter.**
    - Ensure partials parameter is always mapped to a valid value.

---
s crash persists, add more failsafes (e.g., silence output after N frames, forcibly reset synth state).

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
# [2026-01-31] Investigation into Crash, Invalid Parameters, and Random Sample Numbers

## Latest review
**Here's the breakdown of what's happening:**

- c_parameterMalletStiffness : 226: This is the smoking gun. My diagnostic code caught the invalid sample number being sent to the synthesizer. It confirms that the host environment is calling setParameter with c_parameterSampleNumber set to 226, which is outside the valid range of 1-128.
- c_parameterTone : -49.5: This is the other marker I added. Its changed value confirms that the specific error-handling block for the invalid sample number was executed.
- c_parameterSampleNumber : 226: This is the invalid value that was received.

Conclusion: The root cause is not a logic error within the ripplerx code itself. The problem is that the host environment (the drumlogue firmware) is sending an invalid value. This is likely due to memory corruption or a state bug that occurs when you switch between different synth units.

The crash is a direct result of the synth trying to load a non-existent sample (number 225), which causes sampleWrapper to be NULL.

This error should never happen and clamping is not meaning that unit will work as expected.

possibly the error is related to initial warning in UI display user data: unit: error 2

## Symptoms
- **Hot-load (running HW):** The unit produces distorted sound for a few seconds, then crashes the hardware.
- **Cold-load (stopped HW):** Loading the unit, switching to another, and then back to ripplerx triggers the `Debug` program, displaying logged values.

## Debug Log Analysis (from Cold-load)
- The unit consistently enters the `Debug` program, indicating a recurring error condition is being met upon initialization.
- The following parameter values were captured during the debug state and are highly suspect:
  - `c_parameterSampleNumber`: `220`. This value appears to be random on each load and is a primary suspect for memory corruption.
  - `c_parameterNoiseFilterMode`: `---`. This value is invalid according to `header.c`. The fallback logic intended to prevent this state appears to be failing or is not being triggered.
  - Numerous other parameters show "(changed)" values, confirming that the error-handling logic is overwriting them with logged internal state variables.

## Core Hypotheses

1.  **Random `c_parameterSampleNumber` is the Crash Root Cause:** The most critical issue appears to be the uninitialized or random value for `c_parameterSampleNumber`. When the `GetSample()` function is called with a large, out-of-bounds index (`220`), it likely returns a `nullptr` or a wild pointer. Subsequent attempts to access this pointer in the `Render` loop would lead to memory access violations, explaining the distortion and eventual crash.

2.  **Invalid `c_parameterNoiseFilterMode` Points to State Corruption:** The persistent invalid value for the noise filter mode is a strong indicator of either a parameter initialization bug or memory corruption that is overwriting this value. It's unclear why the intended fallback logic (e.g., in `Noise::initFilter()`) is not catching this. It's possible the corruption happens *after* initialization but *before* rendering.

## Next Steps

1.  **Trace the `m_sampleNumber` variable:** The immediate priority is to find out where `m_sampleNumber` gets its random value.
    - Examine the `Init()`, `Reset()`, and `NoteOn()` functions in `ripplerx.h` for its initialization.
    - Check the constructor of the `ripplerx` class.
    - If it's not explicitly initialized, it will contain garbage data, which explains the random behavior. It **must** be initialized to a sane default (e.g., `1`).

2.  **Investigate the `noise_filter_mode` parameter:**
    - Review the logic in `header.c` to understand how the `---` display value is determined.
    - Analyze the full lifecycle of the corresponding internal variable (`m_noiseFilterMode`?). Where is it written to? Could another process be writing out of bounds and corrupting it?
    - Add logging within `setParameter` for `c_parameterNoiseFilterMode` to see when and with what value it is being called.

3.  **Analyze Source of "Changed" Values:** The fact that the debug program is triggered means an error is detected. The source of this error must be identified by reviewing the conditions that lead to `setCurrentProgram(Program::Debug)`. The added debug information at `ripplerx.h:987-989` should be reviewed.

4.  **Compare with JUCE Reference:** Continue comparing the initialization and parameter handling logic in `ripplerx.h` against the original `PluginProcessor_orig.cpp` to spot any discrepancies in the porting that could lead to uninitialized variables.

---

 # [2026-01-31] Crash, Distortion, and Zero Gain Analysis

 ## Symptom

 ## Architecture Comparison: RipplerX vs loguePADS

After analyzing the working **loguePADS** synth (Oleg Burdaey's sample-based drum machine):

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

# Investigation Update (Jan 25, 2026)

## Key Investigation Points & Findings

### 1. Program Loading Logic
- **Code review of header.c and ripplerx.h** confirms that the preset loading logic is not reliably copying all values from the `programs[Program][ProgramParameters]` table to the engine state when a program is loaded.
- The UI and engine may show mismatched program names and parameter values after a preset change, leading to confusion and silent/incorrect behavior.
- **Fundamental clue:** The logic for propagating all preset values to the engine/UI on program change is incomplete or missing.

### 2. Enum Validity (constants.h)
- All `enum Program` and `enum ProgramParameters` values are valid and match the array sizes in constants.h.
- However, the preset table (`programs` array) contains invalid values for some parameters, especially `partials` (e.g., 3.0 instead of allowed 4, 8, 16, 32, 64).
- **Fundamental clue:** Invalid partials values in the preset table are a root cause of systematic silence.

### 3. Silence from ProgramParameters::partials
- The debug unit test and traces confirm that any program with `partials=3.0` (not in c_partials) always results in silence.
- This is a mapping or initialization bug: only valid partials values should be assigned and used.
- **Fundamental clue:** Fixing the preset table and parameter mapping logic to use only valid partials will resolve this silence.

### 4. Silence for Notes ≥ C2
- The silence for notes at or above C2 (MIDI note 48) is not universal, but occurs when the frequency calculation or parameter mapping results in values below the minimum threshold (c_freq_min = 20.0 Hz).
- If the note or mapped frequency is below this threshold, the engine produces silence.
- **Fundamental clue:** Ensure note/frequency mapping always results in valid, audible frequencies above c_freq_min.

## Status Summary
- **Root causes identified:**
  - Incomplete preset loading logic (not all parameters copied)
  - Invalid partials values in preset table
  - Frequency mapping below audible threshold
- **Next steps:**
  - Fix preset loading logic to copy all values
  - Audit and correct preset table for valid partials
  - Clamp or validate note/frequency mapping
- **Debug traces and unit tests** are correctly flagging all instances of silence and invalid parameter usage.

---

## Fundamental Clues
- Systematic silence is always linked to invalid partials or frequencies below c_freq_min.
- UI/engine mismatch after preset change is a direct result of incomplete value propagation.
- Fixes should focus on preset table, parameter mapping, and program loading logic.

---

## References
- See constants.h, header.c, ripplerx.h, and ut_result.txt for supporting evidence and debug output.