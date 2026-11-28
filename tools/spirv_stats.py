#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the shader translator emits: per-shader SPIR-V figures, ranked by how often the game uses them.

Reads a directory of translated shaders (.spv files, named STAGE-ADDRESS[-...].spv: the executor's
BB_SHADER_SWEEP directory, or any dump named the same way) and gives, for each shader:

  words         the module's size in 32-bit words
  insts         instructions inside its functions
  reg_ld/st     loads and stores of Function variables: the translator keeps the GCN registers in
                these, and a compiler's register promotion (spirv-opt's mem2reg, a driver's) removes them
  mem_ld/st     loads and stores through physical-storage-buffer pointers: guest memory reads and writes
  push_ld       loads of push constants (the draw's user data and table snapshots)
  branches      conditional branches and switches
  selects       OpSelect
  image         image instructions (sample, fetch, read, write, query)
  valid         whether spirv-val accepts it

With --opt, each module also goes through `spirv-opt -O`, and the same figures are given for its
output: what an optimiser removes is what the emitted code spends for nothing (a driver's compiler
removes much of it too; what the optimiser keeps, a driver most likely keeps as well).

With --counts FILE (lines "STAGE-LETTER ADDRESS CALLS UNITS PIXELS", hexadecimal address; v, p, c
for vertex, pixel and compute shaders), shaders are ranked by calls x instructions (after the
optimiser with --opt), the hottest first.

The shaders are translations of the game's own code and stay outside the repository; this tool
only reads them. SPIRV-Tools (spirv-dis, spirv-val, spirv-opt) must be on PATH.

Usage: tools/spirv_stats.py DIR [--opt] [--counts FILE] [--top N] [--json]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

FIGURES = ('words', 'insts', 'reg_ld', 'reg_st', 'mem_ld', 'mem_st', 'push_ld', 'branches', 'selects', 'image')
STAGE_LETTERS = {'vertex': 'v', 'pixel': 'p', 'compute': 'c'}

# "%12 = OpTypePointer Function %5", "OpStore %30 %31", "%7 = OpLoad %uint %30 Aligned 4"
LINE = re.compile(r'^\s*(?:(%\S+)\s*=\s*)?(Op\w+)(.*)$')


def disassembly_figures(text):
    """The figures (FIGURES without words) of one module from its `spirv-dis --raw-id` text."""
    pointer_class = {}  # pointer type id -> storage class
    pointer_of = {}     # value id -> storage class of the pointer it is
    figures = dict.fromkeys(FIGURES[1:], 0)
    in_function = False
    for line in text.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        result, op, rest = m.group(1), m.group(2), m.group(3).split()
        if op == 'OpTypePointer' and result:
            pointer_class[result] = rest[0]
        elif op == 'OpTypeForwardPointer':
            pointer_class[rest[0]] = rest[1]
        elif op == 'OpVariable' and result:
            pointer_of[result] = pointer_class.get(rest[0], rest[1] if len(rest) > 1 else '?')
        elif op in ('OpAccessChain', 'OpInBoundsAccessChain', 'OpPtrAccessChain', 'OpConvertUToPtr', 'OpBitcast',
                    'OpCopyObject') and result and rest and rest[0] in pointer_class:
            pointer_of[result] = pointer_class[rest[0]]
        if op == 'OpFunction':
            in_function = True
            continue
        if op == 'OpFunctionEnd':
            in_function = False
            continue
        if not in_function or op == 'OpLabel':
            continue
        figures['insts'] += 1
        if op == 'OpLoad':
            storage = pointer_of.get(rest[1]) if len(rest) > 1 else None
            if storage == 'Function':
                figures['reg_ld'] += 1
            elif storage == 'PhysicalStorageBuffer':
                figures['mem_ld'] += 1
            elif storage == 'PushConstant':
                figures['push_ld'] += 1
        elif op == 'OpStore':
            storage = pointer_of.get(rest[0]) if rest else None
            if storage == 'Function':
                figures['reg_st'] += 1
            elif storage == 'PhysicalStorageBuffer':
                figures['mem_st'] += 1
        elif op in ('OpBranchConditional', 'OpSwitch'):
            figures['branches'] += 1
        elif op == 'OpSelect':
            figures['selects'] += 1
        elif op.startswith('OpImage'):
            figures['image'] += 1
    return figures


def module_figures(path):
    """The figures of one .spv file, and whether spirv-val accepts it."""
    text = subprocess.run(['spirv-dis', '--raw-id', path], capture_output=True, text=True, check=True).stdout
    figures = disassembly_figures(text)
    figures['words'] = os.path.getsize(path) // 4
    figures['valid'] = subprocess.run(['spirv-val', path], capture_output=True).returncode == 0
    return figures


def optimised_figures(path):
    """The figures of the module after `spirv-opt -O`, or None when the optimiser refuses it."""
    with tempfile.TemporaryDirectory() as scratch:
        out = os.path.join(scratch, 'opt.spv')
        if subprocess.run(['spirv-opt', '-O', path, '-o', out], capture_output=True).returncode:
            return None
        return module_figures(out)


def shader_key(name):
    """(stage letter, address) from a file name STAGE-ADDRESS[-...].spv, or None."""
    parts = os.path.basename(name)[:-len('.spv')].split('-')
    if len(parts) < 2 or parts[0] not in STAGE_LETTERS:
        return None
    try:
        return STAGE_LETTERS[parts[0]], int(parts[1], 16)
    except ValueError:
        return None


def read_counts(path):
    """{(stage letter, address): calls} from a counts file."""
    counts = {}
    with open(path) as f:
        for line in f:
            fields = line.split()
            if len(fields) >= 3:
                key = (fields[0], int(fields[1], 16))
                counts[key] = counts.get(key, 0) + int(fields[2])
    return counts


def survey(directory, optimise=False, counts=None):
    """One record per .spv file in the directory: its name, figures, optimised figures and calls."""
    records = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith('.spv'):
            continue
        path = os.path.join(directory, name)
        record = {'name': name, 'figures': module_figures(path)}
        if optimise:
            record['optimised'] = optimised_figures(path)
        key = shader_key(name)
        record['calls'] = counts.get(key, 0) if counts is not None and key else None
        records.append(record)
    return records


def cost(record):
    figures = record.get('optimised') or record['figures']
    return figures['insts']


def totals(records, field):
    out = dict.fromkeys(FIGURES, 0)
    for r in records:
        figures = r.get(field)
        if figures:
            for k in FIGURES:
                out[k] += figures[k]
    return out


def report(records, top):
    ranked = records
    if any(r['calls'] is not None for r in records):
        ranked = sorted(records, key=lambda r: (r['calls'] or 0) * cost(r), reverse=True)
    header = f"{'shader':40} {'calls':>9} " + ' '.join(f'{k:>8}' for k in FIGURES) + '  opt-insts valid'
    print(header)
    for r in ranked[:top]:
        f, o = r['figures'], r.get('optimised')
        calls = '' if r['calls'] is None else str(r['calls'])
        print(f"{r['name'][:40]:40} {calls:>9} " + ' '.join(f'{f[k]:>8}' for k in FIGURES) +
              f"  {o['insts'] if o else '-':>9} {'yes' if f['valid'] else 'NO'}")
    whole = totals(records, 'figures')
    print(f"\n{len(records)} shaders, {sum(1 for r in records if not r['figures']['valid'])} rejected by spirv-val")
    print('emitted:   ' + ' '.join(f'{k} {whole[k]}' for k in FIGURES))
    if any(r.get('optimised') for r in records):
        opt = totals(records, 'optimised')
        print('optimised: ' + ' '.join(f'{k} {opt[k]}' for k in FIGURES))
        hot = [r for r in records if r.get('optimised') and r['calls']]
        if hot:
            weighted = sum(r['calls'] * r['figures']['insts'] for r in hot)
            weighted_opt = sum(r['calls'] * r['optimised']['insts'] for r in hot)
            print(f'call-weighted instructions: emitted {weighted}, optimised {weighted_opt} '
                  f'({100 * (1 - weighted_opt / weighted):.1f}% removed)')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('directory')
    parser.add_argument('--opt', action='store_true', help='also measure each module after spirv-opt -O')
    parser.add_argument('--counts', help='per-shader call counts, to rank by calls x instructions')
    parser.add_argument('--top', type=int, default=30)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    counts = read_counts(args.counts) if args.counts else None
    records = survey(args.directory, args.opt, counts)
    if args.json:
        json.dump(records, sys.stdout, indent=1)
        print()
    else:
        report(records, args.top)
    return 0


if __name__ == '__main__':
    sys.exit(main())
