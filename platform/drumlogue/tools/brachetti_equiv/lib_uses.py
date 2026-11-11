#!/usr/bin/env python3
"""lib_uses.py — which brachetti_common helpers each Brachetti unit uses.

    python3 lib_uses.py            per unit file: header → the names it uses
    python3 lib_uses.py --index    per helper: the units that use it
    python3 lib_uses.py --check    exit 1 if a unit file's "From
                                   brachetti_common" block is out of date
    python3 lib_uses.py --write    rewrite those blocks (and their #includes)

Every unit file (presets.h, voice.h, synth_engine.h, modal_drum_kernel.h)
opens with a block naming each library header it includes and the names it
uses from it:

    // From brachetti_common (its README.md indexes every helper):
    //   dsp_math.h       clampf, knob_exp2
    //   knobs.h          knob::decay, off_anchor
    #include "../brachetti_common/dsp_math.h"
    #include "../brachetti_common/knobs.h"

so a reader sees what already exists before writing a helper of their own.
Run --write after using a new library name (or dropping one), and --check
before a commit.  The scan is textual: a name counts as used when the file
writes it as brc::name (or knob::name under `namespace knob = brc::knob`), or
names one of the few unqualified library types (FastSVF, FastNoise, the
float_math.h functions).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DRUM = os.path.normpath(os.path.join(HERE, '..', '..'))
LIB = os.path.join(DRUM, 'brachetti_common')
UNITS = ['kick', 'tom', 'kernel', 'snare', 'bar', 'plate', 'cymbal', 'noise', 'world']
FILES = ['presets.h', 'voice.h', 'synth_engine.h', 'modal_drum_kernel.h']
BLOCK_HEAD = '// From brachetti_common (its README.md indexes every helper):'
UNQUALIFIED = {'FastSVF', 'FastNoise', 'FastEnvelope'}


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


def library_exports():
    """name → header for every name a library header declares at its top level."""
    exports = {}
    for f in sorted(os.listdir(LIB)):
        if not f.endswith('.h'):
            continue
        s = strip_comments(open(os.path.join(LIB, f)).read())
        found = []
        if f == 'float_math.h':
            found = re.findall(r'\b(fast(?:er)?\w+)\s*\(', s)
        else:
            m = re.search(r'namespace knob \{(.*?)\n\}', s, re.S)
            if m:
                found += ['knob::' + n for n in re.findall(r'inline\s+\w+\s+(\w+)\s*\(', m.group(1))]
                s = s.replace(m.group(0), '')
            found += re.findall(r'^(?:template\s*<[^>]*>\s*)?(?:inline\s+|static\s+|constexpr\s+)*'
                                r'(?:const\s+)?[\w:<>]+[\s*&]+(\w+)\s*\(', s, re.M)
            found += re.findall(r'^(?:struct|class)\s+(\w+)', s, re.M)
            found += re.findall(r'^(?:static\s+)?(?:constexpr|const)\s+[\w:]+\s+(k\w+)', s, re.M)
            for blk in re.findall(r'^enum\s+\w*\s*(?::\s*\w+\s*)?\{(.*?)\}', s, re.M | re.S):
                found += re.findall(r'\b(k_?\w+|ENV_\w+)\b', blk)
        for n in found:
            exports.setdefault(n, f)
    return exports


def uses_of(path, exports):
    """header → sorted names the file uses."""
    s = strip_comments(open(path).read())
    used = {}
    for q in re.findall(r'brc::((?:knob::)?\w+)', s):
        if q in exports:
            used.setdefault(exports[q], set()).add(q)
    if re.search(r'namespace knob = brc::knob;', s):
        for q in re.findall(r'(?<![\w:])knob::(\w+)', s):
            if 'knob::' + q in exports:
                used.setdefault(exports['knob::' + q], set()).add('knob::' + q)
    for n, h in exports.items():
        if '::' in n:
            continue
        if h == 'float_math.h' or n in UNQUALIFIED:
            if re.search(r'(?<![\w:])' + re.escape(n) + r'\b', s):
                used.setdefault(h, set()).add(n)
    return {h: sorted(v) for h, v in sorted(used.items())}


def unit_files():
    for u in UNITS:
        for f in FILES:
            p = os.path.join(DRUM, 'brachetti_' + u, f)
            if os.path.exists(p):
                yield u, p


def block_for(uses):
    out = [BLOCK_HEAD]
    for h, names in uses.items():
        line, rows = '', []
        for n in names:
            add = n if not line else line + ', ' + n
            if len(add) > 56 and line:
                rows.append(line + ',')
                line = n
            else:
                line = add
        rows.append(line)
        out.append('//   %-17s%s' % (h, rows[0]))
        out += ['//   %-17s%s' % ('', r) for r in rows[1:]]
    out += ['#include "../brachetti_common/%s"' % h for h in uses]
    return out


def rewrite(path, uses):
    """The file's text with its block (and library #includes) regenerated."""
    lines = open(path).read().split('\n')
    if BLOCK_HEAD in lines:
        at = lines.index(BLOCK_HEAD)
        end = at + 1
        while end < len(lines) and (lines[end].startswith('//   ') or
                                    lines[end].startswith('#include "../brachetti_common/')):
            end += 1
        del lines[at:end]
    else:
        incs = [i for i, l in enumerate(lines) if l.startswith('#include "../brachetti_common/')]
        at = incs[0] if incs else max(i for i, l in enumerate(lines) if l.startswith('#include')) + 1
        for i in reversed(incs):
            del lines[i]
        if at > 0 and lines[at - 1].strip():
            lines.insert(at, '')
            at += 1
    lines[at:at] = block_for(uses)
    return '\n'.join(lines)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ''
    exports = library_exports()
    stale = []
    users = {}
    for u, p in unit_files():
        uses = uses_of(p, exports)
        rel = os.path.relpath(p, DRUM)
        for h, names in uses.items():
            for n in names:
                users.setdefault((h, n), set()).add(u)
        if mode in ('--check', '--write'):
            new = rewrite(p, uses)
            if new != open(p).read():
                stale.append(rel)
                if mode == '--write':
                    open(p, 'w').write(new)
        elif mode == '':
            print(rel)
            for h, names in uses.items():
                print('   %-16s %s' % (h, ', '.join(names)))
    if mode == '--index':
        for (h, n) in sorted(users):
            print('%-16s %-24s %s' % (h, n, ', '.join(u for u in UNITS if u in users[(h, n)])))
        unused = sorted((h, n) for n, h in exports.items() if (h, n) not in users)
        print('\nnot used directly by any unit file (library-internal or via ModalSynth):')
        for h, n in unused:
            print('%-16s %s' % (h, n))
    if mode == '--check':
        for rel in stale:
            print('out of date:', rel)
        sys.exit(1 if stale else 0)
    if mode == '--write':
        for rel in stale:
            print('rewrote:', rel)


if __name__ == '__main__':
    main()
