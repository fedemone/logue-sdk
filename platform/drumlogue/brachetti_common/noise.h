#pragma once
// Brachetti common library — noise and random sources.
//
//   brc::xorshift32   one step of the xorshift32 generator (the state)
//   FastNoise         white noise in [-1, 1) from xorshift32 (the exciters)
//   brc::lcg_unit     one step of a linear congruential generator, in [0, 1)
//   brc::PinkFilter   white → pink noise (Paul Kellet's refined filter)
#include <cstdint>

namespace brc {

// One step of xorshift32 (Marsaglia): the new state, never 0 from a non-zero
// seed.  Callers scale it to the range they need.
inline uint32_t xorshift32(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// One step of the Numerical Recipes LCG, as a float in [0, 1) from the top 24
// bits (the low bits of an LCG are poorly random).
inline float lcg_unit(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return ((state >> 8) * (1.0f / 16777216.0f));
}

// Pink noise from white: Paul Kellet's refined filter (seven one-poles summed,
// accurate to ±0.05 dB above 9.2 Hz), scaled by 0.11 to keep about the level
// of the white input.
struct PinkFilter {
    float s[7] = {};

    void reset() {
        for (int i = 0; i < 7; ++i) s[i] = 0.0f;
    }

    float process(float w) {
        s[0] = 0.99886f * s[0] + w * 0.0555179f;
        s[1] = 0.99332f * s[1] + w * 0.0750759f;
        s[2] = 0.96900f * s[2] + w * 0.1538520f;
        s[3] = 0.86650f * s[3] + w * 0.3104856f;
        s[4] = 0.55000f * s[4] + w * 0.5329522f;
        s[5] = -0.7616f * s[5] - w * 0.0168980f;
        const float p = s[0] + s[1] + s[2] + s[3] + s[4] + s[5] + s[6] + w * 0.5362f;
        s[6] = w * 0.115926f;
        return p * 0.11f;
    }
};

}  // namespace brc

/**
 * Fast PRNG (Pseudo-Random Number Generator) for Audio.
 * Uses Xorshift algorithm. Extremely lightweight for ARM Cortex.
 */
struct FastNoise {
    uint32_t seed = 2463534242UL; // Non-zero initial seed

    inline float process() {
        // Convert uint32_t to a float between -1.0f and +1.0f
        // 2147483648.0f is 2^31
        return ((float)(int32_t)brc::xorshift32(seed) / 2147483648.0f);
    }
};
