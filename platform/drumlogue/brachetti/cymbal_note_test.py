"""Test note=76 vs note=65 for Cymbal render."""
import sys, subprocess
from pathlib import Path
from batch_tune_runner import parse_presets, find_render_for_preset, class_weighted_score, MANUAL_SAMPLE_TO_PRESET
from pre_hw_analysis import compare_pair

RDIR = Path('.')
RENDER_DIR = RDIR / 'rendered_tune'
SYNTH = RDIR / 'synth_engine.h'
REND = RDIR / 'render_presets.cpp'
SAMPLES = list((RDIR / 'samples').glob('*.wav'))
cymbal_samples = [s for s in SAMPLES if MANUAL_SAMPLE_TO_PRESET.get(s.name) == 'Cymbal']

def modify_render_note(new_note):
    text = REND.read_text()
    # Change note for Cymbal (idx=13) in render_presets.cpp
    import re
    new_text = re.sub(
        r'(\{13,\s*)65(\s*,\s*6\.0f\s*,\s*"Cymbal")',
        rf'\g<1>{new_note}\g<2>',
        text
    )
    REND.write_text(new_text)

def compile_and_render():
    r = subprocess.run(['g++', '-std=c++17', '-O2', '-I.', '-Itest_stubs', '-I..', '-I../../common', '-I../common',
         '-DRUNTIME_COMMON_H_', 'render_presets.cpp', '-o', 'render_presets_tune'],
        capture_output=True, cwd=RDIR)
    if r.returncode != 0:
        print(f"Compile failed: {r.stderr.decode()[:200]}")
        return False
    subprocess.run(['./render_presets_tune', 'rendered_tune'], capture_output=True, cwd=RDIR)
    return True

def score_and_detail(label):
    presets = parse_presets(SYNTH)
    preset = presets.get('Cymbal')
    wav = find_render_for_preset(RENDER_DIR, preset)
    total_scores = []
    print(f"\n=== {label} ===")
    for s in cymbal_samples:
        comp = compare_pair(s, wav, auto_note_align=False)
        m = comp['metrics']
        ren = comp['ren_features']
        sc = class_weighted_score(comp['score'], 'Cymbal', m)
        ct = min(m['centroid_pct'], m['centroid_pitchnorm_pct'])
        total_scores.append(sc)
        print(f"  {s.name}: {sc:.2f}")
        print(f"    f0={ren['f0_hz']:.0f}Hz({m['f0_pct']:.1f}%)  centroid={ren['centroid_hz']:.0f}Hz({ct:.1f}%)")
        print(f"    t60={ren['t60_ms']:.0f}ms({m['t60_pct']:.1f}%)  flat={ren['flatness']:.3f}({m['flatness_pct']:.1f}%)")
        print(f"    flux={ren['flux']:.2f}({m['flux_pct']:.1f}%)  attack={ren['attack_ms']:.1f}ms({m['attack_pct']:.1f}%)")
        print(f"    mrstft={m['mrstft_log_l1']:.4f}")
    mean = sum(total_scores) / len(total_scores)
    print(f"  MEAN: {mean:.2f}")
    return mean

orig_rend = REND.read_text()
try:
    # Test note=65 (current)
    compile_and_render()
    score_and_detail("Note=65 (current)")
    
    # Test note=72
    modify_render_note(72)
    compile_and_render()
    score_and_detail("Note=72")

    # Test note=76
    REND.write_text(orig_rend)
    import re
    text = orig_rend
    text = re.sub(r'(\{13,\s*)65(\s*,\s*6\.0f\s*,\s*"Cymbal")', r'\g<1>76\g<2>', text)
    REND.write_text(text)
    compile_and_render()
    score_and_detail("Note=76")
    
    # Test note=80
    text = REND.read_text()
    text = re.sub(r'(\{13,\s*)\d+(\s*,\s*6\.0f\s*,\s*"Cymbal")', r'\g<1>80\g<2>', text)
    REND.write_text(text)
    compile_and_render()
    score_and_detail("Note=80")
    
finally:
    REND.write_text(orig_rend)
    compile_and_render()
    print("\n[Restored to note=65]")
