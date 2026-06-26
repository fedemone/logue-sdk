#pragma once

/**
 * @file fm_voice6.h
 * @brief Six-operator FM percussion voice (18 algorithms) + ADSR + SVF.
 *
 * Port of copych/ESP32-S3_FM_Drum_Synth FMDrums/FmVoice6.h (MIT License,
 * Copyright 2025 Evgeny Aslovskiy "Copych").
 *
 * Changes from the original:
 *   - C++ class kept, but the per-operator FmOperator C++ object is replaced
 *     by the lighter C `fm_op_t` from fm_operator.h (already used in EffeMD).
 *   - ESP32 / Arduino / heap_caps / ESP_LOG / IRAM_ATTR dependencies removed.
 *   - DMA block rendering kept (processBlock) so the drumlogue mixer can apply
 *     a NEON pan/mix pass over per-voice scratch buffers.
 *   - Sample rate 48 kHz; pitch argument to the operators is fixed at 1.0
 *     (global pitch is handled by scaling baseFreq in the synth layer).
 *
 * The 18 algorithm graphs are reproduced verbatim from the upstream file so
 * the timbres match.  Each algoN_*() consumes one envelope sample `e` and
 * returns one output sample.
 */

#include <cstdint>
#include <cstring>
#include "constants.h"
#include "fm_operator.h"
#include "adsr.h"
#include "svf_filter.h"

#define FMV_NUM_OPS   6
#define FMV_NUM_ALGOS 18

// ---- Patch data (mirrors the original FmDrumPatch / FmOpParams) -------------
typedef struct {
    float ratio;
    float detune;
    float feedback;
    float volume;
    fmo_waveform_t waveform;
} fm_op_params_t;

typedef struct {
    uint8_t  algo;
    float    baseFreq;
    float    volume;
    float    pan;            // -1..+1
    float    attack;
    float    hold;
    float    decay;
    float    sustain;
    float    release;
    float    veloMod;
    uint8_t  useFilter;
    float    filterFreqHz;
    float    filterReso;
    float    filterMorph;
    fm_op_params_t ops[FMV_NUM_OPS];
} fm_drum_patch_t;

class FmVoice6 {
public:
    FmVoice6() {
        setSampleRate(SAMPLE_RATE);
        for (auto& op : ops_) fmo_init(&op);
        filter_.init(SAMPLE_RATE);
        env_.init(SAMPLE_RATE);
    }

    void setSampleRate(float sr) {
        sr_ = sr;
        env_.init(sr);
    }

    // ---- patch application -------------------------------------------------
    void applyPatch(const fm_drum_patch_t& p) {
        algo_      = (p.algo < FMV_NUM_ALGOS) ? p.algo : 0;
        baseFreq_  = p.baseFreq;
        volume_    = p.volume;
        veloMod_   = p.veloMod;
        useFilter_ = (p.useFilter != 0);
        setPan(p.pan);
        env_.setAttackTime(p.attack);
        env_.setHoldTime(p.hold);
        env_.setDecayTime(p.decay);
        env_.setSustainLevel(p.sustain);
        env_.setReleaseTime(p.release);
        filter_.setFreqHz(p.filterFreqHz);
        filter_.setResonance(p.filterReso);
        filter_.setMorph(p.filterMorph);
        for (int i = 0; i < FMV_NUM_OPS; ++i) {
            fmo_set_ratio(&ops_[i], p.ops[i].ratio);
            fmo_set_detune(&ops_[i], p.ops[i].detune);
            fmo_set_feedback(&ops_[i], p.ops[i].feedback);
            fmo_set_volume(&ops_[i], p.ops[i].volume);
            ops_[i].waveform = p.ops[i].waveform;
            fmo_set_freq(&ops_[i], baseFreq_);
        }
    }

    void setFrequency(float hz) {
        baseFreq_ = hz;
        for (auto& op : ops_) fmo_set_freq(&op, hz);
    }
    void setVolume(float v)  { volume_ = v; velocityVol_ = v * velocity_; }
    void setVeloMod(float m) { veloMod_ = (m < 0.f) ? 0.f : (m > 1.f ? 1.f : m); }
    void setAlgorithm(uint8_t a) { algo_ = (a < FMV_NUM_ALGOS) ? a : 0; }
    void setFilterActive(bool a) { useFilter_ = a; }
    void setOpVolume(int i, float v) { if (i >= 0 && i < FMV_NUM_OPS) fmo_set_volume(&ops_[i], v); }

    // Adds a global detune (Hz) on top of every operator's per-patch detune.
    // Called once at note-on, after applyPatch(), so it never accumulates.
    void addDetune(float hz) {
        if (hz == 0.0f) return;
        for (auto& op : ops_) fmo_set_detune(&op, op.detune + hz);
    }

    // Adds `delta` to every operator's per-patch feedback (global Feedbk macro;
    // fmo_set_feedback clamps to 0..7). Negative removes grit, positive adds it
    // — so the macro is useful even on patches whose feedback is zero.
    // Called once at note-on, after applyPatch(), so it never accumulates.
    void addFeedback(float delta) {
        if (delta == 0.0f) return;
        for (auto& op : ops_) fmo_set_feedback(&op, op.fb + delta);
    }

    void setPan(float pan) {
        pan_  = pan;
        float p = (pan + 1.0f) * 0.5f;     // 0..1
        panR_ = p;
        panL_ = 1.0f - p;
    }
    float getPanL() const { return panL_; }
    float getPanR() const { return panR_; }

    // ---- transport ---------------------------------------------------------
    void reset() {
        for (auto& op : ops_) fmo_reset(&op);
        filter_.reset();
    }

    void noteOn(uint8_t midiNote, float vel) {
        reset();
        velocity_    = vel;
        note_        = midiNote;
        float effective_vel = 1.0f - veloMod_ * (1.0f - vel);
        velocityVol_ = effective_vel * volume_;
        env_.retrigger(Adsr::END_NOW);
    }
    void noteOff()  { env_.end(Adsr::END_REGULAR); }
    void noteChoke(){ env_.end(Adsr::END_SEMI_FAST); }

    bool    isActive() const { return env_.isRunning(); }
    uint8_t getNote()  const { return note_; }
    float   getStealScore() const {
        if (!isActive()) return 1e6f;
        return env_.getPenalty() * velocityVol_;
    }

    // ---- block render ------------------------------------------------------
    // Fills `buf` with `n` mono samples for this voice.
    fast_inline void processBlock(float* __restrict buf, int n) {
        if (useFilter_) {
            for (int i = 0; i < n; ++i)
                buf[i] = velocityVol_ * filter_.processMorph(renderSample(env_.process()));
        } else {
            for (int i = 0; i < n; ++i)
                buf[i] = velocityVol_ * renderSample(env_.process());
        }
    }

private:
    fast_inline float renderSample(float e) {
        switch (algo_) {
            case 0:  return algo0_2c(e);
            case 1:  return algo1_3c(e);
            case 2:  return algo2_1m_1c(e);
            case 3:  return algo3_2m_2c(e);
            case 4:  return algo4_3ms_1c(e);
            case 5:  return algo5_4ms_1c(e);
            case 6:  return algo6_2m_1m_1c(e);
            case 7:  return algo7_3m_1m_2c(e);
            case 8:  return algo8_2m_1m_1c(e);
            case 9:  return algo9_2m_2m_2c(e);
            case 10: return algo10_2m_3c(e);
            case 11: return algo11_3m_3c(e);
            case 12: return algo12_2m_4c(e);
            case 13: return algo13_1m_5c(e);
            case 14: return algo14_2m_1amp_1c(e);
            case 15: return algo15_2m_2amp_2c(e);
            case 16: return algo16_2m_2amp_1c(e);
            case 17: return algo17_4m_1amp_1c(e);
            default: return 0.0f;
        }
    }

    // shorthands matching the upstream op->process() calls (pitch fixed = 1.0)
    fast_inline float M(int i, float m, float e) { return fmo_mod(&ops_[i], m, e, 1.0f); }
    fast_inline float O(int i, float m, float e) { return fmo_out(&ops_[i], m, e, 1.0f); }
    fast_inline float A(int i, float m)          { return fmo_am(&ops_[i], m, 1.0f); }

    // --- algorithm implementations (verbatim graphs from FmVoice6.h) --------
    fast_inline float algo0_2c(float e) {
        return ONE_DIV_SQRT2 * (O(0, 0.f, e) + O(5, 0.f, e));
    }
    fast_inline float algo1_3c(float e) {
        return ONE_DIV_SQRT3 * (O(0, 0.f, e) + O(5, 0.f, e) + O(4, 0.f, e));
    }
    fast_inline float algo2_1m_1c(float e) {
        float m = M(5, 0.f, e);
        return O(0, m, e);
    }
    fast_inline float algo3_2m_2c(float e) {
        float m5 = M(5, 0.f, e);
        float m4 = M(4, 0.f, e);
        return ONE_DIV_SQRT2 * (O(0, m5, e) + O(3, m4, e));
    }
    fast_inline float algo4_3ms_1c(float e) {
        float m1 = M(5, 0.f, e);
        float m2 = M(4, m1, e);
        float m3 = M(3, m2, e);
        return O(0, m3, e);
    }
    fast_inline float algo5_4ms_1c(float e) {
        float m5 = M(5, 0.f, e);
        float m4 = M(4, m5, e);
        float m3 = M(3, m4, e);
        float m2 = M(2, 0.f, e);
        return ONE_DIV_SQRT2 * (O(0, m3, e) + O(2, m2, e));
    }
    fast_inline float algo6_2m_1m_1c(float e) {
        float m5 = M(5, 0.f, e);
        float m4 = M(4, 0.f, e);
        float m3 = M(3, 0.f, e);
        return O(0, m3 + m4 + m5, e);
    }
    fast_inline float algo7_3m_1m_2c(float e) {
        float m5 = M(5, 0.f, e);
        float m4 = M(4, m5, e);
        float m3 = M(3, m4, e);
        float m1 = M(1, 0.f, e);
        return ONE_DIV_SQRT2 * (O(0, m1, e) + O(2, m3, e));
    }
    fast_inline float algo8_2m_1m_1c(float e) {
        float m5 = M(5, 0.f, e);
        float m4 = M(4, 0.f, e);
        float m3 = M(3, m5 + m4, e);
        return O(0, m3, e);
    }
    fast_inline float algo9_2m_2m_2c(float e) {
        float m1 = M(2, 0.f, e);
        float m2 = M(1, m1, e);
        float m3 = M(5, 0.f, e);
        float m4 = M(4, m3, e);
        return ONE_DIV_SQRT2 * (O(0, m2, e) + O(3, m4, e));
    }
    fast_inline float algo10_2m_3c(float e) {
        float m4 = M(4, 0.f, e);
        float m5 = M(5, 0.f, e);
        return ONE_DIV_SQRT3 * (O(0, m5, e) + O(1, m4, e) + O(2, 0.f, e));
    }
    fast_inline float algo11_3m_3c(float e) {
        float m1 = M(1, 0.f, e);
        float m3 = M(3, 0.f, e);
        float m5 = M(5, 0.f, e);
        return ONE_DIV_SQRT3 * (O(0, m1, e) + O(2, m3, e) + O(4, m5, e));
    }
    fast_inline float algo12_2m_4c(float e) {
        float m5 = M(1, 0.f, e);
        float m4 = M(4, 0.f, e);
        return ONE_DIV_SQRT5 * (O(0, m5, e) + O(1, m4, e) + O(2, 0.f, e) + O(3, 0.f, e));
    }
    fast_inline float algo13_1m_5c(float e) {
        float m5 = M(1, 0.f, e);
        return ONE_DIV_SQRT5 * (O(0, m5, e) + O(1, m5, e) + O(2, 0.f, e) + O(3, 0.f, e) + O(4, 0.f, e));
    }
    fast_inline float algo14_2m_1amp_1c(float e) {
        float amp3 = A(3, 0.f);
        float m5   = M(5, 0.f, e);
        return O(0, m5, e * amp3);
    }
    fast_inline float algo15_2m_2amp_2c(float e) {
        float amp3 = A(3, 0.f);
        float amp2 = A(2, 0.f);
        float m5   = M(5, 0.f, e);
        float m4   = M(4, 0.f, e);
        return ONE_DIV_SQRT2 * (O(0, m5, e * amp3) + O(1, m4, e * amp2));
    }
    fast_inline float algo16_2m_2amp_1c(float e) {
        float amp3 = A(3, 0.f);
        float amp2 = A(2, 0.f);
        float m5   = M(5, 0.f, e);
        float m4   = M(4, 0.f, amp2);
        return amp3 * O(0, m5 + m4, e);
    }
    fast_inline float algo17_4m_1amp_1c(float e) {
        float m3   = M(3, 0.f, e);
        float amp2 = A(2, m3);
        float m5   = M(5, 0.f, e);
        float m4   = M(4, m5, e);
        float m1   = M(1, 0.f, e);
        return O(0, m1 + m4, e * amp2);
    }

    // ---- state -------------------------------------------------------------
    fm_op_t   ops_[FMV_NUM_OPS];
    Adsr      env_;
    SvfFilter filter_;

    float sr_          = SAMPLE_RATE;
    float baseFreq_    = 60.0f;
    float velocity_    = 1.0f;
    float veloMod_     = 0.5f;
    float volume_      = 1.0f;
    float velocityVol_ = 1.0f;
    float pan_         = 0.0f;
    float panL_        = ONE_DIV_SQRT2;
    float panR_        = ONE_DIV_SQRT2;
    uint8_t note_      = 255;
    uint8_t algo_      = 0;
    bool  useFilter_   = true;
};
