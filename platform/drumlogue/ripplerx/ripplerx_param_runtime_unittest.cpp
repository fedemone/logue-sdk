// ripplerx_param_runtime_unittest.cpp
// Standalone runtime unit test for RipplerX parameter mapping (no ARM NEON)
#include "ripplerx_debug.h"
#include <stdio.h>

int main() {
    printf("RipplerX Runtime Parameter Test (Standalone Stub)\n");
    RipplerX synth;
    int8_t init_result = synth.Init(nullptr);
    if (init_result != 0) {
        printf("Init failed: %d\n", init_result);
        return 1;
    }
    // After Init, check runtime value of a_partials
    int a_partials_runtime = (int)synth.getParameterValue(ProgramParameters::a_partials);
    printf("Runtime a_partials value after Init: %d\n", a_partials_runtime);
    if (a_partials_runtime < 0 || a_partials_runtime >= (int)c_partialElements) {
        printf("ERROR: a_partials runtime value is out of valid index range!\n");
        return 2;
    }
    printf("a_partials runtime value is valid.\n");
    // Optionally, check other parameters here
    return 0;
}
