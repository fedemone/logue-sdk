"""Diagnose CrashA high score by checking what metrics differ most."""
import sys
from pathlib import Path
from batch_tune_runner import (
    parse_presets, find_render_for_preset, class_weighted_score, MANUAL_SAMPLE_TO_PRESET
)
from pre_hw_analysis import compare_pair

RDIR = Path('.')
RENDER_DIR = RDIR / 'rendered_tune'
SYNTH = RDIR / 'synth_engine.h'
SAMPLES = list((RDIR / 'samples').glob('*.wav'))
cymbal_samples = [s for s in SAMPLES if MANUAL_SAMPLE_TO_PRESET.get(s.name) == 'Cymbal']
presets = parse_presets(SYNTH)
preset = presets.get('Cymbal')
wav = find_render_for_preset(RENDER_DIR, preset)

for s in cymbal_samples:
    comp = compare_pair(s, wav, auto_note_align=False)
    m = comp['metrics']
    rf = comp['ref_features']
    ren = comp['ren_features']
    sc = class_weighted_score(comp['score'], 'Cymbal', m)
    ct = min(m['centroid_pct'], m['centroid_pitchnorm_pct'])
    rt = min(m['rolloff_pct'], m['rolloff_pitchnorm_pct'])
    mr = m['mrstft_log_l1']
    
    print(f"\n=== {s.name} (score={sc:.2f}) ===")
    print(f"  f0:          {rf['f0_hz']:.0f} vs {ren['f0_hz']:.0f} Hz  pct={m['f0_pct']:.1f}  contrib={0.16*m['f0_pct']:.2f}")
    print(f"  attack:      {rf['attack_ms']:.1f} vs {ren['attack_ms']:.1f} ms  pct={m['attack_pct']:.1f}  contrib={0.14*m['attack_pct']:.2f}")
    print(f"  t60:         {rf['t60_ms']:.0f} vs {ren['t60_ms']:.0f} ms  pct={m['t60_pct']:.1f}  contrib={0.18*m['t60_pct']:.2f}")
    print(f"  centroid:    {ct:.1f}%  contrib={0.16*ct:.2f}  (raw={m['centroid_pct']:.1f}, pitchnorm={m['centroid_pitchnorm_pct']:.1f})")
    print(f"  rolloff:     {rt:.1f}%  contrib={0.10*rt:.2f}  (raw={m['rolloff_pct']:.1f}, pitchnorm={m['rolloff_pitchnorm_pct']:.1f})")
    print(f"  flatness:    {rf['flatness']:.3f} vs {ren['flatness']:.3f}  pct={m['flatness_pct']:.1f}  total={0.20*m['flatness_pct']:.2f}")
    print(f"  flux:        {rf['flux']:.2f} vs {ren['flux']:.2f}  pct={m['flux_pct']:.1f}  total={0.20*m['flux_pct']:.2f}")
    print(f"  inharm:      {rf['inharm']:.4f} vs {ren['inharm']:.4f}  pct={m['inharm_pct']:.1f}  contrib={0.10*m['inharm_pct']:.2f}")
    print(f"  mrstft:      {mr:.4f}  contrib={8.0*mr:.2f}")
    print(f"  centroid_dec:{m['centroid_decay_slope_pct']:.1f}%  contrib={0.04*m['centroid_decay_slope_pct']:.2f}")
    print(f"  timbre_cos:  {m['timbre_vec_cosdist']:.4f}  contrib={4.0*m['timbre_vec_cosdist']:.2f}")
    print(f"  Raw contrib total: {comp['score']:.2f}")
    class_bonus = 0.12*m['flatness_pct'] + 0.12*m['flux_pct']
    print(f"  PERCUSSIVE class bonus: {class_bonus:.2f}")
    print(f"  TOTAL: {sc:.2f}")
