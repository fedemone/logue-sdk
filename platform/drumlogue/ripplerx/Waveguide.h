// Copyright 2025 tilr
// Waveguide for OpenTube and ClosedTube models
// Uses float32x4_t ARM NEON vectors for consistency with rest of codebase
#pragma once
#include "constants.h"
#include "float_math.h"

class alignas(16) Waveguide
{
public:
	Waveguide();  // Proper initialization in .cpp
	~Waveguide() {};

	void update(float32_t f_0, float32_t vel, bool isRelease);
    float32x4_t process(float32x4_t input);
    void clear();

    // --- Hot Data (128-bit aligned) ---
    float32x4_t tube[tube_len];
    float32x4_t vY1;           // Damping filter state
    float32x4_t vAP_State;     // All-pass interpolation state vector
    float32x4_t vRadius;
    float32x4_t vOneMinusRad;
    float32x4_t vDecay;
    float32x4_t vPolarity;
    float32x4_t vG;            // All-pass coefficient (eta)
    float32x4_t vAP_State_Prev_X;

    // --- Warm Data ---
    int read_ptr;
    int write_ptr;
    bool is_closed;
    float32_t srate;
	float32_t decay;
	float32x4_t radius;
	float32x4_t max_radius;
	float32_t rel;
	float32_t vel_decay;

private:
	float32_t tube_decay;
	float32x4_t y;
	float32x4_t y1;
};