// multi_load.cpp — load drumlogue units the way a host loads plugins (each
// with RTLD_LOCAL, all in one process) and print every display string each
// unit gives: every parameter of type "strings", at every value of its range.
// multi_load.sh runs it once per unit alone and once with all of them
// loaded together; any difference is state one unit shares with another.
//
//     multi_load <unit.so> [<unit.so> ...]
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include "unit.h"

typedef int8_t (*init_fn)(const unit_runtime_desc_t*);
typedef const char* (*str_fn)(uint8_t, int32_t);
typedef void (*set_fn)(uint8_t, int32_t);

static uint8_t no_banks() { return 0; }
static uint8_t no_samples(uint8_t) { return 0; }
static const sample_wrapper_t* no_sample(uint8_t, uint8_t) { return nullptr; }

int main(int argc, char** argv) {
    void* h[16] = {};
    // Load (and initialise) every unit first, so a shared symbol is already
    // bound to the first unit's copy when the later ones are asked.
    for (int u = 1; u < argc && u < 16; ++u) {
        h[u] = dlopen(argv[u], RTLD_NOW | RTLD_LOCAL);
        if (!h[u]) { fprintf(stderr, "dlopen %s: %s\n", argv[u], dlerror()); return 2; }
        const unit_header_t* hdr = (const unit_header_t*)dlsym(h[u], "unit_header");
        init_fn init = (init_fn)dlsym(h[u], "unit_init");
        unit_runtime_desc_t d = {};
        d.target = hdr->target;
        d.api = hdr->api;
        d.samplerate = 48000;
        d.frames_per_buffer = 64;
        d.output_channels = 2;
        d.get_num_sample_banks = no_banks;
        d.get_num_samples_for_bank = no_samples;
        d.get_sample = no_sample;
        if (init(&d) != k_unit_err_none) { fprintf(stderr, "unit_init failed: %s\n", argv[u]); return 2; }
    }
    for (int u = 1; u < argc && u < 16; ++u) {
        const unit_header_t* hdr = (const unit_header_t*)dlsym(h[u], "unit_header");
        str_fn str = (str_fn)dlsym(h[u], "unit_get_param_str_value");
        set_fn set = (set_fn)dlsym(h[u], "unit_set_param_value");
        // Every preset in turn (through Program), so preset-dependent strings
        // (Partls' mode count) are covered too.
        const int programs = hdr->params[0].max - hdr->params[0].min + 1;
        for (int p = 0; p < programs; ++p) {
            set(0, hdr->params[0].min + p);
            for (uint32_t i = 0; i < hdr->num_params; ++i) {
                const unit_param_t& q = hdr->params[i];
                if (q.type != k_unit_param_type_strings) continue;
                for (int v = q.min; v <= q.max; ++v) {
                    const char* s = str((uint8_t)i, v);
                    printf("%s|preset %d|%s|%d|%s\n", hdr->name, p, q.name, v, s ? s : "(null)");
                }
            }
        }
    }
    return 0;
}
