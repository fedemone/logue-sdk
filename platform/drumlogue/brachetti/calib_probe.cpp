/**
 * calib_probe.cpp — calibrate kPresetOutTrim[].
 *
 * Stage 4b is a peak limiter with a 0.99 ceiling, and the voice bus arrives at
 * it between 0.5x and 116x full scale depending on the preset.  Nothing can be
 * transparent across a 47 dB spread: the presets at the top of it are not
 * "limited", they are levelled — the limiter holds them AT the ceiling for the
 * whole note, so the instrument's decay never happens and every strike ducks
 * whatever is still ringing.  See CLAUDE.md, "Pass 47".
 *
 * The trim that fixes that is calibrated on the note's BODY, not on its strike
 * transient.  Limiting a 2 ms mallet transient by 15 dB is what percussion
 * mastering does and is inaudible; holding the body 20 dB down for 500 ms is
 * the defect.  So: strike each preset ONCE at its own shipped Note through
 * GateOn (the sequencer path) at velocity 127 — the loudest thing a player can
 * send — with the master knee bypassed, and take the peak over [10 ms, 1 s].
 * The trim puts that on kBodyTarget, just at the limiter's threshold, which
 * leaves the strike free to use the headroom above it.
 *
 * The trim is clamped to <= 1: this only ever takes back drive the master
 * stage could not use, it never makes a preset louder, so a preset that
 * already fits (Cymbal, Ride, RidBel, HHat-O, Claves, Splash) keeps a trim of
 * exactly 1.0 and renders bit-identically.
 *
 * Build (from the brachetti directory):
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       -DBRACHETTI_MASTER_PROBE calib_probe.cpp -o /tmp/calib_probe
 * Run:
 *   /tmp/calib_probe            # 1.5, the shipping target; ready to paste
 *   /tmp/calib_probe 1.0        # a different body target
 *
 * It is IDEMPOTENT: the probe measures what the engine currently delivers,
 * which already includes kPresetOutTrim, so it composes the trim it measured
 * through back into what it prints.  Running it against a calibrated tree
 * reprints the same table; running it after changing a preset's voicing
 * (Kick's and DeepBs's decay changes move their body level)
 * reprints that preset's entry corrected.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0.f, ut_delay_read = 0.f, ut_voice_out = 0.f;
float g_mp_bus_peak = 0.f, g_mp_pre_peak = 0.f, g_mp_gr_min = 1.f;
long  g_mp_wall_hits = 0, g_mp_samples = 0, g_mp_pinned = 0;
float g_mp_trim = 1.0f;
bool  g_mp_bypass = false;
#include "synth_engine.h"

static const int   kSR   = 48000;
static const float kDur  = 6.0f;    // covers every preset's body window
static const float kBodyLo = 0.010f, kBodyHi = 1.000f;

int main(int argc, char** argv) {
    // Default is the SHIPPING body target, so a bare run reproduces the
    // table in synth_engine.h rather than a different calibration.
    const float target = (argc > 1) ? (float)atof(argv[1]) : 1.5f;
    const int total = (int)(kDur * kSR);
    const int lo = (int)(kBodyLo * kSR), hi = (int)(kBodyHi * kSR);
    std::vector<float> body(BrachettiSynth::k_NumPrograms, 0.f), attack(BrachettiSynth::k_NumPrograms, 0.f),
                       trim(BrachettiSynth::k_NumPrograms, 1.f);

    for (int idx = 0; idx < BrachettiSynth::k_NumPrograms; ++idx) {
        BrachettiSynth s;
        unit_runtime_desc_t d = {};
        d.samplerate = kSR; d.output_channels = 2;
        d.get_num_sample_banks = mock_get_num_sample_banks;
        d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
        d.get_sample = mock_get_sample;
        s.Init(&d); s.LoadPreset(idx);
        std::vector<float> o(total, 0.f);
        float st[256 * 2];
        g_mp_bypass = true;                    // measure what the master SEES
        s.GateOn(127);
        for (int f = 0; f < total; ) {
            int todo = 128; if (f + todo > total) todo = total - f;
            memset(st, 0, todo * 2 * sizeof(float));
            s.processBlock(st, (size_t)todo);
            for (int i = 0; i < todo; ++i) o[f + i] = st[i * 2];
            f += todo;
        }
        g_mp_bypass = false;
        float pb = 0.f, pa = 0.f;
        for (int j = 0; j < total; ++j) {
            const float v = fabsf(o[j]);
            if (j < lo)            { if (v > pa) pa = v; }
            else if (j < hi && v > pb) pb = v;
        }
        body[idx] = pb; attack[idx] = pa;
        // pb was measured THROUGH the table already in the tree, so compose.
        // kPresetOutTrim is a non-static member (the .data placement rule the
        // other preset tables follow), so it is read off the instance.
        const float was = s.kPresetOutTrim[idx];
        trim[idx] = (pb > 1e-6f) ? fminf(1.0f, was * target / pb) : was;
    }

    printf("// body target %.2f, measured at velocity 127 on each preset's own Note\n", target);
    printf("float kPresetOutTrim[k_NumPrograms] = {\n");
    for (int i = 0; i < BrachettiSynth::k_NumPrograms; ++i)
        printf("    %.5ff,%s", trim[i],
               (i % 4 == 3) ? "\n" : "");
    printf("};\n\n");
    printf("%-3s %-9s %10s %10s %8s %9s\n",
           "idx", "name", "body", "strike", "crest dB", "trim dB");
    for (int i = 0; i < BrachettiSynth::k_NumPrograms; ++i)
        printf("%-3d %-9s %10.3f %10.3f %8.1f %9.2f\n", i,
               BrachettiSynth::getPresetName(i), body[i], attack[i],
               20.f * log10f((attack[i] + 1e-9f) / (body[i] + 1e-9f)),
               20.f * log10f(trim[i]));
    return 0;
}
