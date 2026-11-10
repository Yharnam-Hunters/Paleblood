#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tag every function of the executable with its likely source file and library.

usage: source_files.py --elf ELF [--root DIR] [--out FILE] [--stats]   regenerate the table
       source_files.py --check [--elf ELF] [--root DIR]                  exit 1 if it is stale

Inputs: the function extents in symbols/ghidra_functions.csv, the library rules in
game/source_libraries.csv (one regular expression per library, matched against a source path
with `/` separators; its `file` group, after the rule's prefix, is the file we record) and,
when regenerating, the executable (read only).

1. Direct evidence. Assertion messages name the source file they were compiled from. Every
   RIP-relative `lea` in a function whose target is a NUL-terminated source path (`.c`, `.cc`,
   `.cpp`, `.cxx`) names a candidate file for that function. Headers (`.h`, `.inl`) are ignored:
   their code is inlined into other files. A function with several candidates keeps the one the
   fewest functions reference (the others are mostly inlined allocator or lock helpers).
2. Contiguity check. The linker keeps one object file's functions together, so a run of direct
   tags for file B with file A's tags on both sides contradicts the layout: B's string came in by
   cross-file inlining. Such a run is dropped when it is no longer than the longer A run beside
   it.
3. Home cluster. A file's direct tags further apart than HOME_GAP form separate clusters; only
   the cluster with the most tags is kept (the rest are inlined copies far from the object
   file). Steps 2 and 3 repeat until nothing changes.
4. Ranges. Untagged functions between two direct tags of the same file get that file
   (evidence `range`); between two files of the same library they get the library only.
   Anything else stays untagged.

The table holds one row per run of consecutive functions (in ghidra_functions.csv order) that
share library, file and evidence: `address` (first function), `last` (last function's address),
`functions`, `bytes`, `library`, `file`, `evidence` (direct|range), `confidence` (high for
direct, medium for a file range, low for a library-only range). Only the normalized path inside
the library is recorded, never the build machine's drive or directories.

Regenerating also rewrites the coverage table between the `source-files` markers of
docs/FUNCTION_CATALOG.md; --check compares that block too.

--check without --elf re-derives the range rows from the committed direct rows and the function
extents (what CI can do); with --elf it regenerates everything and compares.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import io
import os
import posixpath
import re
import struct
import sys

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUNCTIONS = 'symbols/ghidra_functions.csv'
RULES = 'game/source_libraries.csv'
TABLE = 'symbols/source_files.csv'
DOC = 'docs/FUNCTION_CATALOG.md'
DOC_BLOCK = re.compile(r'(<!-- source-files:start -->\n)(.*?)(<!-- source-files:end -->)', re.S)
HEADER = ['address', 'last', 'functions', 'bytes', 'library', 'file', 'evidence', 'confidence']
BASE = 0x400000
PT_LOAD = 1
PF_X = 1
MAX_PATH = 512
HOME_GAP = 0x10000  # anchors of one file further apart than this are separate clusters
SOURCE_EXT = re.compile(r'\.(c|cc|cpp|cxx)$', re.I)
# lea r64, [rip + disp32]: REX.W (with or without REX.R), opcode 8d, ModRM mod=00 rm=101.
LEA_RIP = re.compile(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]')
PRINTABLE = re.compile(rb'[\x20-\x7e]+')


# ---- inputs ---------------------------------------------------------------------------------

def read_functions(path: str) -> list[tuple[int, int]]:
    with open(path, newline='') as f:
        return [(int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f)]


def read_rules(path: str) -> list[tuple[re.Pattern, str, str]]:
    with open(path, newline='') as f:
        return [(re.compile(r['pattern']), r['library'], r['prefix']) for r in csv.DictReader(f)]


def classify(path: str, rules) -> tuple[str, str] | None:
    """(library, file inside it) for a translation-unit source path, None for anything else."""
    path = posixpath.normpath(path.replace('\\', '/'))
    if not SOURCE_EXT.search(path):
        return None
    for pattern, library, prefix in rules:
        m = pattern.search(path)
        if m:
            return library, prefix + m.group('file')
    return None


class Image:
    """The executable's loadable segments, addressed like the repository (ELF vaddr + BASE)."""

    def __init__(self, path: str):
        with open(path, 'rb') as f:
            self.data = f.read()
        if self.data[:4] != b'\x7fELF':
            raise SystemExit(f'{path}: not an ELF file')
        phoff, = struct.unpack_from('<Q', self.data, 0x20)
        phnum, = struct.unpack_from('<H', self.data, 0x38)
        self.segments = []  # (address, file offset, file size, executable)
        for i in range(phnum):
            p_type, flags, off, vaddr, _, filesz = struct.unpack_from('<IIQQQQ', self.data, phoff + 56 * i)
            if p_type == PT_LOAD and filesz:
                self.segments.append((vaddr + BASE, off, filesz, bool(flags & PF_X)))

    def offset(self, address: int) -> int | None:
        for start, off, size, _ in self.segments:
            if start <= address < start + size:
                return off + address - start
        return None

    def c_string(self, address: int) -> str | None:
        off = self.offset(address)
        if off is None:
            return None
        end = self.data.find(b'\0', off, off + MAX_PATH)
        if end < 0:
            return None
        m = PRINTABLE.fullmatch(self.data, off, end)
        return m.group().decode() if m else None


def direct_candidates(image: Image, functions, rules) -> tuple[dict, collections.Counter]:
    """{function address: {(library, file), ...}} from the source paths each function loads,
    and the unclassified source paths that were referenced (with how many functions)."""
    found: dict[int, set] = {}
    unknown: collections.Counter = collections.Counter()
    cache: dict[int, tuple | None | str] = {}
    for address, size in functions:
        off = image.offset(address)
        if off is None:
            continue
        body = image.data[off:off + size]
        names = set()
        for m in LEA_RIP.finditer(body):
            disp_at = m.start() + 3
            if disp_at + 4 > len(body):
                continue
            disp, = struct.unpack_from('<i', body, disp_at)
            target = address + disp_at + 4 + disp
            if target not in cache:
                text = image.c_string(target)
                tag = classify(text, rules) if text and ('/' in text or '\\' in text) else None
                cache[target] = tag if tag or not (text and SOURCE_EXT.search(text)) else text
            tag = cache[target]
            if isinstance(tag, tuple):
                names.add(tag)
            elif isinstance(tag, str):
                unknown[tag] += 1
        if names:
            found[address] = names
    return found, unknown


# ---- the layout model (no executable needed) ----------------------------------------------

def choose_files(candidates: dict) -> dict:
    """One (library, file) per function: the candidate the fewest functions reference."""
    uses = collections.Counter(tag for tags in candidates.values() for tag in tags)
    return {a: min(tags, key=lambda t: (uses[t], t)) for a, tags in candidates.items()}


def drop_inlined(tags: dict) -> tuple[dict, list[int]]:
    """Remove runs of direct tags that sit inside another file's run (see step 2)."""
    kept = dict(tags)
    dropped: list[int] = []
    while True:
        order = sorted(kept)
        runs = []  # [first index, last index, tag]
        for i, a in enumerate(order):
            if runs and runs[-1][2] == kept[a]:
                runs[-1][1] = i
            else:
                runs.append([i, i, kept[a]])
        sandwiched = []  # (length, run index)
        for n in range(1, len(runs) - 1):
            left, mid, right = runs[n - 1], runs[n], runs[n + 1]
            length = mid[1] - mid[0] + 1
            if left[2] == right[2] and length <= max(left[1] - left[0] + 1, right[1] - right[0] + 1):
                sandwiched.append((length, n))
        # Shortest first, and never two neighbouring runs in one pass: in A B A B A only the
        # B runs go, and the A between them survives to join its neighbours.
        taken: set[int] = set()
        remove = []
        for _, n in sorted(sandwiched):
            if n - 1 in taken or n + 1 in taken:
                continue
            taken.add(n)
            remove.extend(order[runs[n][0]:runs[n][1] + 1])
        if not remove:
            return kept, sorted(dropped)
        for a in set(remove):
            del kept[a]
            dropped.append(a)


def keep_home(tags: dict) -> tuple[dict, list[int]]:
    """Keep each file's direct tags only in its home cluster (see step 3)."""
    clusters: dict = collections.defaultdict(list)  # tag -> [[address, ...], ...]
    for a in sorted(tags):
        runs = clusters[tags[a]]
        if runs and a - runs[-1][-1] <= HOME_GAP:
            runs[-1].append(a)
        else:
            runs.append([a])
    kept, dropped = {}, []
    for tag, runs in clusters.items():
        home = max(runs, key=len)  # the first of equally large clusters
        for run in runs:
            for a in run:
                if run is home:
                    kept[a] = tag
                else:
                    dropped.append(a)
    return kept, sorted(dropped)


def anchors(candidates: dict) -> tuple[dict, list[int]]:
    """The direct tags that survive steps 1 to 3, and the functions whose tags were dropped."""
    tags = choose_files(candidates)
    dropped: list[int] = []
    while True:
        tags, inlined = drop_inlined(tags)
        tags, far = keep_home(tags)
        if not inlined and not far:
            return tags, sorted(dropped)
        dropped += inlined + far


def propagate(functions, direct: dict) -> list[tuple[str, str, str] | None]:
    """Per function (in order): (library, file, evidence) or None. `direct` maps a function
    address to its (library, file)."""
    out: list[tuple[str, str, str] | None] = [None] * len(functions)
    anchors = [i for i, (a, _) in enumerate(functions) if a in direct]
    for i in anchors:
        out[i] = (*direct[functions[i][0]], 'direct')
    for i, j in zip(anchors, anchors[1:]):
        if j == i + 1:
            continue
        lib_i, file_i = direct[functions[i][0]]
        lib_j, file_j = direct[functions[j][0]]
        if lib_i != lib_j:
            continue
        fill = (lib_i, file_i if file_i == file_j else '', 'range')
        for k in range(i + 1, j):
            out[k] = fill
    return out


def confidence(evidence: str, file: str) -> str:
    if evidence == 'direct':
        return 'high'
    return 'medium' if file else 'low'


def to_rows(functions, tags) -> list[dict]:
    rows: list[dict] = []
    previous = None
    for (address, size), tag in zip(functions, tags):
        if tag is None:
            previous = None
            continue
        if tag == previous:
            row = rows[-1]
            row['last'] = f'0x{address:08x}'
            row['functions'] += 1
            row['bytes'] += size
        else:
            library, file, evidence = tag
            rows.append({'address': f'0x{address:08x}', 'last': f'0x{address:08x}', 'functions': 1,
                         'bytes': size, 'library': library, 'file': file, 'evidence': evidence,
                         'confidence': confidence(evidence, file)})
        previous = tag
    return rows


def render(rows) -> str:
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=HEADER, lineterminator='\n')
    w.writeheader()
    w.writerows(rows)
    return buf.getvalue()


def direct_from_table(functions, rows) -> dict:
    """The direct tags a committed table holds, expanded to function addresses."""
    index = {a: i for i, (a, _) in enumerate(functions)}
    direct = {}
    for r in rows:
        if r['evidence'] != 'direct':
            continue
        first, last = index.get(int(r['address'], 16)), index.get(int(r['last'], 16))
        if first is None or last is None or last < first:
            raise ValueError(f"{TABLE}: direct run {r['address']}..{r['last']} is not a run of {FUNCTIONS}")
        for i in range(first, last + 1):
            direct[functions[i][0]] = (r['library'], r['file'])
    return direct


def summary(functions, rows) -> str:
    total_f = len(functions)
    total_b = sum(s for _, s in functions)
    per: dict = collections.defaultdict(lambda: collections.Counter())
    for r in rows:
        c = per[r['library']]
        key = r['evidence'] if r['file'] or r['evidence'] == 'direct' else 'library'
        c[f'{key}_f'] += int(r['functions'])
        c[f'{key}_b'] += int(r['bytes'])
    lines = ['| Library | Functions | Bytes | Direct | File range | Library range |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    tagged_f = tagged_b = 0
    for lib in sorted(per, key=lambda k: -sum(v for n, v in per[k].items() if n.endswith('_b'))):
        c = per[lib]
        f = c['direct_f'] + c['range_f'] + c['library_f']
        b = c['direct_b'] + c['range_b'] + c['library_b']
        tagged_f += f
        tagged_b += b
        lines.append(f"| {lib} | {f:,} | {b:,} | {c['direct_f']:,} | {c['range_f']:,} | {c['library_f']:,} |")
    lines.append(f'| untagged | {total_f - tagged_f:,} | {total_b - tagged_b:,} | | | |')
    lines.append(f'| all | {total_f:,} | {total_b:,} | | | |')
    return '\n'.join(lines)


# ---- entry point ----------------------------------------------------------------------------

def generate(root: str, elf: str, stats: bool = False) -> str:
    functions = read_functions(os.path.join(root, FUNCTIONS))
    image = Image(elf)
    candidates, unknown = direct_candidates(image, functions, read_rules(os.path.join(root, RULES)))
    chosen = choose_files(candidates)
    kept, dropped = anchors(candidates)
    rows = to_rows(functions, propagate(functions, kept))
    if stats:
        several = sum(1 for tags in candidates.values() if len(tags) > 1)
        files = len(set(chosen.values()))
        print(f'functions loading a source path: {len(candidates)} ({several} with several files, '
              f'{files} distinct files); direct tags dropped as inlined or far from home: {len(dropped)}', file=sys.stderr)
        for text, n in unknown.most_common():
            print(f'no library rule: {n} reference(s) to {text}', file=sys.stderr)
        print(summary(functions, rows), file=sys.stderr)
    return render(rows)


def update_doc(root: str, table: str, write: bool) -> list[str]:
    """Put the coverage summary of `table` into the docs block; report (or fix) a stale one."""
    path = os.path.join(root, DOC)
    if not os.path.isfile(path):
        return []
    with open(path) as f:
        text = f.read()
    m = DOC_BLOCK.search(text)
    if not m:
        return [f'{DOC} has no <!-- source-files:start --> block']
    functions = read_functions(os.path.join(root, FUNCTIONS))
    block = summary(functions, list(csv.DictReader(io.StringIO(table)))) + '\n'
    if m.group(2) == block:
        return []
    if not write:
        return [f'{DOC}: the coverage block is stale: run tools/source_files.py --elf ELF']
    with open(path, 'w') as f:
        f.write(text[:m.start(2)] + block + text[m.end(2):])
    return []


def check(root: str, elf: str | None) -> list[str]:
    path = os.path.join(root, TABLE)
    if not os.path.isfile(path):
        return [f'{TABLE} is missing: run tools/source_files.py --elf ELF']
    with open(path, newline='') as f:
        text = f.read()
    rows = list(csv.DictReader(io.StringIO(text)))
    if text.split('\n', 1)[0] != ','.join(HEADER):
        return [f'{TABLE}: header must be {",".join(HEADER)}']
    if elf:
        expected = generate(root, elf)
    else:
        functions = read_functions(os.path.join(root, FUNCTIONS))
        try:
            direct = direct_from_table(functions, rows)
        except ValueError as e:
            return [str(e)]
        expected = render(to_rows(functions, propagate(functions, direct)))
    if text != expected:
        return [f'{TABLE} is stale: run tools/source_files.py --elf ELF (with the target executable)']
    return update_doc(root, text, write=False)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=DEFAULT_ROOT)
    ap.add_argument('--elf', help='the target executable (read only)')
    ap.add_argument('--out', help=f'write here instead of {TABLE}')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--stats', action='store_true', help='print coverage per library on stderr')
    a = ap.parse_args()
    if a.check:
        errs = check(a.root, a.elf)
        for e in errs:
            print(f'source-files: {e}', file=sys.stderr)
        return 1 if errs else 0
    if not a.elf:
        ap.error('--elf is required to regenerate the table')
    text = generate(a.root, a.elf, a.stats)
    with open(a.out or os.path.join(a.root, TABLE), 'w', newline='') as f:
        f.write(text)
    if not a.out:
        update_doc(a.root, text, write=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
