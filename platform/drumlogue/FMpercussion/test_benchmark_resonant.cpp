/**
 * @file test_benchmark_resonant.cpp
 * @brief CPU cycle benchmarks for resonant synthesis
 *
 * Compile: g++ -o bench test_benchmark_resonant.cpp -O3 -mfpu=neon
 * Run: ./bench
 */

#include <arm_neon.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

// Read CPU cycle counter (ARM Cortex-A9)
static inline uint32_t read_cycle_counter(void) {
    uint32_t cycles;
    asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r" (cycles));
    return cycles;
}

// Simple resonant process simulation
float32x4_t resonant_process_sim(float32x4_t sin_f0, float32x4_t cos_fc,
                                  float32x4_t a, float32x4_t one) {
    float32x4_t a_sq = vmulq_f32(a, a);
    float32x4_t two_a = vmulq_f32(vdupq_n_f32(2.0f), a);

    float32x4_t denom = vsubq_f32(vaddq_f32(one, a_sq),
                                   vmulq_f32(two_a, cos_fc));

    // Protect against division by zero
    denom = vmaxq_f32(denom, vdupq_n_f32(0.0001f));

    // Division - the expensive part
    float32x4_t result = fast_div_neon(sin_f0, denom);

    return result;
}

// Optimized version using reciprocal
float32x4_t resonant_process_opt(float32x4_t sin_f0, float32x4_t cos_fc,
                                  float32x4_t a, float32x4_t one) {
    float32x4_t a_sq = vmulq_f32(a, a);
    float32x4_t two_a = vmulq_f32(vdupq_n_f32(2.0f), a);

    float32x4_t denom = vsubq_f32(vaddq_f32(one, a_sq),
                                   vmulq_f32(two_a, cos_fc));

    // Protect against division by zero
    denom = vmaxq_f32(denom, vdupq_n_f32(0.0001f));

    // Use reciprocal + multiply instead of division
    float32x4_t recip = vrecpeq_f32(denom);
    // One Newton-Raphson iteration for better accuracy
    recip = vmulq_f32(vrecpsq_f32(denom, recip), recip);

    float32x4_t result = vmulq_f32(sin_f0, recip);

    return result;
}


void test_benchmark_resonant_operations() {
    const int ITERATIONS = 10000;

    float32x4_t a = vdupq_n_f32(0.5f);
    float32x4_t b = vdupq_n_f32(1.5f);
    float32x4_t c = vdupq_n_f32(2.0f);
    float32x4_t d = vdupq_n_f32(3.0f);
    float32x4_t result;

    // Benchmark 1: Standard division (slow)
    uint32_t start = read_cycle_counter();
    for (int i = 0; i < ITERATIONS; i++) {
        result = fast_div_neon(a, b);
    }
    uint32_t end = read_cycle_counter();
    uint32_t div_cycles = (end - start) / ITERATIONS;
    printf("fast_div_neon: %d cycles\n", div_cycles);

    // Benchmark 2: Division using reciprocal + multiply (faster)
    start = read_cycle_counter();
    for (int i = 0; i < ITERATIONS; i++) {
        float32x4_t recip = vrecpeq_f32(b);
        recip = vmulq_f32(vrecpsq_f32(b, recip), recip);
        result = vmulq_f32(a, recip);
    }
    end = read_cycle_counter();
    uint32_t recip_cycles = (end - start) / ITERATIONS;
    printf("reciprocal method: %d cycles\n", recip_cycles);

    // Benchmark 3: Approximate division (for denom protection)
    start = read_cycle_counter();
    for (int i = 0; i < ITERATIONS; i++) {
        // Denom protection using vmaxq_f32 is cheap
        float32x4_t denom = vmaxq_f32(b, vdupq_n_f32(0.0001f));
        float32x4_t recip = vrecpeq_f32(denom);
        recip = vmulq_f32(vrecpsq_f32(denom, recip), recip);
        result = vmulq_f32(a, recip);
    }
    end = read_cycle_counter();
    uint32_t protected_cycles = (end - start) / ITERATIONS;
    printf("protected division: %d cycles\n", protected_cycles);

    // Expected results:
    // - fast_div_neon: ~20-30 cycles
    // - reciprocal method: ~8-12 cycles
    // - protected division: ~10-14 cycles
}



typedef struct {
    const char* name;
    uint32_t cycles_per_op;
    uint32_t ops_per_second;  // At 1GHz
} benchmark_result_t;

/**
 * Benchmark NEON sine approximation
 */
benchmark_result_t bench_sine_neon() {
    const int ITERATIONS = 10000;
    float32x4_t phases = {0.1f, 0.5f, 1.0f, 1.5f};
    float32x4_t result;

    uint32_t start = read_cycle_counter();

    for (int i = 0; i < ITERATIONS; i++) {
        result = neon_sin(phases);
        phases = vaddq_f32(phases, vdupq_n_f32(0.01f));
    }

    uint32_t end = read_cycle_counter();
    uint32_t total_cycles = end - start;

    benchmark_result_t res;
    res.name = "NEON Sine (4 values)";
    res.cycles_per_op = total_cycles / ITERATIONS;
    res.ops_per_second = 1000000000 / res.cycles_per_op;  // At 1GHz

    return res;
}

/**
 * Benchmark PRNG throughput
 */
benchmark_result_t bench_prng() {
    const int ITERATIONS = 100000;
    neon_prng_t rng;
    neon_prng_init(&rng, 0x12345678);

    uint32_t start = read_cycle_counter();

    for (int i = 0; i < ITERATIONS; i++) {
        uint32x4_t rand = neon_prng_rand_u32(&rng);
        (void)rand;
    }

    uint32_t end = read_cycle_counter();
    uint32_t total_cycles = end - start;

    benchmark_result_t res;
    res.name = "NEON PRNG (4 streams)";
    res.cycles_per_op = total_cycles / ITERATIONS;
    res.ops_per_second = 1000000000 / res.cycles_per_op;

    return res;
}

/**
 * Run all benchmarks
 */
void run_benchmarks() {
    printf("\n=== CPU BENCHMARKS ===\n\n");

    benchmark_result_t results[4];
    results[0] = bench_sine_neon();
    results[1] = bench_prng();

    for (int i = 0; i < 2; i++) {
        printf("%s:\n", results[i].name);
        printf("  Cycles per op: %d\n", results[i].cycles_per_op);
        printf("  Ops/sec @1GHz: %dM\n", results[i].ops_per_second / 1000000);
        printf("\n");
    }

    // Verify against targets
    assert(results[0].cycles_per_op < 20);  // Target: <20 cycles per 4 sines
    assert(results[1].cycles_per_op < 10);  // Target: <10 cycles per 4 randoms
}

// Expected output:
// === CPU BENCHMARKS ===
//
// NEON Sine (4 values):
//   Cycles per op: 12
//   Ops/sec @1GHz: 83M
//
// NEON PRNG (4 streams):
//   Cycles per op: 8
//   Ops/sec @1GHz: 125M



// TODO complete this test
// void benchmark_division() {
//     const int ITERATIONS = 100000;

//     float32x4_t sin_f0 = vdupq_n_f32(0.5f);
//     float32x4_t cos_fc = vdupq_n_f32(0.5f);
//     float32x4_t a = vdupq_n_f32(0.5f);
//     float32x4_t one = vdupq_n_f32(1.0f);
//     float32x4_t result;

//     printf("\n=== Resonant Synthesis Benchmarks ===\n");

//     // Benchmark 1: Direct division
//     uint32_t start = read_cycle_counter();
//     for (int i = 0; i < ITERATIONS; i++) {
