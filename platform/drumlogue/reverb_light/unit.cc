/**
 * @file unit.cc
 * @brief drumlogue SDK unit interface for Light Reverb
 *
 * FIXED:
 * - Removed dynamic allocation (no 'new')
 * - Static instance only
 * - Proper deinterleaving of stereo buffers
 * - Safe bounds checking
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "unit.h"
#include "fdn_engine.h"

// ============================================================================
// Constants
// ============================================================================

// Maximum frames per render call (drumlogue typically uses 64 or 128)
constexpr uint32_t kMaxFrames = 256;

// ============================================================================
// Static Instances (No dynamic allocation!)
// ============================================================================

static FDNEngine s_fdn_engine;
static unit_runtime_desc_t s_runtime_desc;
static bool s_initialized = false;
static bool s_bypass = true;
static const int num_of_presets = 4;
enum k_parameters {
    k_name, k_dark, k_bright, k_glow,
    k_color, k_spark, k_size, k_pdly,
    k_total
};

// ============================================================================
// Presets
// ============================================================================
static const char* k_preset_names[num_of_presets] = {
    "StanzaNeon", // 0: Tight, bright, standard drum room
    "VicoBuio",   // 1: Long decay, heavy LPF, spooky
    "Schiaffo",   // 2: High pre-delay, short decay, heavily modulated
    "Abisso"      // 3: Massive size, max decay, floating
};

// Values map to: { DARK, BRIG, GLOW, COLR, SPRK, SIZE, PDLY }
static const int32_t k_preset_values[num_of_presets][k_total] = {
    { 0, 40, 70, 30, 10,  5, 30,  5 },  // StanzaNeon
    { 1, 80, 20, 40, 80, 10, 60, 15 },  // VicoBuio
    { 2, 20, 50, 40, 40, 40, 10, 80 },  // Schiaffo
    { 3, 95, 40, 60, 30, 25, 90, 10 }   // Abisso
};

static uint8_t s_current_preset = 0;

// ============================================================================
// Parameter State (mirrors header.c defaults)
// ============================================================================
// ID 0: NAME  string   default 0
// ID 1: DARK  0..100 %  default 20
// ID 2: BRIG  0..100 %  default 50
// ID 3: GLOW  0..100 %  default 30
// ID 4: COLR  0..100 %  default 10
// ID 5: SPRK  0..100 %  default 5
// ID 6: SIZE  0..100 %  default 50
// ID 7: PDLY  0..100 %  default 50
static int32_t s_params[k_total] = { 0, 60, 50, 70, 10, 5, 50, 0 };

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

    // Initialize the FDN engine (static instance - no allocation!)
    if (!s_fdn_engine.init(desc->samplerate)) {
        // Allocation failed within FDN engine - unit will bypass
        s_initialized = false;
        s_bypass = true;
        return k_unit_err_memory;
    }

    s_initialized = true;
    s_bypass = false;

    // Apply default parameter values
    for (uint8_t i = 0; i < k_total; i++) {
        unit_set_param_value(i, s_params[i]);
    }

    return k_unit_err_none;
}

__unit_callback void unit_teardown() {
    // Nothing to do - static instance cleans up itself
    // The FDNEngine destructor will be called when program exits
}

__unit_callback void unit_reset() {
    // Reset FDN engine state if needed
    // s_fdn_engine.reset(); // Would need a reset method
}

__unit_callback void unit_resume() {}
__unit_callback void unit_suspend() {}

__unit_callback void unit_render(const float* in, float* out, uint32_t frames) {
    // ========================================================================
    // Safety Check: Bypass if not initialized
    // ========================================================================
    if (!s_initialized || s_bypass) {
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
    s_fdn_engine.processStereo(s_inL, s_inR, s_outL, s_outR, frames);

    // ========================================================================
    // Interleave output: separate L/R buffers -> [L,R,L,R,...]
    // ========================================================================
    for (uint32_t i = 0; i < frames; i++) {
        out[i * 2] = s_outL[i];
        out[i * 2 + 1] = s_outR[i];
    }
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
    if (id >= k_total) return;
    s_params[id] = value;

    const float norm = value / 100.0f;  // 0..100 → 0.0..1.0

    switch (id) {
        case k_dark: // DARK  decay/warmth  0-100% → decay 0.0..0.99
            s_fdn_engine.setDecay(norm * 0.99f);
            break;
        case k_bright: // BRIG  brightness  0-100% → 0.0..1.0
            s_fdn_engine.setBrightness(norm);
            break;
        case k_glow: // GLOW  wet/dry mix  0-100% → 0.0..1.0
            s_fdn_engine.setGlow(norm);
            break;
        case k_color: // COLR  tone color (LPF)  0-100% → coeff 0.0..0.95
            s_fdn_engine.setColor(norm * 0.95f);
            break;
        case k_spark: // SPRK  sparkle / modulation depth  0-100% → 0.0..1.0
            s_fdn_engine.setModulation(norm);
            break;
        case k_size: // SIZE  room size  0-100% → scale 0.1..2.0
            s_fdn_engine.setSize(0.1f + norm * 1.9f);
            break;
        case k_pdly: // PDLY pre delay
            s_fdn_engine.setPreDelay(norm * 200.0f);
            break;
        default:
            break;
    }
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
    if (id >= k_total) return 0;
    return s_params[id];
}

__unit_callback const char* unit_get_param_str_value(uint8_t id, int32_t value) {
    if ((id == k_name) && (value < num_of_presets)) {
        return k_preset_names[value];
    }
    (void)id;
    (void)value;
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

__unit_callback uint8_t unit_get_preset_index() {
    return s_current_preset;
}

__unit_callback const char* unit_get_preset_name(uint8_t idx) {
    if (idx >= num_of_presets) return nullptr;
    return k_preset_names[idx];
}

__unit_callback void unit_load_preset(uint8_t idx) {
    if (idx >= num_of_presets) return;
    s_current_preset = idx;

    // Apply all parameter values from the preset
    for (uint8_t p = 0; p < k_total; p++) {
        // Update the internal state array
        s_params[p] = k_preset_values[idx][p];
        // Trigger the logic to update the FDN engine
        unit_set_param_value(p, s_params[p]);
    }
}