/**
 * @file unit.cc
 * @brief drumlogue SDK unit interface — Brachetti (all twelve families)
 *
 * The presets are the families; Program picks the instrument inside the
 * selected family.  brachetti_synth.h does the routing.
 */

#include "unit.h"
#include "brachetti_synth.h"

// Static: allocated with the unit, no real-time allocation.
static brachetti::Synth s_synth;

__unit_callback int8_t unit_init(const unit_runtime_desc_t * desc) {
    if (!desc) return k_unit_err_undef;
    if (desc->target != unit_header.target) return k_unit_err_target;
    if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
    return s_synth.Init(desc);
}

__unit_callback void unit_teardown() { s_synth.Teardown(); }
__unit_callback void unit_reset()    { s_synth.Reset(); }
__unit_callback void unit_resume()   { s_synth.Resume(); }
__unit_callback void unit_suspend()  { s_synth.Suspend(); }

__unit_callback void unit_render(const float * in, float * out, uint32_t frames) {
    (void)in;
    s_synth.Render(out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) { s_synth.SetParameter(id, value); }
__unit_callback int32_t unit_get_param_value(uint8_t id) { return s_synth.GetParameter(id); }
__unit_callback const char * unit_get_param_str_value(uint8_t id, int32_t value) {
    return s_synth.GetParameterText(id, value);
}
__unit_callback const uint8_t * unit_get_param_bmp_value(uint8_t id, int32_t value) {
    (void)id; (void)value;
    return nullptr;
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velocity) { s_synth.NoteOn(note, velocity); }
__unit_callback void unit_note_off(uint8_t note) { s_synth.NoteOff(note); }
__unit_callback void unit_gate_on(uint8_t velocity) { s_synth.GateOn(velocity); }
__unit_callback void unit_gate_off() { s_synth.GateOff(); }
__unit_callback void unit_all_note_off() { s_synth.AllNoteOff(); }
__unit_callback void unit_pitch_bend(uint16_t bend) { s_synth.PitchBend(bend); }
__unit_callback void unit_channel_pressure(uint8_t pressure) { (void)pressure; }
__unit_callback void unit_aftertouch(uint8_t note, uint8_t aftertouch) { (void)note; (void)aftertouch; }

// The presets are the families.
__unit_callback void unit_load_preset(uint8_t idx) { s_synth.LoadPreset(idx); }
__unit_callback uint8_t unit_get_preset_index() { return s_synth.PresetIndex(); }
__unit_callback const char * unit_get_preset_name(uint8_t idx) { return brachetti::Synth::PresetName(idx); }
