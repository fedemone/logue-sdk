#!/usr/bin/env python3
"""Check whether Step 2/3 tuning run has required sample mappings.

Prints:
- mapped sample -> preset pairs for target sets
- unmapped files that likely need manual mapping or curated replacements
"""
from pathlib import Path
from batch_tune_runner import parse_presets, map_sample_to_preset, SYNTH_ENGINE

# Note: this should have names or aliases (MANUAL_SAMPLE_TO_PRESET)? Otherwise match is not possible
TARGET = {
   "AcSnre", "Kick", "HHat-C", "HHat-O", "Timpni",
    "Ac Tom","Flute", "Clrint", "Tick", "Clap", "Kalimba",
}


def main() -> int:
    root = Path(__file__).resolve().parent
    presets = parse_presets(SYNTH_ENGINE)
    sample_dir = root / "samples"
    samples = sorted([p for p in sample_dir.iterdir() if p.suffix.lower() in [".wav", ".mp3"]])

    mapped = []
    unmapped = []
    target_covered = set()

    for i, s in enumerate(samples):
        p = map_sample_to_preset(s, presets)
        if p is None:
            unmapped.append(s.name)
            continue
        if p in TARGET:
            mapped.append((s.name, p))
            target_covered.add(p)

    missing_targets = sorted(TARGET - target_covered)

    print("# Phase 2/3 readiness report")
    print(f"samples_scanned={len(samples)}")
    print(f"target_mapped={len(mapped)}")
    print(f"target_missing={len(missing_targets)}")
    print()

    if mapped:
        print("## Target mappings")
        for s, p in mapped:
            print(f"- {s} -> {p}")
        print()

    if missing_targets:
        print("## Missing target presets (need curated sample or explicit mapping)")
        for p in missing_targets:
            print(f"- {p}")
        print()

    if unmapped:
        print("## Unmapped sample files")
        for n in unmapped[:40]:
            print(f"- {n}")
        if len(unmapped) > 40:
            print(f"- ... (+{len(unmapped)-40} more)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
