// FmClapModel.cpp
#include "FmClapModel.h"

void FmClapModel::Init() {
    // Reseed first so every trigger is bit-identical (RT-safe / reproducible
    // contract) — including the randomized start phase below.
    drum_rng_seed(&rng_, 0xC1A90001u);
    t = 0.0f;
    t_fm = 0.0f;  // FM timer — never resets between bursts
    clap_stage = 0;
    clap_timer = 0.0f;
    float white = drum_rng_bipolar(&rng_);
    mod_phase = car_phase = HALF_PI + white * 0.2f;
    prev_mod = 0.0f;
    b0 = b1 = b2 = a1 = a2 = 0.0f;
    // x_prev = y_prev = 0.0f;
    x2 = x1 = y2 = y1 = 0.0f;
    active = true;
}

void FmClapModel::Trigger() {
    Init();
    updateOmegas();  // pick up the current note's pitch_ratio_
}

void FmClapModel::updateFilterCoeffs(float fc, float q) {
    if (q == 0) return;
    float w0 = 2.0f * PI * fc * INV_SAMPLE_RATE;
    float alpha = fastersinfullf(w0) * 0.5f / q;
    float inv_a0 = 1.0f / (1.0f + alpha);

    b0 = alpha * inv_a0;
    b1 = 0.0f;
    b2 = -alpha * inv_a0;
    a1 = -2.0f * fastercosfullf(w0) * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;
}


inline float FmClapModel::processHPF(float x)
{
    float y =
        b0 * x +
        b1 * x1 +
        b2 * x2 -
        a1 * y1 -
        a2 * y2;

    x2 = x1;
    x1 = x;

    y2 = y1;
    y1 = y;

    return y;
}

float FmClapModel::Process() {
    if (!active) return 0.0f;

    float decay = (clap_stage < clap_count) ? d1 : d2;

    // Per-burst noise envelopes: reset to 1.0 at each burst boundary so the
    // noise body follows the multi-burst rhythm.
    float amp_env = ExpDecay(t, decay);                  // slow noise body
    float noise_env = ExpDecay(t, decay * 0.066667f);    // fast noise attack
    if (amp_env < 1e-6f)  amp_env = 0;

    // FM envelopes: use t_fm which is tied to note-on and NEVER resets between
    // bursts. This prevents the FM carrier re-attacking at each burst boundary
    // (the "guiro" artifact where each burst produces a new pitched transient).
    float fm_attack = 1.0f - ExpDecay(t_fm, CLAP_ATTACK_TIME);
    float mod_env = fm_attack * ExpDecay(t_fm, d_m);
    if (mod_env < 1e-6f)  mod_env = 0;

    // FM synthesis
    float mod_feedback = bm * prev_mod;
    mod_phase = WrapPhase(mod_phase + omega_m + mod_feedback);
    float mod_out = fastersinfullf(mod_phase);
    prev_mod = mod_out;

    car_phase = WrapPhase(car_phase + omega_c + (I * mod_env) * mod_out);
    float tone = fastersinfullf(car_phase);

    float white = drum_rng_bipolar(&rng_);

    // Noise body: band-passed noise on the per-burst slow envelope.
    float burst = white * noise * amp_env;
    float filteredNoise = processHPF(burst);

    // FM snap: also uses t_fm so it fires only once at note-on, not at every
    // burst boundary. noise_env gives it a fast attack-decay shape within that
    // single window.
    float snap_env = fm_attack * ExpDecay(t_fm, d_m);
    float snap = tone * snap_env * (1.0f - noise);

    float output = filteredNoise + snap;

    t    += INV_SAMPLE_RATE;
    t_fm += INV_SAMPLE_RATE;
    clap_timer += INV_SAMPLE_RATE;
    if (clap_timer >= clap_interval) {
        ++clap_stage;
        t = 0.0f;           // reset per-burst noise envelope only
        clap_timer = 0.0f;  // t_fm keeps running
        if (clap_stage >= clap_count + 1)
            active = false;
    }

    return output;
}

// f_b >> f_m >> I >> d_m >> d1 >> d2 >> clap_count >> clap_interval >> fhp >> bm;
void FmClapModel::loadPreset(uint8_t idx) {
    switch (idx) {
        case 0:
        // "234.804 1066.67 3.431 0.17 0.023 0.3 2 0.028 786.765 1\n";
          f_b = 234.901f;  f_m = 1066.67f;  I = 3.431f;  d_m = 0.0087f;  d1 = 0.023f;  d2 = 0.3f;  clap_count = 2;  clap_interval = 0.028f;  fhp = 886.765f;  bm = 1.0f;  noise = 0.6f;   Q = 2.3f;
          break;
        case 1:
        // 176.64 1585.66 15.164 0.095 0.01 0.09 3 0.034 953.197 0.018
          f_b = 176.73f; f_m = 1585.65f; I = 15.164f; d_m = 0.0095f; d1 = 0.01f; d2 = 0.09f; clap_count = 3; clap_interval = 0.034f; fhp = 1153.197f; bm = 0.018f;  noise = 0.7f;   Q = 2.0f;
          break;
        // case 2: - maybe in the future
    }
    updateFilterCoeffs(fhp, Q);
};

void FmClapModel::setParameter(fm_param_index_t param_index, float value) {
    // user editable parameters are in range 0..100
    switch (param_index) {
        case K_Base_Frequency:
        // ParameterSlider("f_b (Base Freq)", &f_b, 100.0f, 1200.0f);
            f_b = 100.0f + value * 5.517f;    //0..200 - decimals increasing atonal f_b / f_m ratios
            updateOmegas();
            break;
        case K_Modulation_Frequency:
        // ParameterSlider("f_m (Mod Freq)", &f_m, 100.0f, 3000.0f);
            f_m = 100.0f + value * 29.39f;   //  - decimals increasing atonal f_b / f_m ratios
            updateOmegas();
            break;
        case K_Modulation_Feedback:
        // ParameterSlider("bm (Mod Feedback)", &bm, 0.0f, 1.0f);
            bm = value * 0.01f;
            break;
        case K_Modulation_Index:
        // ParameterSlider("I (Mod Index)", &I, 0.0f, 100.0f);
            I = value * 0.5f;   //0..200
            break;
        case K_Modulation_Decay:
        // ParameterSlider("d_m (Mod Decay)", &d_m, 0.001f, 0.2f);
            d_m = 0.001f + value * 0.00199f;
            break;
        case K_Decay_A:   // 0..200
        // ParameterSlider("d1 (Pre-Clap Decay)", &d1, 0.005f, 0.6f);
            d1 = 0.005f + value * 0.002975f;
            break;
        case K_Decay_B:
        // ParameterSlider("d2 (Final Clap Decay)", &d2, 0.01f, 0.9f);
        // now sets the band-passed-noise tail length; default (50) -> ~0.22 s
            d2 = 0.02f + value * 0.004f;
            break;
        case K_Gap:
        // ParameterSlider("clap_interval (s)", &clap_interval, 0.005f, 0.05f);
            clap_interval = 0.005f + value * 0.00045f;
            break;
        case K_Count:
        // ParameterSliderInt("clap_count", &clap_count, 1, 6);
            clap_count = value;
            break;
        case K_Noise_Level:
            noise = value * 0.01f;
            break;
        case K_HPF:
        // ParameterSlider("fhp (HPF Cutoff)", &fhp, 20.0f, 2000.0f);
            fhp = 20.0f + value * 19.80f;
            updateFilterCoeffs(fhp, Q);
            break;
        case K_Frequency_Sweep:
            Q = 0.1 + value * 0.039; // 0.1..4.0
            updateFilterCoeffs(fhp, Q);
        default:
            break;
    }
};
float FmClapModel::getParameter(fm_param_index_t param_index) {
    // user editable parameters are in range 0..100
    switch (param_index) {
        case K_Base_Frequency:
            return f_b;
            break;
        case K_Modulation_Frequency:
            return f_m;
            break;
        case K_Modulation_Feedback:
            return bm;
            break;
        case K_Modulation_Index:
            return I;
            break;
        case K_Modulation_Decay:
            return d_m;
            break;
        case K_Decay_A:   // 0..200
            return d1;
            break;
        case K_Decay_B:
            return d2;
            break;
        case K_Gap:
            return clap_interval;  // seconds
            break;
        case K_Count:
            return clap_count;
            break;
        case K_Noise_Level:
            return noise;
            break;
        case K_HPF:
            return fhp; // Hz
        case K_Frequency_Sweep:
            return Q;
            break;
        default:
            return 255.0f;  // invalid
            break;
    }
};

// void FmClapModel::RenderControls() {
//     CustomControls::ParameterSlider("f_b (Base Freq)", &f_b, 100.0f, 1200.0f);
//     CustomControls::ParameterSlider("f_m (Mod Freq)", &f_m, 100.0f, 3000.0f);
//     CustomControls::ParameterSlider("bm (Mod Feedback)", &bm, 0.0f, 1.0f);
//     CustomControls::ParameterSlider("I (Mod Index)", &I, 0.0f, 100.0f);
//     CustomControls::ParameterSlider("d_m (Mod Decay)", &d_m, 0.01f, 1.0f);
//     CustomControls::ParameterSlider("d1 (Pre-Clap Decay)", &d1, 0.005f, 0.6f);
//     CustomControls::ParameterSlider("d2 (Final Clap Decay)", &d2, 0.01f, 0.9f);
//     CustomControls::ParameterSliderInt("clap_count", &clap_count, 1, 6);
//     CustomControls::ParameterSlider("clap_interval (s)", &clap_interval, 0.005f, 0.05f);
//     CustomControls::ParameterSlider("fhp (HPF Cutoff)", &fhp, 20.0f, 2000.0f);
// }