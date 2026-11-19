// brachetti_check.cpp — does Brachetti (all families in one unit) play each
// family exactly as the family's own unit does?
//
//     brachetti_check <brachetti.so> <brachetti_kick.so> ... <brachetti_cordiera.so>
//
// The family units are given in Brachetti's preset order.  Every unit is
// loaded as the drumlogue loads it (dlopen, RTLD_LOCAL, all in one process).
// For every family f and every instrument p in it:
//   * Brachetti: unit_load_preset(f), then Program = p;
//     the family unit: Program = p.
//   * Every knob Brachetti shows must read back as the family's own value
//     (through the slot's range translation), with the family's display text.
//   * Both are struck (gate and MIDI notes, several velocities) and rendered,
//     and must give the same samples, bit for bit; then every dedicated knob
//     is moved to its ends, centre and two random points — set on Brachetti
//     through its slot, on the family unit directly with the translated value
//     — and both are struck and rendered again.
// Plus: Brachetti's header defaults equal what its first preset loads, the
// Program range holds every family's instruments, the common knobs survive a
// family switch, and every slot is wide enough for its knobs.
//
// Host build without -ffast-math (brachetti_check.sh): any difference is real.
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "unit.h"

typedef int8_t (*init_fn)(const unit_runtime_desc_t*);
typedef void (*render_fn)(const float*, float*, uint32_t);
typedef void (*set_fn)(uint8_t, int32_t);
typedef int32_t (*get_fn)(uint8_t);
typedef const char* (*str_fn)(uint8_t, int32_t);
typedef void (*u8_fn)(uint8_t);
typedef void (*u8u8_fn)(uint8_t, uint8_t);
typedef void (*void_fn)();
typedef uint8_t (*idx_fn)();
typedef const char* (*name_fn)(uint8_t);

static uint8_t no_banks() { return 0; }
static uint8_t no_samples(uint8_t) { return 0; }
static const sample_wrapper_t* no_sample(uint8_t, uint8_t) { return nullptr; }

struct Unit {
    void* h = nullptr;
    const unit_header_t* hdr = nullptr;
    init_fn init; render_fn render; set_fn set; get_fn get; str_fn str;
    u8_fn gate_on, note_off, load_preset; u8u8_fn note_on; void_fn gate_off, all_off, reset;
    idx_fn preset_index; name_fn preset_name;
    bool open(const char* path) {
        h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return false; }
        hdr = (const unit_header_t*)dlsym(h, "unit_header");
        init = (init_fn)dlsym(h, "unit_init");
        render = (render_fn)dlsym(h, "unit_render");
        set = (set_fn)dlsym(h, "unit_set_param_value");
        get = (get_fn)dlsym(h, "unit_get_param_value");
        str = (str_fn)dlsym(h, "unit_get_param_str_value");
        gate_on = (u8_fn)dlsym(h, "unit_gate_on");
        gate_off = (void_fn)dlsym(h, "unit_gate_off");
        note_on = (u8u8_fn)dlsym(h, "unit_note_on");
        note_off = (u8_fn)dlsym(h, "unit_note_off");
        all_off = (void_fn)dlsym(h, "unit_all_note_off");
        reset = (void_fn)dlsym(h, "unit_reset");
        load_preset = (u8_fn)dlsym(h, "unit_load_preset");
        preset_index = (idx_fn)dlsym(h, "unit_get_preset_index");
        preset_name = (name_fn)dlsym(h, "unit_get_preset_name");
        unit_runtime_desc_t d = {};
        d.target = hdr->target;
        d.api = hdr->api;
        d.samplerate = 48000;
        d.frames_per_buffer = 64;
        d.output_channels = 2;
        d.get_num_sample_banks = no_banks;
        d.get_num_samples_for_bank = no_samples;
        d.get_sample = no_sample;
        if (init(&d) != k_unit_err_none) { fprintf(stderr, "unit_init failed: %s\n", path); return false; }
        return true;
    }
    std::vector<float> run(int frames) {
        std::vector<float> out((size_t)frames * 2);
        for (int done = 0; done < frames; done += 64) {
            const int n = (frames - done < 64) ? frames - done : 64;
            render(nullptr, &out[(size_t)done * 2], (uint32_t)n);
        }
        return out;
    }
};

// Brachetti's slot -> family knob, as brachetti_synth.h places them.  Kept
// here independently: a change there that this file does not know about
// fails the check (the knobs would read back wrong).
static const uint8_t N = 0xFF;
static const uint8_t kStd[24]    = {0,1,2,3,4,5,6,7, 8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};
static const uint8_t kKernel[24] = {0,1,2,3,4,5,6,7, 8,9,10,11,12,19,13,14,15,16,17,18,20,N,N,21};
static const uint8_t kCymbal[24] = {0,1,2,3,4,5,6,7, 8,9,10,11,12,19,13,14,15,16,17,18,N,N,N,N};
static const uint8_t kNoise[24]  = {0,1,2,3,4,5,6,7, 9,8,10,11,16,18,N,19,N,N,N,17,12,13,14,15};
static const uint8_t* const kMaps[12] = {kStd, kStd, kKernel, kStd, kStd, kStd, kCymbal, kNoise,
                                         kStd, kStd, kStd, kStd};

static int32_t mdr(int32_t a, int32_t num, int32_t den) { return (int32_t)(((int64_t)a * num + den / 2) / den); }
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int32_t to_slot(const unit_param_t& s, const unit_param_t& f, int32_t v) {
    if (s.min == f.min && s.max == f.max) return v;
    return s.min + mdr(clampi(v, f.min, f.max) - f.min, s.max - s.min, f.max - f.min);
}
static int32_t to_family(const unit_param_t& s, const unit_param_t& f, int32_t v) {
    if (s.min == f.min && s.max == f.max) return v;
    return f.min + mdr(clampi(v, s.min, s.max) - s.min, f.max - f.min, s.max - s.min);
}
static bool blank(const unit_param_t& p) { return p.name[0] == '\0'; }

// Knob positions tried per dedicated knob: ends, centre, two random points
// (BRACHETTI_KNOB_POINTS: the first n of them; the ARM run uses fewer).
static int g_points = 5;
static int g_fail = 0;
static long g_renders = 0, g_knobs = 0;
static void fail(const std::string& what) {
    if (g_fail < 40) fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++g_fail;
}

// Bit for bit; or, with BRACHETTI_TOL set (the ARM build, -ffast-math: the
// compiler may fuse float operations differently in a different translation
// unit), within that absolute tolerance.  The largest difference is reported.
static double g_tol = -1.0, g_maxdiff = 0.0;
static bool differ(const std::vector<float>& a, const std::vector<float>& b) {
    if (g_tol < 0.0) return memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0;
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = std::fabs((double)a[i] - (double)b[i]);
        if (!(d <= m)) m = d;   // NaN counts as a difference
    }
    if (!(m <= g_maxdiff)) g_maxdiff = m;
    return !(m <= g_tol);
}

// Strike both and compare the output.
static void strike_compare(Unit& b, Unit& f, const std::string& ctx, unsigned& seed) {
    struct Hit { bool gate; uint8_t note, vel; int frames; };
    const uint8_t vels[] = {127, 90, 40};
    for (uint8_t vel : vels) {
        b.gate_on(vel); f.gate_on(vel);
        std::vector<float> ob = b.run(2400), of = f.run(2400);
        b.gate_off(); f.gate_off();
        std::vector<float> ob2 = b.run(4800), of2 = f.run(4800);
        ob.insert(ob.end(), ob2.begin(), ob2.end());
        of.insert(of.end(), of2.begin(), of2.end());
        ++g_renders;
        if (differ(ob, of))
            fail(ctx + " gate vel " + std::to_string(vel) + ": output differs");
    }
    // Two MIDI notes, a chord's worth, then panic.
    seed = seed * 1103515245u + 12345u;
    const uint8_t n1 = (uint8_t)(36 + (seed >> 16) % 40), n2 = (uint8_t)(n1 + 7);
    b.note_on(n1, 100); f.note_on(n1, 100);
    b.note_on(n2, 70);  f.note_on(n2, 70);
    std::vector<float> ob = b.run(3000), of = f.run(3000);
    b.note_off(n1); f.note_off(n1);
    b.all_off(); f.all_off();
    std::vector<float> ob2 = b.run(3000), of2 = f.run(3000);
    ob.insert(ob.end(), ob2.begin(), ob2.end());
    of.insert(of.end(), of2.begin(), of2.end());
    ++g_renders;
    if (differ(ob, of))
        fail(ctx + " notes: output differs");
    b.reset(); f.reset();
}

// Every Brachetti knob reads back as the family's value, with its text.
static void knobs_compare(Unit& b, Unit& f, const uint8_t* map, const std::string& ctx) {
    for (int s = 0; s < 24; ++s) {
        const uint8_t fs = map[s];
        if (fs == N || blank(f.hdr->params[fs])) {
            const char* t = b.str((uint8_t)s, b.get((uint8_t)s));
            if (s >= 8 && (!t || strcmp(t, "---") != 0)) fail(ctx + " unused slot " + std::to_string(s) + " text");
            continue;
        }
        const unit_param_t& sp = b.hdr->params[s];
        const unit_param_t& fp = f.hdr->params[fs];
        const int32_t bv = b.get((uint8_t)s), fv = f.get(fs);
        ++g_knobs;
        if (bv < sp.min || bv > sp.max) fail(ctx + " slot " + sp.name + " value outside its range");
        if ((s == 0 ? bv : to_family(sp, fp, bv)) != fv)   // Program is an index, not scaled
            fail(ctx + " slot " + sp.name + " reads " + std::to_string(bv) + ", family " + fp.name + " = " + std::to_string(fv));
        if (fp.type == k_unit_param_type_strings) {
            const char* tb = b.str((uint8_t)s, bv);
            std::string ref = f.str(fs, fv) ? f.str(fs, fv) : "(null)";
            if (!tb || ref != tb) fail(ctx + " slot " + sp.name + " text '" + (tb ? tb : "(null)") + "' vs '" + ref + "'");
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 14) { fprintf(stderr, "usage: brachetti_check <brachetti.so> <12 family .so in preset order>\n"); return 2; }
    if (const char* t = getenv("BRACHETTI_TOL")) g_tol = atof(t);
    if (const char* t = getenv("BRACHETTI_KNOB_POINTS")) g_points = clampi(atoi(t), 0, 5);
    Unit b;
    if (!b.open(argv[1])) return 2;
    std::vector<Unit> fam(12);
    for (int i = 0; i < 12; ++i) if (!fam[i].open(argv[2 + i])) return 2;

    if (b.hdr->num_presets != 12) fail("num_presets != 12");
    // The slots are wide enough, and the Program range holds every family.
    for (int i = 0; i < 12; ++i) {
        const int n = fam[i].hdr->params[0].max + 1;
        if (n - 1 > b.hdr->params[0].max) fail(std::string(fam[i].hdr->name) + ": more instruments than Program's range");
        for (int s = 8; s < 24; ++s) {
            const uint8_t fs = kMaps[i][s];
            if (fs == N) continue;
            const unit_param_t& sp = b.hdr->params[s];
            const unit_param_t& fp = fam[i].hdr->params[fs];
            if (sp.max - sp.min < fp.max - fp.min)
                fail(std::string(fam[i].hdr->name) + ": " + fp.name + " wider than slot " + sp.name);
            if (sp.type != k_unit_param_type_strings) fail(std::string("slot ") + sp.name + " is not a strings knob");
            for (int v = fp.min; v <= fp.max; ++v)   // every value of the knob survives the slot
                if (to_family(sp, fp, to_slot(sp, fp, v)) != v)
                    fail(std::string(fam[i].hdr->name) + ": " + fp.name + " value " + std::to_string(v) + " lost in its slot");
        }
        // Every family knob is placed exactly once.
        for (uint32_t k = 0; k < fam[i].hdr->num_params; ++k) {
            if (blank(fam[i].hdr->params[k])) continue;
            int count = 0;
            for (int s = 0; s < 24; ++s) count += (kMaps[i][s] == k);
            if (count != 1) fail(std::string(fam[i].hdr->name) + ": knob " + fam[i].hdr->params[k].name + " placed " + std::to_string(count) + " times");
        }
    }
    // Header defaults = what the first preset loads (the panel at power-on).
    for (int s = 0; s < 24; ++s)
        if (b.hdr->params[s].init != b.get((uint8_t)s))
            fail(std::string("header default of ") + b.hdr->params[s].name + " is " + std::to_string(b.hdr->params[s].init) +
                 ", the first preset loads " + std::to_string(b.get((uint8_t)s)));

    unsigned seed = 1;
    for (int i = 0; i < 12; ++i) {
        Unit& f = fam[i];
        b.load_preset((uint8_t)i);
        if (b.preset_index() != i) fail("preset index after load_preset " + std::to_string(i));
        b.run(1024);   // the previous family's fade-out
        const int programs = f.hdr->params[0].max + 1;
        for (int p = 0; p < programs; ++p) {
            b.set(0, p); f.set(0, p);
            const std::string ctx = std::string(b.preset_name((uint8_t)i)) + "/" + (f.str(0, p) ? f.str(0, p) : "?");
            knobs_compare(b, f, kMaps[i], ctx);
            strike_compare(b, f, ctx, seed);
            // Every dedicated knob: ends, centre, two random points.
            for (int s = 8; s < 24; ++s) {
                const uint8_t fs = kMaps[i][s];
                if (fs == N) continue;
                const unit_param_t& sp = b.hdr->params[s];
                const unit_param_t& fp = f.hdr->params[fs];
                const int32_t keep_b = b.get((uint8_t)s), keep_f = f.get(fs);
                int32_t pts[5] = {sp.min, sp.max, (sp.min + sp.max) / 2, 0, 0};
                for (int r = 3; r < 5; ++r) { seed = seed * 1103515245u + 12345u; pts[r] = sp.min + (int32_t)((seed >> 8) % (uint32_t)(sp.max - sp.min + 1)); }
                for (int k = 0; k < g_points; ++k) {
                    const int32_t v = pts[k];
                    b.set((uint8_t)s, v); f.set(fs, to_family(sp, fp, v));
                    knobs_compare(b, f, kMaps[i], ctx + " " + sp.name + "=" + std::to_string(v));
                    strike_compare(b, f, ctx + " " + sp.name + "=" + std::to_string(v), seed);
                }
                b.set((uint8_t)s, keep_b); f.set(fs, keep_f);
            }
        }
        // Past the family's last instrument: its last.
        b.set(0, b.hdr->params[0].max); f.set(0, programs - 1);
        if (b.get(0) != programs - 1) fail(std::string(b.preset_name((uint8_t)i)) + ": Program beyond the last instrument");
        b.set(0, 0); f.set(0, 0);
    }

    // The common knobs follow the panel across a family switch.
    b.load_preset(1);
    b.set(1, 5); b.set(2, 2); b.set(3, -30); b.set(4, 12); b.set(5, 900); b.set(6, 150);
    b.load_preset(4);
    const int32_t want[7] = {0, 5, 2, -30, 12, 900, 150};
    for (int s = 1; s <= 6; ++s)
        if (b.get((uint8_t)s) != want[s]) fail(std::string("common knob ") + b.hdr->params[s].name + " not carried across a family switch");

    printf("brachetti: %s — 12 families, %ld renders, %ld knob read-backs compared with the family units\n",
           g_fail ? "DIFFERENT" : (g_tol < 0.0 ? "IDENTICAL" : "WITHIN TOLERANCE"), g_renders, g_knobs);
    if (g_tol >= 0.0) printf("largest sample difference: %.3g (tolerance %.3g)\n", g_maxdiff, g_tol);
    return g_fail ? 1 : 0;
}
