/**
 * @file test_levelref.cc
 * @brief Host-side tests for LevelRef's parameter contract.
 *
 * LevelRef is a measurement instrument, so the property under test is not that
 * it sounds right but that it never misreports its own level. It failed exactly
 * there once: PinkNz cannot be driven past -10 LUFS, the target was silently
 * capped, and getParameterValue() returning the capped value was assumed to make
 * "the knob stop where the signal does". On hardware the drumlogue displays the
 * value it sent, so the knob read up to 0 while -10 LUFS came out -- ten steps
 * of a reference signal lying about itself, which is worse than no reference.
 *
 * The request and the delivered level are now separate, ActLUFS reports the
 * second, and these checks pin that down.
 *
 * The DSP is scalar and needs no NEON, so this builds natively:
 *
 *   g++ -std=c++14 -O2 -I . -I ../common -o test_levelref test_levelref.cc -lm
 *   ./test_levelref
 */

#include <initializer_list>
#include <cstdio>
#include "synth.h"
static int fails = 0;
static void chk(const char* what, int32_t got, int32_t want) {
  bool ok = (got == want);
  if (!ok) fails++;
  printf("  %-46s got %4d  want %4d  %s\n", what, got, want, ok ? "ok" : "FAIL");
}
int main(void) {
  LevelRef u; unit_runtime_desc_t d{}; d.samplerate = 48000; d.output_channels = 2;
  u.Init(&d);
  const uint8_t SIG = LevelRef::k_param_signal, TGT = LevelRef::k_param_target,
                ACT = LevelRef::k_param_actual;

  printf("PinkNz, dial the knob to 0 (the case that misled the measurement)\n");
  u.setParameter(SIG, LevelRef::k_sig_pink);
  u.setParameter(TGT, 0);
  chk("TgtLUFS reads back what was dialed", u.getParameterValue(TGT), 0);
  chk("ActLUFS reports the ceiling actually delivered", u.getParameterValue(ACT), -10);

  printf("switch to WhitNz -- the request must be honoured, not lost to the clamp\n");
  u.setParameter(SIG, LevelRef::k_sig_white);
  chk("TgtLUFS unchanged", u.getParameterValue(TGT), 0);
  chk("ActLUFS now reaches 0", u.getParameterValue(ACT), 0);

  printf("back to PinkNz -- the ceiling reapplies, the request survives\n");
  u.setParameter(SIG, LevelRef::k_sig_pink);
  chk("TgtLUFS still 0", u.getParameterValue(TGT), 0);
  chk("ActLUFS back to -10", u.getParameterValue(ACT), -10);

  printf("Sine100 ceiling is -2\n");
  u.setParameter(SIG, LevelRef::k_sig_sine100);
  chk("ActLUFS", u.getParameterValue(ACT), -2);

  printf("below every ceiling the two agree exactly\n");
  u.setParameter(SIG, LevelRef::k_sig_pink);
  for (int v : {-40, -30, -25, -20, -11}) {
    u.setParameter(TGT, v);
    chk("TgtLUFS", u.getParameterValue(TGT), v);
    chk("ActLUFS", u.getParameterValue(ACT), v);
  }

  printf("out-of-range requests are still clamped to the param range\n");
  u.setParameter(TGT, 99);  chk("TgtLUFS(99) -> 0", u.getParameterValue(TGT), 0);
  u.setParameter(TGT, -999); chk("TgtLUFS(-999) -> -40", u.getParameterValue(TGT), -40);

  printf("%s\n", fails ? "FAILURES" : "all checks passed");
  return fails != 0;
}
