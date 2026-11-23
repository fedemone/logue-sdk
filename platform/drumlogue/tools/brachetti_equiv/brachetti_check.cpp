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
#include <fstream>
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
// Brachetti's coarse slots (kCoarse there): Cutoff and NzFltFrq step 0.1 kHz,
// family value = slot x 10 + offset.  Kept here independently too.
static const int kCoarseFactor[24] = {1,1,1,1,1,10,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,10};
static const int kCoarseOffset[24] = {0,0,0,0,0,-1,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0};
static const unit_header_t* g_bhdr = nullptr;   // Brachetti's header
static bool coarse(int slot) { return kCoarseFactor[slot] > 1; }
static int32_t to_slot(int slot, const unit_param_t& f, int32_t v) {
    const unit_param_t& s = g_bhdr->params[slot];
    if (coarse(slot)) {
        const int k = kCoarseFactor[slot];
        return clampi((clampi(v, f.min, f.max) - kCoarseOffset[slot] + k / 2) / k, s.min, s.max);
    }
    if (s.min == f.min && s.max == f.max) return v;
    return s.min + mdr(clampi(v, f.min, f.max) - f.min, s.max - s.min, f.max - f.min);
}
static int32_t to_family(int slot, const unit_param_t& f, int32_t v) {
    const unit_param_t& s = g_bhdr->params[slot];
    if (coarse(slot)) return clampi(clampi(v, s.min, s.max) * kCoarseFactor[slot] + kCoarseOffset[slot], f.min, f.max);
    if (s.min == f.min && s.max == f.max) return clampi(v, s.min, s.max);
    return f.min + mdr(clampi(v, s.min, s.max) - s.min, f.max - f.min, s.max - s.min);
}
static bool blank(const unit_param_t& p) { return p.name[0] == '\0'; }

// Knob positions tried per dedicated knob: ends, centre, two random points
// (BRACHETTI_KNOB_POINTS: the first n of them; the ARM run uses fewer).
static int g_points = 5;
static int g_fail = 0;
static long g_renders = 0, g_knobs = 0;
static void fail(const std::string& what) {
    if (g_fail < 400) fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++g_fail;
}

// Bit for bit; or, with BRACHETTI_TOL set (the ARM build: under -ffast-math
// the compiler may reorder float operations differently in a different
// translation unit), each render's difference energy must be at least
// BRACHETTI_TOL dB below its signal (e.g. -90).  The worst render is reported.
static double g_tol = 1.0, g_worst_db = -400.0, g_maxdiff = 0.0;   // g_tol > 0: bit-exact
static bool differ(const std::vector<float>& a, const std::vector<float>& b) {
    if (g_tol > 0.0) return memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0;
    double e = 0.0, s = 0.0, m = 0.0;
    size_t first = a.size();
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = (double)a[i] - (double)b[i];
        e += d * d; s += (double)b[i] * b[i];
        if (!(std::fabs(d) <= m)) m = std::fabs(d);   // NaN counts as a difference
        if (d != 0.0 && first == a.size()) first = i;
    }
    if (!(m == m)) return true;
    const double db = (e == 0.0) ? -400.0 : 10.0 * std::log10(e / (s + 1e-30));
    if (db > g_worst_db) g_worst_db = db;
    if (m > g_maxdiff) g_maxdiff = m;
    const bool bad = db > g_tol;
    if (bad && getenv("BRACHETTI_VERBOSE"))
        fprintf(stderr, "  diff/signal %.1f dB, max diff %.3g, first difference at frame %zu\n",
                db, m, first / 2);
    return bad;
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
            if (s >= 8 && b.hdr->params[s].type == k_unit_param_type_strings && (!t || strcmp(t, "---") != 0))
                fail(ctx + " unused slot " + std::to_string(s) + " text");
            continue;
        }
        const unit_param_t& sp = b.hdr->params[s];
        const unit_param_t& fp = f.hdr->params[fs];
        const int32_t bv = b.get((uint8_t)s), fv = f.get(fs);
        ++g_knobs;
        if (bv < sp.min || bv > sp.max) fail(ctx + " slot " + sp.name + " value outside its range");
        // Program is an index, not scaled; a coarse slot shows the nearest step.
        if (s == 0 ? (bv != fv) : coarse(s) ? (bv != to_slot(s, fp, fv)) : (to_family(s, fp, bv) != fv))
            fail(ctx + " slot " + sp.name + " reads " + std::to_string(bv) + ", family " + fp.name + " = " + std::to_string(fv));
        if (fp.type == k_unit_param_type_strings) {
            const char* tb = b.str((uint8_t)s, bv);
            const int32_t shown = (s == 0) ? fv : to_family(s, fp, bv);   // the value the slot plays
            std::string ref = f.str(fs, shown) ? f.str(fs, shown) : "(null)";
            if (!tb || ref != tb) fail(ctx + " slot " + sp.name + " text '" + (tb ? tb : "(null)") + "' vs '" + ref + "'");
        }
    }
}

// A second, independent instance of a unit: the same library under another
// file name (dlopen returns the loaded one for the same path).
static bool open_copy(Unit& u, const char* path, const char* tag) {
    const std::string copy = std::string(path) + tag;
    { std::ifstream in(path, std::ios::binary); std::ofstream out(copy, std::ios::binary); out << in.rdbuf(); }
    return u.open(copy.c_str());
}

// Ring-over: two instances with the same history; on one, a change (a
// Program change, or Brachetti's family switch) while a held gate rings.  The
// new instrument is silent until struck, so the outputs must stay identical:
// the ringing sound goes on under its own settings, and the gate's release
// still reaches it.  Then both strike again (the changed one with its new
// instrument): the outputs must now differ — the new instrument plays.
static long g_ringover = 0;
static void ringover_compare(Unit& u1, Unit& u2, const std::string& ctx, void (*change)(Unit&)) {
    std::vector<float> a, b, x, y;
    u1.gate_on(127); u2.gate_on(127);
    a = u1.run(2400); b = u2.run(2400);
    change(u1);
    x = u1.run(1200); y = u2.run(1200); a.insert(a.end(), x.begin(), x.end()); b.insert(b.end(), y.begin(), y.end());
    u1.gate_off(); u2.gate_off();
    x = u1.run(8400); y = u2.run(8400); a.insert(a.end(), x.begin(), x.end()); b.insert(b.end(), y.begin(), y.end());
    ++g_ringover;
    if (memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0) fail(ctx + ": the ringing sound changed or was cut");
    u1.gate_on(127); u2.gate_on(127);
    x = u1.run(2400); y = u2.run(2400);
    u1.gate_off(); u2.gate_off();
    if (memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0) fail(ctx + ": the new instrument does not play");
    u1.reset(); u2.reset();
}
static int g_change_to = 0;
static void change_program(Unit& u) { u.set(0, g_change_to); }
static void change_family(Unit& u) { u.load_preset((uint8_t)g_change_to); }

int main(int argc, char** argv) {
    if (argc != 14) { fprintf(stderr, "usage: brachetti_check <brachetti.so> <12 family .so in preset order>\n"); return 2; }
    if (const char* t = getenv("BRACHETTI_TOL")) g_tol = atof(t);   // dB, negative
    if (const char* t = getenv("BRACHETTI_KNOB_POINTS")) g_points = clampi(atoi(t), 0, 5);
    Unit b;
    if (!b.open(argv[1])) return 2;
    std::vector<Unit> fam(12);
    for (int i = 0; i < 12; ++i) if (!fam[i].open(argv[2 + i])) return 2;

    g_bhdr = b.hdr;
    if (b.hdr->num_presets != 12) fail("num_presets != 12");
    // The drumlogue passes a strings knob's value as an unsigned 16-bit number
    // (on the device -1 arrived as 65535): no strings slot may go negative.
    for (int s = 0; s < 24; ++s)
        if (b.hdr->params[s].type == k_unit_param_type_strings && b.hdr->params[s].min < 0)
            fail(std::string("strings slot ") + b.hdr->params[s].name + " has a negative range");
    // The slots are wide enough, and the Program range holds every family.
    for (int i = 0; i < 12; ++i) {
        const int n = fam[i].hdr->params[0].max + 1;
        if (n - 1 > b.hdr->params[0].max) fail(std::string(fam[i].hdr->name) + ": more instruments than Program's range");
        for (int s = 8; s < 24; ++s) {
            const uint8_t fs = kMaps[i][s];
            if (fs == N) continue;
            const unit_param_t& sp = b.hdr->params[s];
            const unit_param_t& fp = fam[i].hdr->params[fs];
            if (coarse(s)) continue;   // 0.1 kHz steps by design
            if (sp.max - sp.min < fp.max - fp.min)
                fail(std::string(fam[i].hdr->name) + ": " + fp.name + " wider than slot " + sp.name);
            // A slot with a number type is formatted and stepped by the
            // drumlogue: every family knob in it must be exactly that knob.
            if (sp.type != k_unit_param_type_strings &&
                (sp.min != fp.min || sp.max != fp.max || sp.type != fp.type || sp.frac != fp.frac || sp.frac_mode != fp.frac_mode))
                fail(std::string("slot ") + sp.name + " has a number type, but " + fam[i].hdr->name + "'s " + fp.name + " is another knob");
            for (int v = fp.min; v <= fp.max; ++v)   // every value of the knob survives the slot
                if (to_family(s, fp, to_slot(s, fp, v)) != v)
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
                    b.set((uint8_t)s, v); f.set(fs, to_family(s, fp, v));
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
    b.set(1, 5); b.set(2, 2); b.set(3, -30); b.set(4, 12); b.set(5, 90); b.set(6, 150);
    b.load_preset(4);
    const int32_t want[7] = {0, 5, 2, -30, 12, 90, 150};
    for (int s = 1; s <= 6; ++s)
        if (b.get((uint8_t)s) != want[s]) fail(std::string("common knob ") + b.hdr->params[s].name + " not carried across a family switch");

    // Ring-over, on every family unit and on Brachetti (each family, and a
    // family switch).  Program 0 rings; the change goes to the last instrument.
    {
        // Fresh instances, so both of a pair have the same history.
        Unit b, b2;
        if (!open_copy(b, argv[1], ".ring1.so") || !open_copy(b2, argv[1], ".ring2.so")) return 2;
        for (int i = 0; i < 12; ++i) {
            Unit f, f2;
            if (!open_copy(f, argv[2 + i], ".ring1.so") || !open_copy(f2, argv[2 + i], ".ring2.so")) return 2;
            const int last = f.hdr->params[0].max;
            if (last == 0) continue;
            f.reset(); f2.reset(); f.set(0, 0); f2.set(0, 0); f.reset(); f2.reset();
            g_change_to = last;
            ringover_compare(f, f2, std::string(f.hdr->name) + " Program change while ringing", change_program);
            f.set(0, 0); f2.set(0, 0);
            b.load_preset((uint8_t)i); b2.load_preset((uint8_t)i);
            b.reset(); b2.reset(); b.set(0, 0); b2.set(0, 0); b.reset(); b2.reset();
            ringover_compare(b, b2, std::string("Brachetti/") + b.preset_name((uint8_t)i) + " Program change while ringing", change_program);
            b.set(0, 0); b2.set(0, 0);
            g_change_to = (i + 1) % 12;
            ringover_compare(b, b2, std::string("Brachetti ") + b.preset_name((uint8_t)i) + " -> " + b.preset_name((uint8_t)g_change_to) + " while ringing", change_family);
            b2.load_preset((uint8_t)g_change_to);   // b2 follows, so both stay in step
        }
    }

    printf("brachetti: %s — 12 families, %ld renders, %ld knob read-backs compared with the family units, %ld ring-over cases\n",
           g_fail ? "DIFFERENT" : (g_tol > 0.0 ? "IDENTICAL" : "WITHIN TOLERANCE"), g_renders, g_knobs, g_ringover);
    if (g_tol <= 0.0) printf("worst render: diff/signal %.1f dB (limit %.1f dB); largest sample difference %.3g\n",
                             g_worst_db, g_tol, g_maxdiff);
    return g_fail ? 1 : 0;
}
