fast_inline float fm_perc_synth_process(fm_perc_synth_t* synth) {
    // 1) Advance the shared envelope.
    // Replace neon_envelope_process() with the actual step function name in envelope_rom.h.
    const float32x4_t env = neon_envelope_process(&synth->envelope);

    const float32x4_t env2 = vmulq_f32(env, env);
    const float32x4_t env4 = vmulq_f32(env2, env2);
    const float32x4_t env8 = vmulq_f32(env4, env4);

    // 2) Advance / smooth LFO outputs.
    // If your LFO API uses different function names, swap these two lines only.
    synth->lfo_pitch_mult = lfo_smoother_process_pitch(&synth->lfo_smooth);
    synth->lfo_index_add  = lfo_smoother_process_index(&synth->lfo_smooth);

    // 3) Build the active mask.
    // In the fixed-4-engine design, all four lanes are always the four engines.
    // The individual engine functions still receive the active mask for lane gating.
    const uint32x4_t active_mask = vdupq_n_u32(0xFFFFFFFFu);

    // 4) Process each engine.
    // These signatures follow the versions already shown in the engine files.
    // If your kick/metal signatures differ slightly, only the call sites need editing.
    float32x4_t kick_out = kick_engine_process(&synth->kick,
                                               env,
                                               active_mask,
                                               synth->lfo_pitch_mult,
                                               synth->lfo_index_add);

    float32x4_t snare_out = snare_engine_process(&synth->snare,
                                                 env,
                                                 active_mask,
                                                 synth->lfo_pitch_mult,
                                                 synth->lfo_index_add,
                                                 vdupq_n_f32(0.0f));

    float32x4_t metal_out = metal_engine_process(&synth->metal,
                                                 env,
                                                 active_mask,
                                                 synth->lfo_pitch_mult,
                                                 synth->lfo_index_add);

    float32x4_t perc_out = perc_engine_process(&synth->perc,
                                               env,
                                               active_mask,
                                               synth->lfo_pitch_mult,
                                               synth->lfo_index_add);

    // 5) Global macro glue.
    // Keep this conservative at first. The mapping layer should already have made
    // the engines behave perceptually correctly, so this stage just helps coherence.
    float32x4_t body_glue = vaddq_f32(vmulq_n_f32(kick_out, 0.25f),
                                      vmulq_n_f32(snare_out, 0.20f));
    body_glue = vaddq_f32(body_glue, vmulq_n_f32(metal_out, 0.18f));
    body_glue = vaddq_f32(body_glue, vmulq_n_f32(perc_out, 0.22f));

    // Attack emphasis from the shared transient domain.
    float32x4_t attack_glue = vaddq_f32(vmulq_n_f32(kick_out, 0.15f),
                                        vmulq_n_f32(snare_out, 0.20f));
    attack_glue = vaddq_f32(attack_glue, vmulq_n_f32(metal_out, 0.22f));
    attack_glue = vaddq_f32(attack_glue, vmulq_n_f32(perc_out, 0.18f));

    // 6) Optional reserved resonant engine.
    // Kept in code, not called in the active path.
#if 0
    float32x4_t res_out = resonant_synth_process(&synth->resonant, env, active_mask);
    body_glue = vaddq_f32(body_glue, vmulq_n_f32(res_out, 0.0f));
#endif

    // 7) Combine the partials.
    // A little more weight to the body, a little more edge to the attack.
    float32x4_t mix = vaddq_f32(vmulq_n_f32(body_glue, 0.70f),
                                vmulq_n_f32(attack_glue, 0.30f));

    // 8) Global gain and soft saturation.
    // This is the first safe version: punchy but not harshly clipped.
    const float32x4_t drive = vdupq_n_f32(1.0f + 0.75f * synth->params[Synth::PARAM_DRIVE] / 100.0f);
    mix = vmulq_f32(mix, drive);

    // Soft clip / normalize-ish saturation.
    // If you already have a faster helper in fm_voices.h, swap it in.
    mix = vdivq_f32(mix, vaddq_f32(vdupq_n_f32(1.0f), vabsq_f32(mix)));

    mix = vmulq_n_f32(mix, synth->master_gain);

    // 9) Return mono sample from the 4 SIMD lanes.
    // If you want engine-to-lane separation later, this is where the sum policy changes.
    return neon_horizontal_sum_alt(mix);
}