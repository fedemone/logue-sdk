/**
 * click_probe.cpp — locate CLICKS in a played passage, not just peak steps.
 *
 * Why this exists.  Pass 41 chased Timpani's "changing note leads to sporadic
 * clicks for the next 8-10 seconds" with a max sample-to-sample step metric,
 * measured no improvement, and rejected the theory.  That metric cannot answer
 * the question: a 90 Hz kettle at full amplitude has a legitimate per-sample
 * step of ~0.02, one number for a whole render hides everything sporadic, and
 * the loudest step in any percussion render is always the strike itself.  So a
 * click that is 20 dB under the attack is invisible, and "no change in the
 * max" says nothing about whether clicks happened.
 *
 * What a click actually is: BROADBAND energy where the signal is tonal.  This
 * probe high-passes at ~8 kHz, tracks the HF envelope against the broadband
 * envelope, and flags samples where HF spikes far above its own running
 * median.  Strikes legitimately do that too, so the window around every known
 * NoteOn is excluded by timestamp — what is left is unexplained.
 *
 * It also renders a CONTROL passage (same strikes, note never changes) and
 * reports both, because a bare event count means nothing without it — the
 * lesson live_edit_probe learned by producing 36 false positives on its first
 * run.
 *
 * Usage:
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       click_probe.cpp -o /tmp/click_probe && /tmp/click_probe
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

static const int kSR = 48000;
static const int kBlock = 128;

struct Event { int type; float t; int arg; };   // type 0 = strike, 1 = set Note

// Render a timeline.  Returns mono samples.
static std::vector<float> render(int preset, const std::vector<Event>& evs,
                                 float dur_s, std::vector<float>* strike_times) {
    static BrachettiSynth s;
    unit_runtime_desc_t d = {};
    d.samplerate = kSR; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    s.Init(&d);
    s.LoadPreset((uint8_t)preset);

    const int total = (int)(dur_s * kSR);
    std::vector<float> mono((size_t)total, 0.0f);
    static float stereo[kBlock * 2];

    size_t ei = 0;
    int frame = 0;
    while (frame < total) {
        const float t = (float)frame / kSR;
        while (ei < evs.size() && evs[ei].t <= t) {
            if (evs[ei].type == 0) {
                s.GateOn((uint8_t)evs[ei].arg);
                s.GateOff();
                if (strike_times) strike_times->push_back(t);
            } else {
                s.setParameter(BrachettiSynth::k_paramNote, evs[ei].arg);
            }
            ++ei;
        }
        int todo = (total - frame < kBlock) ? (total - frame) : kBlock;
        memset(stereo, 0, sizeof(stereo));
        s.processBlock(stereo, (size_t)todo);
        for (int i = 0; i < todo; ++i) mono[frame + i] = stereo[i * 2];
        frame += todo;
    }
    return mono;
}

// One-pole high pass at ~8 kHz, then a fast-attack/slow-release envelope.
static std::vector<float> hf_env(const std::vector<float>& x) {
    const float fc = 8000.0f;
    const float a = expf(-2.0f * (float)M_PI * fc / kSR);
    float lp = 0.0f, env = 0.0f;
    const float rel = expf(-1.0f / (0.002f * kSR));   // 2 ms release
    std::vector<float> e(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        lp = a * lp + (1.0f - a) * x[i];
        float hp = fabsf(x[i] - lp);
        env = (hp > env) ? hp : (env * rel + hp * (1.0f - rel));
        e[i] = env;
    }
    return e;
}

struct Click { float t; float ratio; float level; };

// A click = the HF envelope jumping far above its own recent median.
static std::vector<Click> find_clicks(const std::vector<float>& x,
                                      const std::vector<float>& strikes,
                                      float thresh) {
    std::vector<float> e = hf_env(x);
    const int win = kSR / 20;             // 50 ms of history for the median
    std::vector<Click> out;
    std::vector<float> buf;
    float last_t = -1.0f;
    for (size_t i = (size_t)win; i < e.size(); i += 8) {
        const float t = (float)i / kSR;
        // Skip the 60 ms after any strike: the knock is legitimately broadband.
        bool near_strike = false;
        for (float st : strikes)
            if (t >= st - 0.002f && t <= st + 0.060f) { near_strike = true; break; }
        if (near_strike) continue;
        buf.clear();
        for (int j = -win; j < 0; j += 16) buf.push_back(e[i + j]);
        std::sort(buf.begin(), buf.end());
        const float med = buf[buf.size() / 2];
        if (med < 1e-7f) continue;
        const float ratio = e[i] / med;
        if (ratio > thresh && t - last_t > 0.010f) {
            out.push_back({t, ratio, e[i]});
            last_t = t;
        }
    }
    return out;
}

int main(int argc, char** argv) {
    const int preset = (argc > 1) ? atoi(argv[1]) : 5;      // Timpani
    const int root   = (argc > 2) ? atoi(argv[2]) : 52;
    const float thresh = (argc > 3) ? (float)atof(argv[3]) : 6.0f;
    const float dur = 16.0f;

    // Sequencer-like passage: a strike every 500 ms for the whole render.
    // The NOTE-CHANGE timeline moves the Note knob between strikes, using FOUR
    // distinct notes so a kettle steal actually happens (two kettles cover two
    // notes; alternating two notes never exercises the steal path).
    std::vector<Event> ev_note, ev_ctrl;
    const int notes[] = { root, root + 3, root + 7, root - 2, root + 5 };
    int ni = 0;
    for (float t = 0.05f; t < dur; t += 0.5f) {
        if (t > 3.0f && fmodf(t - 0.05f, 2.0f) < 0.001f) {
            ni = (ni + 1) % 5;
            ev_note.push_back({1, t - 0.02f, notes[ni]});
        }
        ev_note.push_back({0, t, 100});
        ev_ctrl.push_back({0, t, 100});
    }

    std::vector<float> st_n, st_c;
    std::vector<float> a = render(preset, ev_note, dur, &st_n);
    std::vector<float> b = render(preset, ev_ctrl, dur, &st_c);

    std::vector<Click> ca = find_clicks(a, st_n, thresh);
    std::vector<Click> cb = find_clicks(b, st_c, thresh);

    printf("click probe — preset %d, root note %d, %.0f s, HF/median threshold %.1fx\n",
           preset, root, dur, thresh);
    printf("  note-change timeline : %3zu click events\n", ca.size());
    printf("  CONTROL (no changes) : %3zu click events\n", cb.size());
    printf("\n  note-change events (first 40):\n");
    for (size_t i = 0; i < ca.size() && i < 40; ++i)
        printf("    t=%7.3f s   HF/med %6.1fx   level %.5f\n",
               ca[i].t, ca[i].ratio, ca[i].level);
    if (!cb.empty()) {
        printf("\n  control events (first 10):\n");
        for (size_t i = 0; i < cb.size() && i < 10; ++i)
            printf("    t=%7.3f s   HF/med %6.1fx   level %.5f\n",
                   cb[i].t, cb[i].ratio, cb[i].level);
    }

    // Where in the passage do they land?  The report is "for the next 8-10
    // seconds after a note change", so bucket by second.
    printf("\n  events per second (note-change / control):\n    ");
    for (int s = 0; s < (int)dur; ++s) {
        int na = 0, nb = 0;
        for (auto& c : ca) if ((int)c.t == s) na++;
        for (auto& c : cb) if ((int)c.t == s) nb++;
        printf("%d:%d/%d  ", s, na, nb);
    }
    printf("\n");
    return 0;
}
