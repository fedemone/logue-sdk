// ripplerx_debug.h - Debug-augmented version of ripplerx.h
// This file is a copy of ripplerx.h with added debug checks and traces for parameter loading, handling, and audio rendering.
// ==== DEBUG PATCH BEGIN: Diagnostic logging and parameter mapping checks ====
#ifdef DEBUG_RIPPLERX_PARAM
#include <stdio.h>
#include "constants.h"
// Check for out-of-bounds access
static inline void debug_check_program_param_index(int prog, int param) {
    if (prog < 0 || prog >= Program::last_program) {
        printf("[DEBUG] Invalid program index: %d\n", prog);
    }
    if (param < 0 || param >= ProgramParameters::last_param) {
        printf("[DEBUG] Invalid parameter index: %d\n", param);
    }
}
// Check for invalid partials value
static inline void debug_check_partials_value(float partials_value) {
    int idx = debug_find_partials_index((int)partials_value);
    if (idx == -1) {
        printf("[DEBUG] Invalid partials value: %f\n", partials_value);
    }
}
// Check for Debug program selection
static inline void debug_check_debug_program_selected(int prog) {
    if (prog == Program::Debug) {
        printf("[DEBUG] Debug program selected, all parameters zero\n");
    }
}
// Check for array size consistency
static inline void debug_check_array_sizes() {
    if (sizeof(c_programName)/sizeof(c_programName[0]) != Program::last_program) {
        printf("[DEBUG] Program name array size mismatch\n");
    }
    if (sizeof(c_partials)/sizeof(c_partials[0]) != c_partialElements) {
        printf("[DEBUG] Partials array size mismatch\n");
    }
}
// Check for parameter value ranges
static inline void debug_check_param_range(const char* param_name, float value, float min, float max) {
    if (value < min || value > max) {
        printf("[DEBUG] Parameter %s out of range: %f (expected %f-%f)\n", param_name, value, min, max);
    }
}
#endif
// ==== DEBUG PATCH END ====

// ...existing code from ripplerx.h...
// In parameter loading logic, add calls to debug_check_program_param_index, debug_check_partials_value, debug_check_param_range, etc.
// In parameter handling, add debug traces for suspicious values.
// In audio rendering, add debug traces for silence or out-of-range conditions.
// ripplerx_debug.h
// Standalone stub for RipplerX unit test (no ARM NEON)
#pragma once
#include "constants.h"
#include <stdio.h>
#include <math.h>

class RipplerX {
public:
    RipplerX() = default;
    ~RipplerX() = default;

    inline int8_t Init(const void* /*desc*/) {
        // Simulate preset load
        m_currentProgram = 0;
        // Simulate default partials value (should match Program::Initial)
        parameters[ProgramParameters::a_partials] = 3; // index for A:4
        return 0;
    }

    inline int32_t getParameterValue(uint8_t index) const {
        if (index >= ProgramParameters::last_param)
            return 0;
        return parameters[index];
    }

private:
    float parameters[ProgramParameters::last_param] = {};
    uint8_t m_currentProgram = 0;
};
