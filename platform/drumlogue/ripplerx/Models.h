
#pragma once
#include "float_math.h"
#include "constants.h"

// Model data is now static in Models.cc

// triggered on model ratio param changes
void recalcBeam(bool resA, float32_t ratio);
void recalcMembrane(bool resA, float32_t ratio);
void recalcPlate(bool resA, float32_t ratio);

// Accessors for static arrays (read-only)
const float32_t* getBFree();
const float32_t* getAModels(int model); // model: ModelNames enum
const float32_t* getBModels(int model); // model: ModelNames enum