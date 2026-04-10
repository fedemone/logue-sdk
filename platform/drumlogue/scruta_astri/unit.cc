#include "unit.h"
#include "synth.h"
#include <cstdio>

static ScrutaAstri s_synth;

__unit_callback int8_t unit_init(const unit_runtime_desc_t * desc) {
    return s_synth.Init(desc);
}

__unit_callback void unit_teardown() { }
__unit_callback void unit_reset() { s_synth.Reset(); }
__unit_callback void unit_resume() { }
__unit_callback void unit_suspend() { s_synth.Reset(); }

__unit_callback void unit_render(const float * in, float * out, uint32_t frames) {
    s_synth.processBlock(out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
    s_synth.setParameter(id, value);
}

__unit_callback void unit_load_preset(uint8_t idx) {
  return s_synth.setParameter(k_paramProgram, idx);
}

__unit_callback uint8_t unit_get_preset_index() {
  return s_synth.getParameter(k_paramProgram);
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
    return s_synth.getParameter(id);
}

__unit_callback const char * unit_get_param_str_value(uint8_t id, int32_t value) {
    static char buf[16];

    // Format Cutoffs dynamically (value * 10)
    if (id == ScrutaAstri::k_paramF1Cutoff || id == ScrutaAstri::k_paramF2Cutoff) {
        snprintf(buf, sizeof(buf), "%d Hz", (int)(value * 10));
        return buf;
    }

    // Format SubOctave cleanly
    if (id == ScrutaAstri::k_paramO2SubOct) {
        if (value == 0) return "Unison";
        if (value == 1) return "-1 Oct";
        if (value == 2) return "-2 Oct";
        if (value == 3) return "+1 Oct";
    }

    return nullptr; // Let OS handle everything else natively
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) {
    s_synth.NoteOn(note, velocity);
}

__unit_callback void unit_note_off(uint8_t note) { }
__unit_callback void unit_gate_on(uint8_t velocity) { }
__unit_callback void unit_gate_off() { }
__unit_callback void unit_all_note_off() { }
__unit_callback void unit_pitch_bend(uint16_t bend) { }
__unit_callback void unit_channel_pressure(uint8_t pressure) { }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) { }

__unit_callback void unit_set_tempo(uint32_t tempo) { (void)tempo; }

__unit_callback const uint8_t * unit_get_param_bmp_value(uint8_t id, int32_t value) {
    (void)id; (void)value;
    return nullptr;
}

__unit_callback const char * unit_get_preset_name(uint8_t idx) {
    (void)idx;
    return nullptr;
}