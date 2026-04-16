/**
 * @file unit.cc
 * @brief drumlogue SDK unit interface for Labirinto Reverb
 *
 * FIXED:
 * - Proper deinterleaving of input buffers
 * - Correct interleaving of output buffers
 * - Static buffers with bounds checking
 * - Safe bypass mode
 * - FIXED: unit_reset() now calls clear() on reverb
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include "unit.h"
#include "NeonAdvancedLabirinto.h"

// ============================================================================
// Constants
// ============================================================================

// Maximum frames per render call (drumlogue typically uses 64 or 128)
constexpr uint32_t kMaxFrames = 256;

// ============================================================================
// Static Instances
// ============================================================================

static NeonAdvancedLabirinto s_reverb_instance;
static NeonAdvancedLabirinto* s_reverb = &s_reverb_instance;
static unit_runtime_desc_t s_runtime_desc;
static bool s_initialized = false;
static bool s_bypass = true;

static uint8_t s_current_preset = 0;

// ============================================================================
// Parameter State (mirrors header.c defaults)
// ============================================================================
// ID 0:  PRESET 0..3               default 0 (foresta)
// ID 1:  MIX    0..100 %            default 70 (70%)
// ID 2:  TIME   1..100             default 50
// ID 3:  LOW    1..100             default 50
// ID 4:  HIGH   1..100             default 70
// ID 5:  DAMP   20..1000           default 250  (×10 in code → 2500 Hz)
// ID 6:  WIDE   0..200 %           default 100
// ID 7:  COMP   0..1000 (x0.1%)    default 1000
// ID 8:  PILL   0..4               default 3
// ID 9:  SHMR 0..100             default 35 (Hz)
// ID 10: PDLY   0..100             default 0 (ms)
static int32_t s_params[k_total] = {0, 70, 50, 50, 70, 250, 100, 100, 3, 0, 0, 10};

// ============================================================================
// Static Buffers (Safe - allocated in BSS, not on stack)
// ============================================================================

static float s_inL[kMaxFrames];
static float s_inR[kMaxFrames];
static float s_outL[kMaxFrames];
static float s_outR[kMaxFrames];

// ============================================================================
// Callback Implementations
// ============================================================================

__unit_callback int8_t unit_init(const unit_runtime_desc_t* desc) {
    if (!desc) return k_unit_err_undef;
    if (desc->target != unit_header.target) return k_unit_err_target;
    if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;

    s_runtime_desc = *desc;

    // Initialize the reverb (clears delay lines and sets up shimmer tables)
    if (!s_reverb->init()) {
        s_initialized = false;
        s_bypass = true;
        return k_unit_err_memory;
    }

    s_initialized = true;
    s_bypass = false;
    s_current_preset = 0;

    // Apply default parameter values
    s_reverb->setParameter(k_paramProgram, s_current_preset);

    return k_unit_err_none;
}

__unit_callback void unit_teardown() {
    s_initialized = false;
    s_bypass = true;
}

__unit_callback void unit_reset() {
    // FIXED: Clear reverb state when host calls reset
    if (s_reverb) {
        s_reverb->clear();
    }
}

__unit_callback void unit_resume() {}
__unit_callback void unit_suspend() {}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
    // ========================================================================
    // Safety Check: Bypass if not initialized
    // ========================================================================
    if (!s_initialized || !s_reverb || s_bypass) {
        memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }

    // ========================================================================
    // Bounds Check - Prevent buffer overflow
    // ========================================================================
    if (frames > kMaxFrames) {
        // This should never happen with drumlogue, but safety first
        memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }

    // ========================================================================
    // Deinterleave input: [L,R,L,R,...] -> separate L and R buffers
    // ========================================================================
    for (uint32_t i = 0; i < frames; i++) {
        s_inL[i] = in[i * 2];
        s_inR[i] = in[i * 2 + 1];
    }

    // ========================================================================
    // Process through reverb engine (expects deinterleaved buffers)
    // ========================================================================
    s_reverb->process(s_inL, s_inR, s_outL, s_outR, frames);

    // ========================================================================
    // Interleave output: separate L/R buffers -> [L,R,L,R,...]
    // ========================================================================
    for (uint32_t i = 0; i < frames; i++) {
        out[i * 2] = s_outL[i];
        out[i * 2 + 1] = s_outR[i];
    }
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
    if (!s_reverb) return;
    if (id >= k_total) return;
    s_params[id] = value;   // store into local DB
    if (id == k_paramProgram) s_current_preset = s_params[k_paramProgram];
    s_reverb->setParameter(id, value);
}


__unit_callback int32_t unit_get_param_value(uint8_t id) {
    if (id >= k_total) return 0;
    return s_params[id];
}

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
  static char sf_buf[10];
    if ((id == k_paramProgram) && (value < k_preset_number)) {
        return k_preset_names[value];
    }
    (void)id;
    (void)value;
    if (id == k_shimmer_freq)
    {
        int32_t hz_x10 = (int32_t)(s_reverb->getShimmerFreq() * 10.0f);
        snprintf(sf_buf, sizeof(sf_buf), "%d.%dHz", hz_x10 / 10, hz_x10 % 10);
        return sf_buf;
    }
    return nullptr;
}

__unit_callback const uint8_t* unit_get_param_bmp_value(uint8_t id, int32_t value) {
    (void)id;
    (void)value;
    return nullptr;
}

// Unused MIDI callbacks
__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }
__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) { (void)note; (void)velocity; }
__unit_callback void unit_note_off(uint8_t note) { (void)note; }
__unit_callback void unit_gate_on(uint8_t velocity) { (void)velocity; }
__unit_callback void unit_gate_off() {}
__unit_callback void unit_all_note_off() {}
__unit_callback void unit_pitch_bend(uint16_t bend) { (void)bend; }
__unit_callback void unit_channel_pressure(uint8_t pressure) { (void)pressure; }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) {
    (void)note;
    (void)aftertouch;
}

__unit_callback void unit_load_preset(uint8_t idx) {
    if (idx >= k_preset_number) return;
    s_reverb->setParameter(k_paramProgram, idx);
}

__unit_callback uint8_t unit_get_preset_index() {
    return (s_current_preset < k_preset_number) ? s_current_preset : 0;
}

__unit_callback const char* unit_get_preset_name(uint8_t idx) {
    if (idx >= k_preset_number) return nullptr;
    return k_preset_names[idx];
}
