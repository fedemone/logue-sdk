/**
 * @file test_levelref.cc
 * @brief Host-side tests for LevelRef's parameter contract.
 *
 * LevelRef is a measurement instrument, so the property under test is not that
 * it sounds right but that it never misreports its own level. It has failed
 * there twice, both times the same way, and both times on hardware:
 *
 *   1. PinkNz cannot be driven past -10 LUFS (12.1 dB of crest puts its peak at
 *      full scale there). The target was capped, and getParameterValue()
 *      returning the capped value was assumed to make "the knob stop where the
 *      signal does". The drumlogue displays the value it sent, so the knob read
 *      up to 0 while -10 LUFS came out, for the top ten steps.
 *   2. The fix added an ActLUFS read-out beside TgtLUFS. But unit_param_t has no
 *      read-only flag, so every parameter is a turnable knob whose displayed
 *      value is the one the drumlogue sent. Turning ActLUFS moved the number and
 *      changed nothing -- a control that does nothing, next to one that does.
 *
 * So the level is now reported by the only display a knob cannot lie through:
 * TgtLUFS is typed strings and renders the loudness being DELIVERED, marked MAX
 * where the signal's ceiling has been reached. These checks pin that down, and
 * assert the header carries no read-out parameter for the mistake to return to.
 *
 * The DSP is scalar and needs no NEON, so this builds natively:
 *
 *   g++ -std=c++14 -O2 -I . -I ../common -o test_levelref test_levelref.cc header.c -lm
 *   ./test_levelref
 */

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "synth.h"

extern "C" const unit_header_t unit_header;

static int fails = 0;

static void chk(const char* what, int32_t got, int32_t want) {
  const bool ok = (got == want);
  if (!ok) fails++;
  printf("  %-52s got %5d  want %5d  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

static void chks(const char* what, const char* got, const char* want) {
  const bool ok = (got != nullptr) && (strcmp(got, want) == 0);
  if (!ok) fails++;
  printf("  %-52s got %5s  want %5s  %s\n", what, got ? got : "(null)", want,
         ok ? "ok" : "FAIL");
}

int main(void) {
  LevelRef u;
  unit_runtime_desc_t d{};
  d.samplerate = 48000;
  d.output_channels = 2;
  u.Init(&d);

  const uint8_t SIG = LevelRef::k_param_signal, TGT = LevelRef::k_param_target;
  // TgtLUFS is an index: 0 is -40 LUFS, 40 is 0 LUFS.
  auto dial  = [&](int32_t lufs) { u.setParameter(TGT, lufs + 40); };
  auto shown = [&](int32_t lufs) { return u.getParameterStrValue(TGT, lufs + 40); };

  printf("the header offers three controls and no read-out to mis-turn\n");
  chk("TgtLUFS is typed strings, so the unit renders it",
      unit_header.params[TGT].type, k_unit_param_type_strings);
  chk("page 1 slot 3 is empty", (int32_t)strlen(unit_header.params[3].name), 0);
  for (int i = 3; i < 24; ++i) {
    if (strlen(unit_header.params[i].name) != 0) {
      printf("  FAIL param %d is named '%s'\n", i, unit_header.params[i].name);
      fails++;
    }
  }

  printf("PinkNz dialed to 0 -- the case that misled the measurement\n");
  u.setParameter(SIG, LevelRef::k_sig_pink);
  dial(0);
  chks("display reports the delivered level, marked MAX", shown(0), "-10 MAX");
  chk("index round-trips", u.getParameterValue(TGT), 40);

  printf("switch to WhitNz -- the request must be honoured, not lost to the cap\n");
  u.setParameter(SIG, LevelRef::k_sig_white);
  chks("display reaches 0 with no MAX", shown(0), "0");
  chk("index unchanged by the signal change", u.getParameterValue(TGT), 40);

  printf("back to PinkNz -- the ceiling reapplies, the request survives\n");
  u.setParameter(SIG, LevelRef::k_sig_pink);
  chks("display back to the ceiling", shown(0), "-10 MAX");
  chk("index still 40", u.getParameterValue(TGT), 40);

  printf("Sine100 stops at -2, Sine1k and WhitNz reach 0\n");
  u.setParameter(SIG, LevelRef::k_sig_sine100);
  chks("Sine100 at 0 shows -2 MAX", shown(0), "-2 MAX");
  u.setParameter(SIG, LevelRef::k_sig_sine1k);
  chks("Sine1k at 0 shows 0", shown(0), "0");

  printf("below every ceiling the display is the dialed number, unmarked\n");
  for (int sig : {(int)LevelRef::k_sig_pink, (int)LevelRef::k_sig_white}) {
    u.setParameter(SIG, sig);
    for (int v : {-40, -30, -25, -20, -11}) {
      dial(v);
      char want[12];
      snprintf(want, sizeof want, "%d", v);
      chks("display", shown(v), want);
    }
  }

  printf("out-of-range indices clamp to the parameter range\n");
  u.setParameter(TGT, 999);  chk("index(999) -> 40", u.getParameterValue(TGT), 40);
  u.setParameter(TGT, -999); chk("index(-999) -> 0", u.getParameterValue(TGT), 0);

  printf("%s\n", fails ? "FAILURES" : "all checks passed");
  return fails != 0;
}
