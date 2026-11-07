/**
 * equiv_driver.cpp — render a scripted session through ONE Brachetti unit and
 * print a hash of every rendered span, so two units can be compared
 * bit-for-bit on the same script.
 *
 * Built once per unit directory (see run_equiv.sh):
 *
 *   g++ -std=c++17 -O2 -I<unit-dir> -I<unit-dir>/.. -I<drumlogue>/common \
 *       -DRUNTIME_COMMON_H_ equiv_driver.cpp -o driver
 *
 * Everything in a script is addressed by NAME — presets by the name the unit
 * displays, parameters by their header.c name — never by index.  That is what
 * lets the same script drive the original 40-preset `brachetti` and a family
 * unit whose preset list and parameter slots are a renumbered subset of it.
 *
 * Script, one command per line ('#' starts a comment):
 *   scenario <name>      start a new scenario: a FRESH synth, Init() as boot
 *   preset <Name>        select a preset through the Program parameter
 *   param <Name> <v>     unit_set_param_value(<slot of Name>, v)
 *   gate_on <vel>        sequencer gate on (plays root + Pitch)
 *   gate_off             sequencer gate off
 *   note_on <note> <v>   MIDI note on (absolute note)
 *   note_rel <off> <v>   MIDI note on, <off> semitones from the preset root
 *   note_off <note>      MIDI note off (absolute)
 *   note_off_rel <off>   MIDI note off relative to the root
 *   all_off              unit_all_note_off
 *   bend <0..16383>      pitch bend
 *   reset | suspend | resume
 *   render <frames>      render, in 64-frame blocks, and print one hash line
 *   render_only <frames> render without hashing or output (CPU bench)
 *
 * Output: "<scenario> <render#> <hash> <peak>" per render.  The hash is FNV-1a
 * over both output channels with -0.0f folded onto +0.0f.  Two units are
 * equivalent on a script when their outputs are identical line for line.
 *
 *   driver --list        print the unit's presets and parameters instead
 *   driver <script> <dump.f32>
 *                        also append every rendered span (interleaved float32)
 *                        to dump.f32 — for comparing builds that cannot be
 *                        bit-identical, e.g. two -ffast-math ARM builds
 *                        (compare_dumps.py)
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 0; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 0; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
// The original unit declares these for its own tests; harmless elsewhere.
float ut_exciter_out = 0.0f, ut_delay_read = 0.0f, ut_voice_out = 0.0f;

#include "synth_engine.h"
#include "header.c"

static const int kBlock = 64;

static int param_slot(const char* name) {
    for (uint32_t i = 0; i < unit_header.num_params; ++i)
        if (strcmp(unit_header.params[i].name, name) == 0) return (int)i;
    return -1;
}

static int preset_index(const char* name) {
    for (int i = 0; i < (int)BrachettiSynth::k_NumPrograms; ++i)
        if (strcmp(BrachettiSynth::getPresetName((uint8_t)i), name) == 0) return i;
    return -1;
}

static BrachettiSynth* fresh_synth() {
    BrachettiSynth* s = new BrachettiSynth();
    unit_runtime_desc_t desc = {};
    desc.target = unit_header.target;
    desc.api = unit_header.api;
    desc.samplerate = 48000;
    desc.frames_per_buffer = kBlock;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;
    if (s->Init(&desc) != k_unit_err_none) {
        fprintf(stderr, "Init failed\n");
        exit(2);
    }
    // What the OS does at boot: send every parameter's header default.
    for (uint32_t i = 0; i < unit_header.num_params; ++i)
        s->setParameter((uint8_t)i, unit_header.params[i].init);
    return s;
}

static void die(int line, const std::string& msg) {
    fprintf(stderr, "script line %d: %s\n", line, msg.c_str());
    exit(3);
}

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--list") == 0) {
        printf("unit %s presets %d params %u\n", unit_header.name,
               (int)BrachettiSynth::k_NumPrograms, unit_header.num_params);
        for (int i = 0; i < (int)BrachettiSynth::k_NumPrograms; ++i)
            printf("preset %d %s\n", i, BrachettiSynth::getPresetName((uint8_t)i));
        for (uint32_t i = 0; i < unit_header.num_params; ++i) {
            const unit_param_t& p = unit_header.params[i];
            printf("param %u %s %d %d %d\n", i, p.name, p.min, p.max, p.init);
        }
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: %s <script> | --list\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    FILE* dump = (argc >= 3) ? fopen(argv[2], "wb") : nullptr;

    BrachettiSynth* s = nullptr;
    std::string scen = "-";
    int render_no = 0, lineno = 0;
    std::vector<float> buf(2 * 48000 * 4);
    std::string line;
    while (std::getline(in, line)) {
        ++lineno;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string cmd;
        if (!(ls >> cmd)) continue;
        if (cmd == "scenario") {
            ls >> scen;
            delete s;
            s = fresh_synth();
            render_no = 0;
            continue;
        }
        if (!s) die(lineno, "command before the first scenario");
        if (cmd == "preset") {
            // The name runs to the end of the line ("Ac Tom" has a space).
            std::string name; std::getline(ls >> std::ws, name);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\r'))
                name.pop_back();
            const int idx = preset_index(name.c_str());
            if (idx < 0) die(lineno, "no preset " + name);
            const int slot = param_slot("Program");
            if (slot >= 0) s->setParameter((uint8_t)slot, idx);
            else if (BrachettiSynth::k_NumPrograms != 1) die(lineno, "no Program parameter");
        } else if (cmd == "param") {
            std::string name; int v; ls >> name >> v;
            const int slot = param_slot(name.c_str());
            if (slot < 0) die(lineno, "no parameter " + name);
            s->setParameter((uint8_t)slot, v);
        } else if (cmd == "gate_on") {
            int v; ls >> v; s->GateOn((uint8_t)v);
        } else if (cmd == "gate_off") {
            s->GateOff();
        } else if (cmd == "note_on") {
            int n, v; ls >> n >> v; s->NoteOn((uint8_t)n, (uint8_t)v);
        } else if (cmd == "note_rel") {
            int o, v; ls >> o >> v;
            int n = (int)s->PresetRootNote() + o;
            n = n < 0 ? 0 : (n > 127 ? 127 : n);
            s->NoteOn((uint8_t)n, (uint8_t)v);
        } else if (cmd == "note_off") {
            int n; ls >> n; s->NoteOff((uint8_t)n);
        } else if (cmd == "note_off_rel") {
            int o; ls >> o;
            int n = (int)s->PresetRootNote() + o;
            n = n < 0 ? 0 : (n > 127 ? 127 : n);
            s->NoteOff((uint8_t)n);
        } else if (cmd == "all_off") {
            s->AllNoteOff();
        } else if (cmd == "bend") {
            int b; ls >> b; s->PitchBend((uint16_t)b);
        } else if (cmd == "reset") {
            s->Reset();
        } else if (cmd == "suspend") {
            s->Suspend();
        } else if (cmd == "resume") {
            s->Resume();
        } else if (cmd == "render_only") {
            // Render without hashing or output: the CPU bench (arm_cpu.sh)
            // counts instructions, and the hash loop would count with them.
            int frames; ls >> frames;
            frames = (frames + kBlock - 1) / kBlock * kBlock;
            if ((size_t)frames * 2 > buf.size()) buf.resize((size_t)frames * 2);
            for (int f = 0; f < frames; f += kBlock)
                s->processBlock(&buf[(size_t)f * 2], kBlock);
        } else if (cmd == "render") {
            int frames; ls >> frames;
            frames = (frames + kBlock - 1) / kBlock * kBlock;
            if ((size_t)frames * 2 > buf.size()) buf.resize((size_t)frames * 2);
            for (int f = 0; f < frames; f += kBlock)
                s->processBlock(&buf[(size_t)f * 2], kBlock);
            uint64_t h = 1469598103934665603ULL;
            float peak = 0.0f;
            for (int i = 0; i < frames * 2; ++i) {
                float x = buf[(size_t)i];
                if (x == 0.0f) x = 0.0f;            // fold -0 onto +0
                uint32_t bits; memcpy(&bits, &x, 4);
                for (int b = 0; b < 4; ++b) {
                    h ^= (bits >> (8 * b)) & 0xFFu;
                    h *= 1099511628211ULL;
                }
                if (std::isnan(x)) peak = NAN;
                else if (fabsf(x) > peak) peak = fabsf(x);
            }
            if (dump) fwrite(buf.data(), sizeof(float), (size_t)frames * 2, dump);
            printf("%s %d %016llx %.6f\n", scen.c_str(), render_no++,
                   (unsigned long long)h, peak);
        } else {
            die(lineno, "unknown command " + cmd);
        }
    }
    delete s;
    if (dump) fclose(dump);
    return 0;
}
