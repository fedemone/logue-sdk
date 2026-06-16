#pragma once

/**
 * @file adsr.h
 * @brief Attack/Hold/Decay/Sustain/Release envelope.
 *
 * Re-implementation of the `Adsr` interface used by copych's FmVoice6
 * (the original `Adsr.h` lives in a shared library outside the FMDrums
 * sketch).  Public method names and the END_* end-modes are kept identical
 * so the ported voice code reads the same as the upstream source.
 *
 * Segments:
 *   ATTACK   : linear 0 -> 1            over attack seconds
 *   HOLD     : stay at 1                for hold seconds
 *   DECAY    : exponential 1 -> sustain over decay seconds
 *   SUSTAIN  : hold sustain level until end() is called
 *   RELEASE  : exponential -> 0         over release seconds
 *
 * Percussion patches typically use sustain == 0, so DECAY runs straight into
 * silence and a note naturally frees itself (isRunning() turns false).
 */

#include <cstdint>
#include "constants.h"

class Adsr {
public:
    enum EndMode : uint8_t {
        END_NOW       = 0,  // jump straight to idle / immediate restart
        END_REGULAR   = 1,  // normal note-off release
        END_SEMI_FAST = 2,  // faster release used for choke groups
        END_FAST      = 3,
    };

    enum Stage : uint8_t { IDLE, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE };

    void init(float sampleRate) {
        sr_ = (sampleRate > 1.0f) ? sampleRate : 48000.0f;
        inv_sr_ = 1.0f / sr_;
        stage_ = IDLE;
        level_ = 0.0f;
    }

    // ---- parameter setters (seconds) ---------------------------------------
    void setAttackTime(float s)  { attack_  = clampPos(s); }
    void setHoldTime(float s)    { hold_    = clampPos(s); }
    void setDecayTime(float s)   { decay_   = clampPos(s); }
    void setSustainLevel(float l){ sustain_ = (l < 0.0f) ? 0.0f : (l > 1.0f ? 1.0f : l); }
    void setReleaseTime(float s) { release_ = clampPos(s); }

    float getAttackTime()  const { return attack_; }
    float getHoldTime()    const { return hold_; }
    float getDecayTime()   const { return decay_; }
    float getSustainLevel()const { return sustain_; }
    float getReleaseTime() const { return release_; }

    // ---- transport ---------------------------------------------------------
    void retrigger(EndMode /*mode*/ = END_NOW) {
        stage_   = ATTACK;
        level_   = 0.0f;
        holdCnt_ = 0.0f;
        // Per-sample increments for the current times.
        atkInc_  = (attack_ > 0.0f) ? (inv_sr_ / attack_) : 1.0f;
        holdLen_ = hold_ * sr_;
        decCoef_ = expCoef(decay_);
        relReady_ = false;
    }

    void end(EndMode mode = END_REGULAR) {
        if (stage_ == IDLE) return;
        float rel = release_;
        if (mode == END_NOW)            { stage_ = IDLE; level_ = 0.0f; return; }
        else if (mode == END_SEMI_FAST) rel *= 0.35f;
        else if (mode == END_FAST)      rel *= 0.12f;
        relCoef_  = expCoef(rel);
        stage_    = RELEASE;
        relReady_ = true;
    }

    // ---- per-sample hot path ----------------------------------------------
    fast_inline float process() {
        switch (stage_) {
            case ATTACK:
                level_ += atkInc_;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = HOLD; }
                break;
            case HOLD:
                if (holdCnt_ >= holdLen_) stage_ = DECAY;
                holdCnt_ += 1.0f;
                break;
            case DECAY:
                level_ = sustain_ + (level_ - sustain_) * decCoef_;
                if (level_ <= sustain_ + 1e-5f) {
                    level_ = sustain_;
                    stage_ = (sustain_ > 1e-5f) ? SUSTAIN : IDLE;
                }
                break;
            case SUSTAIN:
                break;
            case RELEASE:
                level_ *= relCoef_;
                if (level_ <= 1e-5f) { level_ = 0.0f; stage_ = IDLE; }
                break;
            case IDLE:
            default:
                return 0.0f;
        }
        return level_;
    }

    bool  isRunning() const { return stage_ != IDLE; }
    float level()     const { return level_; }

    // Voice-stealing heuristic: lower = better steal candidate.
    float getPenalty() const {
        if (stage_ == IDLE)    return 0.0f;
        if (stage_ == RELEASE) return 0.25f;
        return 1.0f;
    }

private:
    static float clampPos(float s) { return (s < 0.0f) ? 0.0f : s; }

    // Exponential decay coefficient reaching ~ -60 dB in `t` seconds.
    float expCoef(float t) const {
        if (t <= 0.0f) return 0.0f;
        // y[n] = y[n-1] * c ; c = exp(-1 / (t * sr / k)) with k ~ 6.9 (≈ ln(1000))
        return std::exp(-6.9078f * inv_sr_ / t);
    }

    float sr_ = 48000.0f, inv_sr_ = 1.0f / 48000.0f;
    float attack_ = 0.001f, hold_ = 0.0f, decay_ = 0.2f, sustain_ = 0.0f, release_ = 0.1f;
    float level_ = 0.0f;
    float atkInc_ = 1.0f, holdLen_ = 0.0f, holdCnt_ = 0.0f;
    float decCoef_ = 0.0f, relCoef_ = 0.0f;
    bool  relReady_ = false;
    Stage stage_ = IDLE;
};
