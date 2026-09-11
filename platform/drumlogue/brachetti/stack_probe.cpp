/**
 * stack_probe.cpp — "does this preset clip when you stack notes?"
 *
 * The HW report that motivated this ("most of the instruments seem to be
 * clipping a bit, possibly on stacking notes") cannot be reproduced by
 * render_presets.cpp: that harness plays ONE note at velocity 100 and the
 * master limiter comfortably holds a single voice.  The defect only shows
 * when several voices are alive at once, because every voice is summed onto
 * the bus at the SAME master_gain and nothing between the sum and the limiter
 * knows how many of them there are.
 *
 * So this probe plays a chord: up to `voices` notes, spaced `spacing_ms`
 * apart, and reports what the master stage actually saw.
 *
 *   bus   peak |voice bus| arriving at Stage 4b (before Gain/drive)
 *   pre   peak |x| after the master filter and drive — the limiter's input
 *   GRmax the largest gain reduction the soft knee applied, in dB
 *   wall  samples the ±0.99 safety brickwall actually clamped (should be 0)
 *   pin%  samples the instant-attack branch placed ON the static gain curve,
 *         i.e. waveshaped rather than limited
 *   peak  output peak
 *   flat  samples inside a run of >= 3 identical |samples| >= 0.97 (flat-top)
 *   crest peak / rms over the whole render
 *
 * Build (from the brachetti directory):
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common \
 *       -DRUNTIME_COMMON_H_ -DBRACHETTI_MASTER_PROBE stack_probe.cpp \
 *       -o /tmp/stack_probe
 * Run:
 *   /tmp/stack_probe            # the pitched presets, 1..4 voices
 *   /tmp/stack_probe /tmp/wavs  # ...and dump the renders
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sys/stat.h>
#include <vector>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;

// Master-stage diagnostics the header declares extern under the probe macro.
// All six must be defined by every probe that compiles with the macro.
float g_mp_bus_peak  = 0.0f;
float g_mp_pre_peak  = 0.0f;
float g_mp_gr_min    = 1.0f;
long  g_mp_wall_hits = 0;
long  g_mp_samples   = 0;
long  g_mp_pinned    = 0;
float g_mp_trim      = 1.0f;   // 1.0 = the shipping gain staging
bool  g_mp_bypass    = false;  // true = skip the knee and the brickwall

#include "synth_engine.h"

static bool write_wav(const char* path, const float* s, int n, int sr) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    std::vector<int16_t> pcm(n);
    for (int i = 0; i < n; ++i) {
        float v = s[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + n * 2); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(1);
    w32((uint32_t)sr); w32((uint32_t)(sr * 2)); w16(2); w16(16);
    fwrite("data", 1, 4, f); w32((uint32_t)(n * 2));
    fwrite(pcm.data(), 2, n, f);
    fclose(f);
    return true;
}

struct Row { int idx; const char* name; uint8_t note; float dur; };

// Chord offsets: a player stacking notes on a pitched voice plays intervals,
// and intervals sum worse than unisons do not — the partials land apart, so
// the peaks add rather than beat.  Root, 5th, octave, 10th.
static const int kChord[4] = {0, 7, 12, 16};

int main(int argc, char** argv) {
    const char* dump = (argc > 1) ? argv[1] : nullptr;
    if (dump) mkdir(dump, 0755);

    const Row rows[] = {
        { 1, "Marmba",  72, 4.0f},
        {19, "Trngle",  69, 6.0f},
        {16, "StlPan",  60, 6.0f},
        {28, "Handpn",  62, 5.0f},
        // context: other pitched voices the same change touches
        { 4, "TblrBel", 72, 6.0f},
        {10, "Vibrph",  72, 5.0f},
        {15, "Kalimba", 65, 4.0f},
        {29, "BelTre",  84, 4.0f},
        {30, "SltDrm",  60, 4.0f},
        {24, "GlsBwl",  76, 6.0f},
        { 9, "Koto",    60, 5.0f},
        {32, "RidBel",  60, 4.0f},
        // and the two the user is also asking about
        {20, "Kick",    36, 3.0f},
        {23, "DeepBs",  41, 4.0f},
    };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));

    const int sr = 48000;
    const float spacing_ms = 120.0f;

    printf("%-9s %5s %8s %8s %8s %6s %6s %7s %6s %6s\n",
           "preset", "vcs", "bus", "pre", "GRmax", "pin%", "wall", "peak", "flat", "crest");
    printf("----------------------------------------------------------------------------------\n");

    for (int r = 0; r < nrows; ++r) {
        for (int nv = 1; nv <= 4; ++nv) {
            BrachettiSynth synth;
            unit_runtime_desc_t desc = {};
            desc.samplerate = 48000;
            desc.output_channels = 2;
            desc.get_num_sample_banks     = mock_get_num_sample_banks;
            desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
            desc.get_sample               = mock_get_sample;
            synth.Init(&desc);
            synth.LoadPreset(rows[r].idx);

            g_mp_bus_peak = 0.0f; g_mp_pre_peak = 0.0f;
            g_mp_gr_min = 1.0f;   g_mp_wall_hits = 0;
            g_mp_samples = 0;     g_mp_pinned = 0;

            const int total = (int)(rows[r].dur * sr);
            std::vector<float> mono(total, 0.0f);
            float st[256 * 2];
            const int block = 128;

            int next_note = 0;
            const int spacing = (int)(spacing_ms * 0.001f * sr);

            for (int frame = 0; frame < total; ) {
                int todo = block;
                if (frame + todo > total) todo = total - frame;
                while (next_note < nv && next_note * spacing <= frame) {
                    synth.NoteOn((uint8_t)(rows[r].note + kChord[next_note]), 127);
                    ++next_note;
                }
                memset(st, 0, todo * 2 * sizeof(float));
                synth.processBlock(st, (size_t)todo);
                for (int i = 0; i < todo; ++i) mono[frame + i] = st[i * 2];
                frame += todo;
            }

            double sum2 = 0.0; float peak = 0.0f;
            for (int i = 0; i < total; ++i) {
                const float a = fabsf(mono[i]);
                if (a > peak) peak = a;
                sum2 += (double)mono[i] * mono[i];
            }
            const double rms = sqrt(sum2 / total);

            // Flat-top detector: runs of >= 3 samples whose magnitude is both
            // >= 0.97 and identical to the neighbour.  A limiter that rides
            // gain leaves none; a clipper leaves plateaus.
            long flat = 0;
            int run = 1;
            for (int i = 1; i < total; ++i) {
                if (fabsf(mono[i]) >= 0.97f && mono[i] == mono[i - 1]) ++run;
                else { if (run >= 3) flat += run; run = 1; }
            }
            if (run >= 3) flat += run;

            const double gr_db = 20.0 * log10(g_mp_gr_min > 0 ? g_mp_gr_min : 1e-9);
            const double pin = 100.0 * g_mp_pinned /
                               (double)(g_mp_samples ? g_mp_samples : 1);
            printf("%-9s %5d %8.3f %8.3f %8.2f %6.2f %6ld %7.4f %6ld %6.2f\n",
                   rows[r].name, nv, g_mp_bus_peak, g_mp_pre_peak, gr_db, pin,
                   g_mp_wall_hits, peak, flat, rms > 0 ? peak / rms : 0.0);

            if (dump) {
                char p[512];
                snprintf(p, sizeof(p), "%s/%02d_%s_x%d.wav", dump, rows[r].idx,
                         rows[r].name, nv);
                write_wav(p, mono.data(), total, sr);
            }
        }
    }
    return 0;
}
