"""Grid search for Cymbal NzMx/NzFq to maximize flatness/flux."""
import sys, re, subprocess, json
from pathlib import Path
from batch_tune_runner import (
    parse_presets, find_render_for_preset, class_weighted_score, MANUAL_SAMPLE_TO_PRESET
)
from pre_hw_analysis import compare_pair

RDIR = Path('/home/user/logue-sdk/platform/drumlogue/ripplerx')
RENDER_DIR = RDIR / 'rendered_tune'
SYNTH = RDIR / 'synth_engine.h'
SAMPLES = list((RDIR / 'samples').glob('*.wav'))
cymbal_samples = [s for s in SAMPLES if MANUAL_SAMPLE_TO_PRESET.get(s.name) == 'Cymbal']

def score_cymbal(render_dir):
    presets = parse_presets(SYNTH)
    preset = presets.get('Cymbal')
    wav = find_render_for_preset(render_dir, preset)
    if not wav or not wav.exists():
        return None, None
    scores = []
    details = []
    for s in cymbal_samples:
        comp = compare_pair(s, wav, auto_note_align=False)
        sc = class_weighted_score(comp['score'], 'Cymbal', comp['metrics'])
        m = comp['metrics']
        ren = comp['ren_features']
        details.append(f"    {s.name}: {sc:.2f} flat={ren['flatness']:.3f}({m['flatness_pct']:.0f}%) flux={ren['flux']:.2f}({m['flux_pct']:.0f}%)")
        scores.append(sc)
    mean = sum(scores) / len(scores)
    return mean, details

def modify_cymbal_preset(nzmx, nzfq, nzfl=None):
    """Modify Cymbal preset in synth_engine.h - NzMx(col19), NzFq(col22), NzFl(col21)."""
    text = SYNTH.read_text()
    # Find the Cymbal preset row (col0=13)
    # Pattern: {  13,  65,   0,   1, 800, 450, ...}
    def repl(m):
        inner = m.group(1)
        vals = [v.strip() for v in inner.split(',')]
        if len(vals) >= 23 and vals[0].strip() == '13':
            vals[19] = f'{nzmx:4d}'
            if nzfl is not None:
                vals[21] = f'{nzfl:4d}'
            vals[22] = f'{nzfq:4d}'
            return '{' + ', '.join(vals) + '}'
        return m.group(0)
    new_text = re.sub(r'\{([^{}]+)\}', repl, text)
    SYNTH.write_text(new_text)

def compile_and_render():
    r = subprocess.run(
        ['g++', '-std=c++17', '-O2', '-I.', '-Itest_stubs', '-I..', '-I../../common', '-I../common',
         '-DRUNTIME_COMMON_H_', 'render_presets.cpp', '-o', 'render_presets_tune'],
        capture_output=True, cwd=RDIR
    )
    if r.returncode != 0:
        return False
    r2 = subprocess.run(['./render_presets_tune', 'rendered_tune'], capture_output=True, cwd=RDIR)
    return r2.returncode == 0

# Save original
original_text = SYNTH.read_text()

# Read current Cymbal values
presets = parse_presets(SYNTH)
cymbal = presets['Cymbal']
print(f"Current Cymbal preset: {cymbal.values}")

print("\n=== Current baseline ===")
mean, details = score_cymbal(RENDER_DIR)
if mean:
    print(f"  Mean={mean:.2f}")
    for d in details: print(d)

best_score = mean or 9999
best_params = (40, 340, 2)  # NzMx, NzFq, NzFl

try:
    for nzmx in [60, 70, 80, 85, 90]:
        for nzfq in [80, 100, 150, 200, 340]:
            modify_cymbal_preset(nzmx, nzfq)
            if not compile_and_render():
                print(f"  NzMx={nzmx} NzFq={nzfq}: compile failed")
                continue
            mean, details = score_cymbal(RENDER_DIR)
            if mean is None:
                continue
            marker = " *** BEST ***" if mean < best_score else ""
            print(f"\nNzMx={nzmx} NzFq={nzfq}: Mean={mean:.2f}{marker}")
            for d in details: print(d)
            if mean < best_score:
                best_score = mean
                best_params = (nzmx, nzfq, 2)
finally:
    # Restore original
    SYNTH.write_text(original_text)
    print(f"\n\nBest: NzMx={best_params[0]} NzFq={best_params[1]} NzFl={best_params[2]} → score={best_score:.2f}")
