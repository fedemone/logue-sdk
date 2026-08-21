/**
 * live_edit_probe.cpp — change parameters WHILE a voice is ringing (host-only).
 *
 * Every existing tool sets parameters BEFORE NoteOn and never touches them
 * again: param_audit, plateau_probe and stability_sweep all configure, then
 * strike, then render.  So none of them can see a fault that needs an edit to
 * land in the middle of a sounding voice — and that is exactly the HW report
 * this was written for ("Timpani: changing note while playing leads to silence
 * (audio interface crash)").
 *
 * For every preset it strikes a note, renders partway, writes a parameter, and
 * keeps rendering — checking the whole time for NaN/Inf, for a peak blowup, and
 * for the voice going SILENT after the edit when it was still sounding before.
 * The silence check is the interesting one: a voice that kills itself mid-ring
 * is invisible to any NaN/peak assertion.
 *
 * Usage:
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       live_edit_probe.cpp -o /tmp/live_edit_probe && /tmp/live_edit_probe
 */

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

static const int SR = 48000;
static const int BLOCK = 128;
static float st[BLOCK * 2];

// Render `frames`, return RMS; sets *bad on NaN/Inf or |x| > 1.5.
static float render(BrachettiSynth& s, int frames, int* bad) {
    double acc = 0; int n = 0;
    int done = 0;
    while (done < frames) {
        int t = (frames - done < BLOCK) ? (frames - done) : BLOCK;
        s.processBlock(st, (size_t)t);
        for (int i = 0; i < t * 2; ++i) {
            float v = st[i];
            if (!std::isfinite(v)) { *bad |= 1; v = 0.0f; }
            else if (fabsf(v) > 1.5f) *bad |= 2;
            acc += (double)v * v; n++;
        }
        done += t;
    }
    return (n > 0) ? (float)sqrt(acc / n) : 0.0f;
}

struct Edit { int idx; int value; const char* what; };

int main(int argc, char** argv) {
    unit_runtime_desc_t d = {};
    d.samplerate = SR; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    static BrachettiSynth s;

    // The edits a player actually makes mid-performance.
    const Edit edits[] = {
        {  1,  72, "Note=72"   }, {  1,  36, "Note=36"   }, {  1, 126, "Note=126"  },
        {  1,  24, "Note=24"   }, {  0,   5, "Program=5" }, {  9,   0, "Model=0"   },
        {  8,   7, "Partls=7"  }, { 10, 210, "Dkay=210"  }, { 15, -100, "Inharm=-100" },
        { 13, -98, "HitPos=-98"}, { 14,  20, "Rel=20"    }, {  2,   4, "Poly=4"    },
    };
    const int NE = (int)(sizeof(edits) / sizeof(edits[0]));

    int only = (argc > 1) ? atoi(argv[1]) : -1;
    int problems = 0;

    printf("live-edit probe — strike, render 200 ms, edit, render 800 ms more\n");
    printf("values shown as after/control — RING-DIED or RESTRIKE-DIED means the edit killed it\n\n");

    for (int p = 0; p < BrachettiSynth::k_NumPrograms; ++p) {
        if (only >= 0 && p != only) continue;
        for (int e = 0; e < NE; ++e) {
            s.Init(&d);
            s.LoadPreset((uint8_t)p);
            int note = s.getParameterValue(1);
            if (note < 24 || note > 126) note = 60;

            int bad = 0;
            s.NoteOn((uint8_t)note, 110);
            float pre = render(s, SR / 5, &bad);          // 200 ms
            s.setParameter(edits[e].idx, edits[e].value);
            float post = render(s, (SR * 4) / 5, &bad);   // 800 ms
            // ...then STRIKE AGAIN.  This is the scenario the HW report
            // actually describes — the player turns Note while the sequencer
            // keeps triggering — and it is the half that a ring-out test
            // cannot see.
            int note2 = (edits[e].idx == 1) ? edits[e].value : note;
            if (note2 < 24 || note2 > 126) note2 = note;
            s.NoteOn((uint8_t)note2, 110);
            float again = render(s, SR / 2, &bad);        // 500 ms

            // CONTROL: identical timeline with NO edit.  Without this, every
            // short percussion preset (Woodblock, Claves, RimShot) reports
            // "SILENT" simply because it decayed on its own — 36 false
            // positives on the first run of this probe.
            BrachettiSynth& c = s;
            c.Init(&d); c.LoadPreset((uint8_t)p);
            int cbad = 0;
            c.NoteOn((uint8_t)note, 110);
            render(c, SR / 5, &cbad);
            float cpost = render(c, (SR * 4) / 5, &cbad);
            c.NoteOn((uint8_t)note, 110);
            float cagain = render(c, SR / 2, &cbad);

            bool ring_died   = (cpost  > 1e-5f) && (post  < cpost  * 0.01f);
            bool strike_died = (cagain > 1e-5f) && (again < cagain * 0.01f);
            if (bad || ring_died || strike_died) {
                printf("  %-9s preset %2d  %-12s  ring %.5f/%.5f  restrike %.5f/%.5f  %s%s%s%s\n",
                       BrachettiSynth::getPresetName((uint8_t)p), p, edits[e].what,
                       post, cpost, again, cagain,
                       (bad & 1) ? "NaN/Inf " : "", (bad & 2) ? "BLOWUP " : "",
                       ring_died ? "RING-DIED " : "", strike_died ? "RESTRIKE-DIED" : "");
                problems++;
            }
        }
    }
    printf("\n%d problems\n", problems);
    return problems ? 1 : 0;
}
