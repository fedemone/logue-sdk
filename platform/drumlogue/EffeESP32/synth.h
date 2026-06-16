#pragma once

/**
 * @file synth.h
 * @brief Top-level controller for the EffeESP32 drumlogue FM drum synth.
 *
 * Architecture (mirrors copych/ESP32-S3_FM_Drum_Synth, adapted to drumlogue):
 *   - A table of 59 instrument patches (drum_patches.h), each a flat struct of
 *     fixed FM parameters (the original "instruments" / GmDrums).
 *   - Selecting an instrument copies its patch into a working cache; the 24 UI
 *     parameters then edit the cached copy (override-on-touch).
 *   - A small polyphonic voice pool (MAX_VOICES x FmVoice6) with steal-by-score
 *     allocation, replacing the original DrumVoiceAllocator.
 *   - Block rendering: each active voice fills a mono scratch buffer, then a
 *     NEON pan/mix pass folds them into the stereo output.
 *
 * Real-time rules: no heap allocation, deterministic, ARM NEON where it helps.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__NEON__)
#include <arm_neon.h>
#define EFFEESP32_USE_NEON 1
#endif

#include "unit.h"
#include "constants.h"
#include "float_math.h"
#include "fm_voice6.h"
#include "drum_patches.h"

// ---- Parameter indices (must match header.c order) -------------------------
enum {
    P_INSTR = 0,
    P_PITCH,        // semitones  -24..24
    P_LEVEL,        // %          0..200
    P_PAN,          // pan        -100..100
    P_ALGO,         // algorithm  0..17
    P_ATTACK,       // ms         0..2000
    P_HOLD,         // ms         0..2000
    P_DECAY,        // ms         0..2000
    P_SUSTAIN,      // %          0..100
    P_RELEASE,      // ms         0..2000
    P_VELOMOD,      // %          0..100
    P_FILTER,       // on/off     0..1
    P_FLT_FREQ,     // Hz         20..20000
    P_FLT_RESO,     // %          0..100
    P_FLT_MORPH,    // %          0..100
    P_DETUNE,       // Hz         -50..50
    P_OP1, P_OP2, P_OP3, P_OP4, P_OP5, P_OP6,  // op volumes %  0..100
    P_NOTE,         // MIDI note 0..127 (canonical trigger note for instrument)
    P_FEEDBK,       // global feedback macro %  0..200
    P_COUNT = 24
};

#define EFFEESP32_MAX_BLOCK 64

// Carrier-waveform choices for the combined Filter selector (param order must
// match the -4..-1 / 2..5 mapping and the "Sin/Tri/Sqr/Saw" label table).
static const fmo_waveform_t kCarrierWf[4] = {
    WF_SINE, WF_TRIANGLE, WF_SQUARE, WF_SAW
};

class Synth {
public:
    Synth() {
        for (int i = 0; i < MAX_VOICES; ++i) voices_[i].setSampleRate(SAMPLE_RATE);
        for (int i = 0; i < P_COUNT; ++i) params_[i] = 0;
        load_instrument(1);   // boot on the Kick
    }
    ~Synth() {}

    inline int8_t Init(const unit_runtime_desc_t* desc) {
        if (desc->samplerate != 48000) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;
        for (int i = 0; i < MAX_VOICES; ++i) voices_[i].setSampleRate(SAMPLE_RATE);
        load_instrument(1);
        return k_unit_err_none;
    }

    inline void Teardown() {}
    inline void Resume()   {}
    inline void Suspend()  {}
    inline void Reset() {
        for (int i = 0; i < MAX_VOICES; ++i) voices_[i].noteOff();
        load_instrument(params_[P_INSTR]);
    }

    // ---- audio render ------------------------------------------------------
    fast_inline void Render(float* out, size_t frames) {
        size_t remaining = frames;
        float* dst = out;
        while (remaining > 0) {
            int n = (remaining > EFFEESP32_MAX_BLOCK) ? EFFEESP32_MAX_BLOCK : (int)remaining;
            render_block(dst, n);
            dst += (size_t)n * 2;
            remaining -= n;
        }
    }

    // ---- MIDI --------------------------------------------------------------
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        if (velocity == 0) { NoteOff(note); return; }
        int idx = allocate_voice(note);
        FmVoice6& v = voices_[idx];
        v.applyPatch(working_);
        v.setVolume(working_.volume);
        v.setVeloMod(working_.veloMod);
        v.setPan(working_.pan);
        v.setFilterActive(working_.useFilter != 0);
        v.setFrequency(working_.baseFreq * pitch_mul_);
        v.addDetune(detune_hz_);
        v.addFeedback(feedback_delta_);
        v.noteOn(note, velocity * MIDI_NORM);
    }

    inline void NoteOff(uint8_t note) {
        for (int i = 0; i < MAX_VOICES; ++i)
            if (voices_[i].isActive() && voices_[i].getNote() == note)
                voices_[i].noteOff();
    }

    inline void GateOn(uint8_t velocity)  { NoteOn(assigned_note_, velocity); }
    inline void GateOff()                 { AllNoteOff(); }
    inline void AllNoteOff() { for (int i = 0; i < MAX_VOICES; ++i) voices_[i].noteOff(); }
    inline void PitchBend(uint16_t)            {}
    inline void ChannelPressure(uint8_t)       {}
    inline void Aftertouch(uint8_t, uint8_t)   {}

    // ---- parameters --------------------------------------------------------
    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= P_COUNT) return;
        params_[index] = (int16_t)value;
        if (index == P_INSTR) {
            load_instrument(value);     // reload cache + reflect knobs
        } else {
            apply_param(index, value);  // override the cached field
        }
    }

    inline int32_t getParameterValue(uint8_t index) const {
        return (index < P_COUNT) ? params_[index] : 0;
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char buf[8];
        switch (index) {
            case P_INSTR:
                if (value >= 0 && value < DRUM_INST_COUNT) return g_drum_inst_names[value];
                return nullptr;
            case P_ALGO:
                snprintf(buf, sizeof(buf), "Alg%ld", (long)value);
                return buf;
            case P_FILTER: {
                // -4..5 -> "Off"/"On" (+ carrier waveform when overridden)
                static const char* const wf = "Sin\0Tri\0Sqr\0Saw";
                if (value == 0) return "Off";
                if (value == 1) return "On";
                int idx = (value < 0) ? (-value - 1) : (value - 2);
                const char* on = (value >= 1) ? "On " : "Off";
                snprintf(buf, sizeof(buf), "%s%s", on, wf + idx * 4);
                return buf;
            }
            default:
                return nullptr;
        }
    }

    inline const uint8_t* getParameterBmpValue(uint8_t, int32_t) const { return nullptr; }

    // ---- presets -----------------------------------------------------------
    inline void LoadPreset(uint8_t idx) { (void)idx; current_preset_ = idx; }
    inline uint8_t getPresetIndex() const { return current_preset_; }
    static inline const char* getPresetName(uint8_t idx) {
        return (idx == 0) ? "Init" : nullptr;
    }

private:
    // ---- instrument loading ------------------------------------------------
    void load_instrument(int idx) {
        if (idx < 0) idx = 0;
        if (idx >= DRUM_INST_COUNT) idx = DRUM_INST_COUNT - 1;
        working_ = g_drum_patches[idx];
        params_[P_INSTR] = (int16_t)idx;
        // Reflect the patch values into the knob array (override-on-touch UI).
        params_[P_LEVEL]     = clampi((int)(working_.volume * 100.0f + 0.5f), 0, 200);
        params_[P_PAN]       = clampi((int)(working_.pan * 100.0f), -100, 100);
        params_[P_ALGO]      = working_.algo;
        params_[P_ATTACK]    = clampi((int)(working_.attack  * 1000.0f + 0.5f), 0, 2000);
        params_[P_HOLD]      = clampi((int)(working_.hold    * 1000.0f + 0.5f), 0, 2000);
        params_[P_DECAY]     = clampi((int)(working_.decay   * 1000.0f + 0.5f), 0, 2000);
        params_[P_SUSTAIN]   = clampi((int)(working_.sustain * 100.0f + 0.5f), 0, 100);
        params_[P_RELEASE]   = clampi((int)(working_.release * 1000.0f + 0.5f), 0, 2000);
        params_[P_VELOMOD]   = clampi((int)(working_.veloMod * 100.0f + 0.5f), 0, 100);
        base_carrier_wf_     = working_.ops[0].waveform;   // for waveform override reset
        params_[P_FILTER]    = working_.useFilter ? 1 : 0; // 0/1 = patch carrier waveform
        params_[P_FLT_FREQ]  = clampi((int)(working_.filterFreqHz + 0.5f), 20, 20000);
        params_[P_FLT_RESO]  = clampi((int)(working_.filterReso  * 100.0f + 0.5f), 0, 100);
        params_[P_FLT_MORPH] = clampi((int)(working_.filterMorph * 100.0f + 0.5f), 0, 100);
        for (int i = 0; i < FMV_NUM_OPS; ++i)
            params_[P_OP1 + i] = clampi((int)(working_.ops[i].volume * 100.0f + 0.5f), 0, 100);
        // Assign the instrument's canonical (GM) trigger note.
        assigned_note_ = g_drum_inst_notes[idx];
        params_[P_NOTE] = assigned_note_;
        // Performance controls (not stored in patch) keep their current value.
        pitch_mul_     = semitone_ratio(params_[P_PITCH]);
        detune_hz_     = (float)params_[P_DETUNE];
        feedback_delta_= feedback_delta_from(params_[P_FEEDBK]);
    }

    // ---- per-parameter override into the working patch ---------------------
    void apply_param(uint8_t index, int32_t v) {
        switch (index) {
            case P_PITCH:     pitch_mul_ = semitone_ratio(v); break;
            case P_LEVEL:     working_.volume      = v * 0.01f; break;
            case P_PAN:       working_.pan         = v * 0.01f; break;
            case P_ALGO:      working_.algo        = (uint8_t)v; break;
            case P_ATTACK:    working_.attack      = v * 0.001f; break;
            case P_HOLD:      working_.hold        = v * 0.001f; break;
            case P_DECAY:     working_.decay       = v * 0.001f; break;
            case P_SUSTAIN:   working_.sustain     = v * 0.01f; break;
            case P_RELEASE:   working_.release     = v * 0.001f; break;
            case P_VELOMOD:   working_.veloMod     = v * 0.01f; break;
            case P_FILTER: {
                // v in [-4..5]: filter on when v >= 1; carrier waveform override
                // selected by magnitude (0/1 keep the patch's carrier waveform).
                working_.useFilter = (v >= 1) ? 1 : 0;
                int wf_idx = (v <= 0) ? (-v - 1) : (v - 2);   // -1 = no override
                working_.ops[0].waveform =
                    (wf_idx < 0) ? base_carrier_wf_ : kCarrierWf[wf_idx];
                break;
            }
            case P_FEEDBK:    feedback_delta_      = feedback_delta_from(v); break;
            case P_FLT_FREQ:  working_.filterFreqHz= (float)v; break;
            case P_FLT_RESO:  working_.filterReso  = v * 0.01f; break;
            case P_FLT_MORPH: working_.filterMorph = v * 0.01f; break;
            case P_DETUNE:    detune_hz_           = (float)v; break;
            case P_NOTE:      assigned_note_       = (uint8_t)clampi(v, 0, 127); break;
            case P_OP1: case P_OP2: case P_OP3:
            case P_OP4: case P_OP5: case P_OP6:
                working_.ops[index - P_OP1].volume = v * 0.01f; break;
            default: break;
        }
    }

    // ---- voice allocation --------------------------------------------------
    int allocate_voice(uint8_t note) {
        // Re-use a voice already playing this note (mono-per-note choke).
        for (int i = 0; i < MAX_VOICES; ++i)
            if (voices_[i].isActive() && voices_[i].getNote() == note) return i;
        // Prefer a free voice.
        for (int i = 0; i < MAX_VOICES; ++i)
            if (!voices_[i].isActive()) return i;
        // Otherwise steal the lowest-scoring voice.
        int best = 0; float bestScore = voices_[0].getStealScore();
        for (int i = 1; i < MAX_VOICES; ++i) {
            float s = voices_[i].getStealScore();
            if (s < bestScore) { bestScore = s; best = i; }
        }
        return best;
    }

    // ---- block render + NEON mix ------------------------------------------
    fast_inline void render_block(float* __restrict out, int n) {
        int active[MAX_VOICES]; int na = 0;
        for (int i = 0; i < MAX_VOICES; ++i)
            if (voices_[i].isActive()) active[na++] = i;

        if (na == 0) { memset(out, 0, (size_t)n * 2 * sizeof(float)); return; }

        for (int a = 0; a < na; ++a)
            voices_[active[a]].processBlock(scratch_[a], n);

#if defined(EFFEESP32_USE_NEON)
        // NEON pan/mix: accumulate all voice buffers into stereo, 4 frames/iter.
        const float32x4_t g = vdupq_n_f32(MASTER_GAIN);
        int i = 0;
        for (; i + 4 <= n; i += 4) {
            float32x4_t l = vdupq_n_f32(0.0f);
            float32x4_t r = vdupq_n_f32(0.0f);
            for (int a = 0; a < na; ++a) {
                float32x4_t s = vld1q_f32(&scratch_[a][i]);
                l = vmlaq_n_f32(l, s, voices_[active[a]].getPanL());
                r = vmlaq_n_f32(r, s, voices_[active[a]].getPanR());
            }
            // interleave L/R into stereo output
            float32x4x2_t st; st.val[0] = vmulq_f32(l, g); st.val[1] = vmulq_f32(r, g);
            vst2q_f32(&out[i * 2], st);
        }
        for (; i < n; ++i) {
            float l = 0.0f, r = 0.0f;
            for (int a = 0; a < na; ++a) {
                float s = scratch_[a][i];
                l += s * voices_[active[a]].getPanL();
                r += s * voices_[active[a]].getPanR();
            }
            out[i * 2] = l * MASTER_GAIN; out[i * 2 + 1] = r * MASTER_GAIN;
        }
#else
        for (int i = 0; i < n; ++i) {
            float l = 0.0f, r = 0.0f;
            for (int a = 0; a < na; ++a) {
                float s = scratch_[a][i];
                l += s * voices_[active[a]].getPanL();
                r += s * voices_[active[a]].getPanR();
            }
            out[i * 2] = l * MASTER_GAIN; out[i * 2 + 1] = r * MASTER_GAIN;
        }
#endif
    }

    static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float semitone_ratio(int semi)      { return fasterpow2f((float)semi * (1.0f / 12.0f)); }
    // Feedbk %: 100 = patch (neutral), 0 = none, 200 = +grit. Maps to a ±3.5
    // offset in the operator feedback domain (fmo clamps to 0..7).
    static float feedback_delta_from(int pct)  { return (pct * 0.01f - 1.0f) * 3.5f; }

    // ---- state -------------------------------------------------------------
    FmVoice6        voices_[MAX_VOICES];
    fm_drum_patch_t working_;
    int16_t         params_[P_COUNT];
    float           pitch_mul_ = 1.0f;
    float           detune_hz_ = 0.0f;
    float           feedback_delta_ = 0.0f;
    fmo_waveform_t  base_carrier_wf_ = WF_SINE;
    uint8_t         assigned_note_ = 36;
    uint8_t         current_preset_ = 0;
    float           scratch_[MAX_VOICES][EFFEESP32_MAX_BLOCK];
};
