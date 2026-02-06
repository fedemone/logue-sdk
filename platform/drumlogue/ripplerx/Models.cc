//
//
//
//

#include "Models.h"

// --- Accessors ---
const float32_t* getBFree() { return bFree; }



const float32_t* getAModels(int model) {
    if (model < 0 || model >= c_modelElements) {
        // Exceptional: return a safe fallback (first model) and optionally log error
        return aModels[0];
    }
    return aModels[model];
}
const float32_t* getBModels(int model) {
    if (model < 0 || model >= c_modelElements) {
        return bModels[0];
    }
    return bModels[model];
}

/** optimized version: more precise + higher throughput
* TODO check that model pointer is 16 byte aligned
*/
static inline void freqs_to_ratio(float32_t* model) {
    float32_t f0 = model[0];

    // Safety check
    if (f0 < 1e-9f && f0 > -1e-9f) return; // stronger epsilon check than == 0.0f

    // 1. Scalar Division (High Precision)
    // Instead of reciprocal with vrecpe (low precision), use standard FPU.
    // Being outside loop, it's not impacting (10-20 cycles una tantum).
    float32_t inv_f0_val = 1.0f / f0;

    // duplicate same scalar value
    float32x4_t inv_f0_vec = vdupq_n_f32(inv_f0_val);

    // 2. Loop Unrolling (4x4 = 16 float per cycle)
    // Riduce loop overhead and hide memory latency
    for (int j = 0; j < 64; j += 16) {
        // Carica 4 vettori (16 float totali)
        float32x4_t v0 = vld1q_f32(&model[j]);
        float32x4_t v1 = vld1q_f32(&model[j + 4]);
        float32x4_t v2 = vld1q_f32(&model[j + 8]);
        float32x4_t v3 = vld1q_f32(&model[j + 12]);

        // multiply 1/f0
        v0 = vmulq_f32(v0, inv_f0_vec);
        v1 = vmulq_f32(v1, inv_f0_vec);
        v2 = vmulq_f32(v2, inv_f0_vec);
        v3 = vmulq_f32(v3, inv_f0_vec);

        // save results
        vst1q_f32(&model[j], v0);
        vst1q_f32(&model[j + 4], v1);
        vst1q_f32(&model[j + 8], v2);
        vst1q_f32(&model[j + 12], v3);
    }
}


void recalcBeam(bool resA, float32_t ratio) {
    float32_t* model = resA ? aModels[ModelNames::Beam] : bModels[ModelNames::Beam];

    // Convert int array to float array
    float pwr_2_of_index_float[9];
    for (int i=0; i<9; ++i) {
        pwr_2_of_index_float[i] = (float)pwr_2_of_index[i];
    }

    // Call the static NEON optimizer
    Model::recalcBeam(model, ratio, bFree, pwr_2_of_index_float);

    // Final step
    freqs_to_ratio(model);
}


void recalcMembrane(bool resA, float32_t ratio) {
    float32_t* model = resA ? aModels[ModelNames::Membrane] : bModels[ModelNames::Membrane];

    // Call the static NEON optimizer
    Model::recalcMembrane(model, ratio);

    // Final step
    freqs_to_ratio(model);
}


void recalcPlate(bool resA, float32_t ratio) {
    float32_t* model = resA ? aModels[ModelNames::Plate] : bModels[ModelNames::Plate];

    // Convert int array to float array
    float pwr_2_of_index_float[9];
    for (int i=0; i<9; ++i) {
        pwr_2_of_index_float[i] = (float)pwr_2_of_index[i];
    }

    // Call the static NEON optimizer
    Model::recalcPlate(model, ratio, pwr_2_of_index_float);

    // Final step
    freqs_to_ratio(model);
}
//
//
//
//