"""Detailed comparison of NzMx=40 vs NzMx=60 for Cymbal."""
import sys, re, subprocess
from pathlib import Path
from batch_tune_runner import parse_presets, find_render_for_preset, class_weighted_score, MANUAL_SAMPLE_TO_PRESET
from pre_hw_analysis import compare_pair

RDIR = Path('.')
RENDER_DIR = RDIR / 'rendered_tune'
SYNTH = RDIR / 'synth_engine.h'
SAMPLES = list((RDIR / 'samples').glob('*.wav'))
cymbal_samples = [s for s in SAMPLES if MANUAL_SAMPLE_TO_PRESET.get(s.name) == 'Cymbal']

def modify_and_render(nzmx):
    text = SYNTH.read_text()
    def repl(m):
        inner = m.group(1)
        vals = [v.strip() for v in inner.split(',')]
        if len(vals) >= 23 and vals[0].strip() == '13':
            vals[19] = f'{nzmx:4d}'
            return '{' + ', '.join(vals) + '}'
        return m.group(0)
    SYNTH.write_text(re.sub(r'\{([^{}]+)\}', repl, text))
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
    print(f"\n=== {label} ===")
    for s in cymbal_samples:
        comp = compare_pair(s, wav, auto_note_align=False)
        m = comp['metrics']
        rf = comp['ref_features']
        ren = comp['ren_features']
        sc = class_weighted_score(comp['score'], 'Cymbal', m)
        ct = min(m['centroid_pct'], m['centroid_pitchnorm_pct'])
        print(f"  {s.name}: score={sc:.2f}")
        print(f"    f0={ren['f0_hz']:.0f}Hz({m['f0_pct']:.1f}%) attack={ren['attack_ms']:.1f}ms({m['attack_pct']:.1f}%)")
        print(f"    t60={ren['t60_ms']:.0f}ms({m['t60_pct']:.1f}%) centroid={ren['centroid_hz']:.0f}Hz({ct:.1f}%)")
        print(f"    flat={ren['flatness']:.3f}({m['flatness_pct']:.1f}%) flux={ren['flux']:.2f}({m['flux_pct']:.1f}%)")
        print(f"    inharm={ren['inharm']:.4f}({m['inharm_pct']:.1f}%) mrstft={m['mrstft_log_l1']:.4f}")

original = SYNTH.read_text()
try:
    for nzmx in [40, 50, 60]:
        if not modify_and_render(nzmx):
            break
        score_and_detail(f"NzMx={nzmx}")
finally:
    SYNTH.write_text(original)
    # Re-render original state  
    subprocess.run(['g++', '-std=c++17', '-O2', '-I.', '-Itest_stubs', '-I..', '-I../../common', '-I../common',
         '-DRUNTIME_COMMON_H_', 'render_presets.cpp', '-o', 'render_presets_tune'],
        capture_output=True, cwd=RDIR)
    subprocess.run(['./render_presets_tune', 'rendered_tune'], capture_output=True, cwd=RDIR)
    print("\n[Restored original and re-rendered]")
