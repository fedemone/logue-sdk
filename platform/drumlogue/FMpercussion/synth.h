#pragma once
/*
 *  File: synth.h
 *  FM Percussion Synthesizer for drumlogue
 *
 *  4-voice FM percussion synth with LFO modulation
 *  Version 1.0
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <arm_neon.h>
#include "unit.h"
#include "fm_perc_synth.h"
#include "fm_presets.h"
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif
// defined in header.c
extern const char* const lfo_shape_strings[9];
extern const char* const lfo_target_strings[11];
extern const char* const resonant_mode_strings[5];
extern const char* const voice_alloc_strings[12];
extern const char* const euclidean_mode_strings[9];
#ifdef __cplusplus
}
#endif

class Synth {
public:
    /*===========================================================================*/
    /* Lifecycle Methods */
    /*===========================================================================*/

    Synth(void) : sample_rate_(48000) {
        fm_perc_synth_init(&synth_);
    }

    ~Synth(void) {}

    inline int8_t Init(const unit_runtime_desc_t* desc) {
        // Check compatibility
        if (desc->samplerate != 48000)
            return k_unit_err_samplerate;

        if (desc->output_channels != 2)
            return k_unit_err_geometry;

        sample_rate_ = desc->samplerate;

        // Initialize synth with default preset
        fm_perc_synth_init(&synth_);

        return k_unit_err_none;
    }

    inline void Teardown() {
        // Nothing to clean up - all static allocation
    }

    inline void Reset() {
        fm_perc_synth_init(&synth_);
    }

    inline void Resume() {}
    inline void Suspend() {}

    /*===========================================================================*/
    /* Audio Render */
    /*===========================================================================*/

    fast_inline void Render(float* out, size_t frames) {
        // Idle gate: if all 4 voice envelopes are in ENV_STATE_OFF, every call to
        // fm_perc_synth_process() returns exactly 0.0, so skip the entire block.
        // Horizontal AND of all 4 stage lanes (ARMv7-compatible):
        uint32x4_t off_check = vceqq_u32(synth_.envelope.stage,
                                          vdupq_n_u32(ENV_STATE_OFF));
        uint32x2_t lo_hi = vand_u32(vget_low_u32(off_check),
                                     vget_high_u32(off_check));
        lo_hi = vpmin_u32(lo_hi, lo_hi);

        if (vget_lane_u32(lo_hi, 0) == 0xFFFFFFFFu) {
            // All voices idle — output silence without running synthesis
            memset(out, 0, frames * 2 * sizeof(float));
            return;
        }

        float* __restrict out_p = out;
        const float* out_e = out_p + (frames << 1);  // Stereo output

        while (out_p < out_e) {
            float sample = fm_perc_synth_process(&synth_);
            out_p[0] = sample;
            out_p[1] = sample;
            out_p += 2;
        }
    }

    /*===========================================================================*/
    /* MIDI Handlers */
    /*===========================================================================*/

    inline void NoteOn(uint8_t note, uint8_t velocity) {
        fm_perc_synth_note_on(&synth_, note, velocity);
    }

    inline void NoteOff(uint8_t note) {
        fm_perc_synth_note_off(&synth_, note);
    }

    inline void GateOn(uint8_t velocity) {
        // If no specific note, trigger default mapping (C2, D2, F#2, A2)
        NoteOn(36, velocity);  // Kick
        NoteOn(38, velocity);  // Snare
        NoteOn(42, velocity);  // Metal
        NoteOn(45, velocity);  // Perc
    }

    inline void GateOff() {
        AllNoteOff();
    }

    inline void AllNoteOff() {
        for (int i = 0; i < 128; i++) {
            fm_perc_synth_note_off(&synth_, i);
        }
    }

    inline void PitchBend(uint16_t bend) {
        (void)bend;  // Not implemented in basic FM percussion
        // Could be used to modulate all voices' pitch
    }

    inline void ChannelPressure(uint8_t pressure) {
        (void)pressure;  // Not implemented
    }

    inline void Aftertouch(uint8_t note, uint8_t aftertouch) {
        (void)note;
        (void)aftertouch;  // Not implemented
    }

    /*===========================================================================*/
    /* Parameter Interface */
    /*===========================================================================*/

    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= 24) return;

        // Store parameter value
        synth_.params[index] = (int8_t)value;

        // Update synth with new parameters
        fm_perc_synth_update_params(&synth_);
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index >= 24) return 0;
        return synth_.params[index];
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {

        switch (index) {
            case 12:  // LFO1 Shape (shape_combo: encodes both LFO1+LFO2 shapes)
                if (value >= 0 && value <= 8) return lfo_shape_strings[value];
                break;
            case 16:  // EuclTun — Euclidean per-voice pitch spread
                if (value >= 0 && value < 9) return euclidean_mode_strings[value];
                break;
            case 14: case 18:  // LFO1 Dest and LFO2 Dest
                if (value >= 0 && value <= 10) return lfo_target_strings[value];
                break;
            case 21:  // Voice Alloc
                if (value >= 0 && value <= 11) return voice_alloc_strings[value];
                break;
            case 22:  // Resonant Mode
                if (value >= 0 && value <= 4) return resonant_mode_strings[value];
                break;
        }
        return nullptr;
    }

    inline const uint8_t* getParameterBmpValue(uint8_t index, int32_t value) const {
        (void)index;
        (void)value;
        return nullptr;  // Not implemented
    }

    /*===========================================================================*/
    /* Preset Management */
    /*===========================================================================*/

    inline void LoadPreset(uint8_t idx) {
      load_preset(idx);
    }

    inline uint8_t getPresetIndex() const {
        return current_preset_;
    }

    static inline const char* getPresetName(uint8_t idx) {
        if (idx < 12) {
            return FM_PRESETS[idx].name;
        }
        return nullptr;
    }

private:
    /*===========================================================================*/
    /* Private Methods */
    /*===========================================================================*/

    inline void load_preset(uint8_t idx) {
        load_fm_preset(idx, synth_.params);
        // Update synth with new parameters
        fm_perc_synth_update_params(&synth_);
        current_preset_ = idx;
    }

    /*===========================================================================*/
    /* Private Member Variables */
    /*===========================================================================*/

    fm_perc_synth_t synth_;
    uint32_t sample_rate_;
    uint8_t current_preset_;
};