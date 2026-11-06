#pragma once

// ENV_ATTACK_REL = "attack, then release" — a release that arrived before the
// attack had produced anything.  See FastEnvelope::release().
enum EnvState { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_RELEASE, ENV_ATTACK_REL };

/**
 * Fast, exponentially-curved AR/ADSR Envelope.
 */
struct FastEnvelope {
    EnvState state = ENV_IDLE;
    float value = 0.0f;

    // Rates (Calculated in UI thread: e.g., rate = 1.0 - exp(-1.0 / (time_ms * srate)))
    float attack_rate = 0.01f;
    float decay_rate = 0.001f;
    float release_rate = 0.005f;

    // Levels
    float sustain_level = 0.0f; // 0.0 makes it an AR envelope (Percussive)

    inline void trigger() {
        state = ENV_ATTACK;   // clears any pending deferred release
        value = 0.0f;         // Reset phase
    }

    inline void release() {
        // A release during the attack must be DEFERRED, not applied.  The
        // Drumlogue fires gate_on and gate_off in the same scheduler tick,
        // before any audio block, so trigger() has set value=0 and process()
        // has not run yet.  Going straight to ENV_RELEASE from there is fatal:
        // the first process() computes value += (0 - value)*release_rate,
        // which is still 0, hits the `value <= 0.001f` cutoff and jumps to
        // ENV_IDLE — the envelope dies before it ever opens.  That silenced
        // the whole Clap/Shaker/HHat-C (ENGINE_NOISE) voice on hardware while
        // the host renders, which hold the gate ~50 ms, sounded correct.
        // (master_env dodges this by being force-set to 1.0/ENV_DECAY in
        // NoteOn — see T20 — and ENGINE_SNARE by skipping the call entirely.)
        // Completing the attack first keeps the release itself meaningful, so
        // the Rel knob still shapes the tail, and leaves any envelope that is
        // already open bit-for-bit unchanged.
        if (state == ENV_ATTACK) {
            state = ENV_ATTACK_REL;
        } else if (state != ENV_IDLE && state != ENV_ATTACK_REL) {
            state = ENV_RELEASE;
        }
    }

    inline float process() {
        switch (state) {
            case ENV_IDLE:
                break;
            case ENV_ATTACK:
            case ENV_ATTACK_REL:
                value += (1.0f - value) * attack_rate;
                if (value >= 0.99f) {
                    value = 1.0f;
                    // A deferred release takes effect at the top of the attack.
                    state = (state == ENV_ATTACK) ? ENV_DECAY : ENV_RELEASE;
                }
                break;
            case ENV_DECAY:
                value += (sustain_level - value) * decay_rate;
                // When sustain target is 0 (auto-decay mode), transition to
                // IDLE once below audibility — same threshold as ENV_RELEASE.
                // Without this, the voice stays active forever, consuming CPU.
                if (sustain_level < 0.001f && value <= 0.001f) {
                    value = 0.0f;
                    state = ENV_IDLE;
                }
                break;
            case ENV_RELEASE:
                value += (0.0f - value) * release_rate;
                if (value <= 0.001f) {
                    value = 0.0f;
                    state = ENV_IDLE;
                }
                break;
        }
        return value;
    }
};