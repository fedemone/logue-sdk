#pragma once
// Brachetti common library — the Model knob's physical-model templates.
//
// The Model knob picks one of nine physical models.  On the modal units it
// swaps the bank's mode ratios for the model's template (modal_bank.h); on
// every unit with an exciter it also sets the default balance between the
// noise burst's body and click bands (Exciter::noise_band_mix).
//
// Provides: the Model enum (k_String .. k_ClosedTube), model_name,
// model_noise_band_mix.
#include <cstdint>
#include "knobs.h"

namespace brc {

enum Model : uint8_t {
    k_String,
    k_Beam,
    k_SquarePlate,
    k_Membrane,
    k_Plate,
    k_Drumhead,
    k_MarimbaBar,
    k_OpenTube,
    k_ClosedTube,
    k_NumModels
};

// What the drumlogue shows for each Model knob position.
// Namespace-scope `static`, not a static local: see name_at in knobs.h.
static const char* const kModelNames[k_NumModels] = {
    "String", "Beam", "Square", "Membrn", "Plate",
    "Drumhd", "Marimba", "OpnTube", "ClsTube"
};
inline const char* model_name(int32_t m) { return name_at(kModelNames, m, nullptr); }

// Default noise band balance by model (a preset's own value wins when it has
// one): percussion models lean on the bright click band, tubes on the body
// band.
inline float model_noise_band_mix(uint8_t m) {
    const bool percussion = (m == k_SquarePlate || m == k_Membrane || m == k_Plate ||
                             m == k_Drumhead || m == k_MarimbaBar);
    const bool tube = (m == k_OpenTube || m == k_ClosedTube);
    if (percussion) return 0.70f;
    if (tube)       return 0.35f;
    return 0.50f;
}

}  // namespace brc
