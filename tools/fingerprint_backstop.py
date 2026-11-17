#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The compiled-code backstop of source fingerprints: a fingerprint row stays current only where
the compiled code agrees.

usage: fingerprint_backstop.py [--base REV] [--root DIR] [--jobs N]

A fingerprint row (symbols/verification.csv and symbols/families/*.verification.csv, source
`f1:<fingerprint>:<legacy hash>`) carries the legacy hash of the code it was recorded on. Where that
legacy hash still equals the legacy hash now, the row is current by the old whole-file rule and
nothing more is needed. Where it does not, the old rule would call the row stale and only the
fingerprint (tools/fingerprint.py) says the change cannot reach the function. This backstop does not
take the fingerprint's word for it: it compiles the function's defining files on the code the row
stands for and on the code now, with the game library's compiler and flags, and compares what the
function's code can reach in each object:

  every section of the replacement bb_NAME (and its compiler clones, bb_NAME.part.0 and so on),
  every section a relocation in a reached section points to (functions it calls or takes the
  address of, jump tables, constants, string pools, variables and their initial values, vtables),
  the static initializers (.init_array), and the names of the symbols it reaches outside the object.

Bytes, sizes, flags and relocations (by symbol name outside the object, by place in this walk inside
it) must be identical. Any difference, a file that does not compile, or an object without the
replacement makes the row stale, with a message. The code the row stands for is found as follows:

  - a row the base revision already has, with the same value, was accepted there (by this backstop,
    in the pull request or merge that brought it): it is compared with the base's code, where the
    base's legacy hash differs from the one now;
  - a row this branch adds or changes is compared with the code of the revision (the base, or a
    commit of the branch) whose legacy hash is the row's; where no revision has it, the row is
    stale: it was recorded on code that was never committed.

So a fingerprint row is current only where the old rule calls it current or the compiled code of
everything the function reaches is unchanged since the code it was recorded on: never weaker than
the old rule for a change of behaviour (docs/VERIFY.md, "Source fingerprints"). CI and the pre-push
gate run it; --base defaults to where HEAD leaves origin/main.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import io
import os
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fingerprint  # noqa: E402
import verification  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The game library: every .c and .cpp directly under game/<system>/ (game/CMakeLists.txt).
LIBRARY_SOURCE = re.compile(r'game/[^/]+/[^/]+\.(c|cpp)')
# game/CMakeLists.txt's flags for bbgame (Release), the CMake default build type, and options that
# make the object comparable: a section per function and variable, no debug or unwind data.
GAME_FLAGS = ['-O3', '-DNDEBUG', '-fPIC', '-ffp-contract=off', '-fno-fast-math']
COMPARE_FLAGS = ['-ffunction-sections', '-fdata-sections', '-fno-asynchronous-unwind-tables', '-g0', '-w']
SEARCH_LIMIT = 300          # commits of the branch searched for a row's recorded code
STARTUP_SECTIONS = ('.init_array', '.preinit_array', '.ctors')


# ---- reading an ELF64 relocatable object --------------------------------------------------------

SHT_SYMTAB, SHT_RELA, SHT_NOBITS, SHT_SYMTAB_SHNDX = 2, 4, 8, 18
SHN_UNDEF, SHN_ABS, SHN_COMMON, SHN_XINDEX = 0, 0xfff1, 0xfff2, 0xffff
STT_SECTION, STT_FUNC = 3, 2
# flags that change what the code does with a section: writable, allocated, executable, merged,
# strings, TLS
FLAG_MASK = 0x1 | 0x2 | 0x4 | 0x10 | 0x20 | 0x400


class Section:
    def __init__(self, index, name, kind, flags, data, size, link, info, entsize):
        self.index, self.name, self.kind, self.flags = index, name, kind, flags
        self.data, self.size, self.link, self.info, self.entsize = data, size, link, info, entsize
        self.relocations: list[tuple] = []


class Symbol:
    def __init__(self, name, info, shndx, value, size):
        self.name, self.binding, self.type = name, info >> 4, info & 0xf
        self.shndx, self.value, self.size = shndx, value, size


class ObjectFile:
    def __init__(self, data: bytes):
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            raise ValueError('not a little-endian ELF64 object')
        shoff, = struct.unpack_from('<Q', data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
        headers = []
        first = struct.unpack_from('<IIQQQQIIQQ', data, shoff)
        if shnum == 0:
            shnum = first[5]
        if shstrndx == SHN_XINDEX:
            shstrndx = first[6]
        for i in range(shnum):
            headers.append(struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize))
        names = headers[shstrndx]
        strtab = data[names[4]:names[4] + names[5]]
        self.sections = []
        for i, (name, kind, flags, _, offset, size, link, info, _, entsize) in enumerate(headers):
            body = b'' if kind == SHT_NOBITS else data[offset:offset + size]
            self.sections.append(Section(i, cstring(strtab, name), kind, flags, body, size, link, info, entsize))
        self.symbols: list[Symbol] = []
        symtab = next((s for s in self.sections if s.kind == SHT_SYMTAB), None)
        if symtab is not None:
            names = self.sections[symtab.link].data
            extended = next((s for s in self.sections if s.kind == SHT_SYMTAB_SHNDX and s.link == symtab.index), None)
            for n in range(len(symtab.data) // 24):
                name, info, _, shndx, value, size = struct.unpack_from('<IBBHQQ', symtab.data, n * 24)
                if shndx == SHN_XINDEX and extended is not None:
                    shndx, = struct.unpack_from('<I', extended.data, n * 4)
                self.symbols.append(Symbol(cstring(names, name), info, shndx, value, size))
        for s in self.sections:
            if s.kind == SHT_RELA:
                target = self.sections[s.info]
                for n in range(len(s.data) // 24):
                    offset, info, addend = struct.unpack_from('<QQq', s.data, n * 24)
                    target.relocations.append((offset, info & 0xffffffff, info >> 32, addend))


def cstring(table: bytes, offset: int) -> str:
    end = table.find(b'\0', offset)
    return table[offset:end if end >= 0 else len(table)].decode('utf-8', 'replace')


def reached_code(obj: ObjectFile, is_root) -> bytes | None:
    """A canonical description of everything the replacement's code reaches in the object, or None
    where the object does not define it."""
    roots = sorted((s for s in obj.symbols
                    if s.type == STT_FUNC and s.shndx not in (SHN_UNDEF, SHN_ABS, SHN_COMMON)
                    and is_root(s.name.split('.', 1)[0])), key=lambda s: s.name)
    if not roots:
        return None
    order: dict[int, int] = {}
    queue: list[int] = []

    def visit(index: int) -> int:
        if index not in order:
            order[index] = len(order)
            queue.append(index)
        return order[index]

    out = io.BytesIO()
    for sym in roots:
        out.write(f'root {sym.name} {visit(sym.shndx)} {sym.value} {sym.size}\n'.encode())
    for s in obj.sections:
        if s.name in STARTUP_SECTIONS or s.name.startswith(tuple(n + '.' for n in STARTUP_SECTIONS)):
            out.write(f'startup {visit(s.index)}\n'.encode())
    done = 0
    while done < len(queue):
        s = obj.sections[queue[done]]
        done += 1
        out.write(f'section {order[s.index]} {s.kind} {s.flags & FLAG_MASK} {s.size} {s.entsize}\n'.encode())
        out.write(hashlib.sha256(s.data).digest())
        for offset, kind, index, addend in sorted(s.relocations):
            out.write(f'reloc {offset} {kind} {addend} {target(obj, index, visit)}\n'.encode())
    return out.getvalue()


def target(obj: ObjectFile, index: int, visit) -> str:
    sym = obj.symbols[index] if index < len(obj.symbols) else None
    if sym is None:
        return 'none'
    if sym.shndx == SHN_UNDEF:
        return f'external {sym.name}'
    if sym.shndx == SHN_ABS:
        return f'absolute {sym.value}'
    if sym.shndx == SHN_COMMON:
        return f'common {sym.name} {sym.size}'
    place = visit(sym.shndx)
    if sym.type == STT_SECTION:
        return f'section {place}'
    # a symbol other objects can name keeps its name; a local one is known by its place
    name = sym.name if sym.binding != 0 else ''
    return f'symbol {place} {sym.value} {sym.size} {name}'


# ---- compiling a tree ----------------------------------------------------------------------------

def compiler(cxx: bool) -> list[str]:
    return shlex.split(os.environ.get('CXX' if cxx else 'CC') or ('c++' if cxx else 'cc'))


def compile_object(tree: str, rel: str, out: str) -> str | None:
    """Compiles one game source of the tree to out; None on success, else the compiler's complaint."""
    cxx = rel.endswith('.cpp')
    cmd = compiler(cxx) + ['-c', '-std=gnu++20' if cxx else '-std=gnu11'] + GAME_FLAGS + COMPARE_FLAGS + [
        '-I', 'runtime/include', rel, '-o', out]
    env = dict(os.environ, SOURCE_DATE_EPOCH='0', LC_ALL='C')
    try:
        r = subprocess.run(cmd, cwd=tree, capture_output=True, text=True, env=env, timeout=600)
    except (OSError, subprocess.TimeoutExpired) as e:
        return str(e)
    if r.returncode != 0:
        return (r.stderr.strip().splitlines() or ['the compiler failed'])[-1]
    return None


class Objects:
    """Compiled game sources per tree, each compiled once."""

    def __init__(self, scratch: str, jobs: int):
        self.scratch = scratch
        self.jobs = jobs
        self.done: dict[tuple, tuple] = {}     # (tree, rel) -> (ObjectFile | None, error)

    def compile_all(self, pairs: set[tuple]) -> None:
        todo = sorted(p for p in pairs if p not in self.done)
        with concurrent.futures.ThreadPoolExecutor(max(1, self.jobs)) as pool:
            for pair, result in zip(todo, pool.map(self._compile, todo)):
                self.done[pair] = result

    def _compile(self, pair: tuple) -> tuple:
        tree, rel = pair
        out = os.path.join(self.scratch, hashlib.sha256(f'{tree}\0{rel}'.encode()).hexdigest()[:24] + '.o')
        error = compile_object(tree, rel, out)
        if error:
            return None, f'{rel} does not compile: {error}'
        try:
            with open(out, 'rb') as f:
                return ObjectFile(f.read()), ''
        except (OSError, ValueError, struct.error, IndexError) as e:
            return None, f'{rel}: cannot read the object: {e}'
        finally:
            if os.path.exists(out):
                os.unlink(out)

    def get(self, tree: str, rel: str) -> tuple:
        if (tree, rel) not in self.done:
            self.compile_all({(tree, rel)})
        return self.done[(tree, rel)]


def library_files(tree: str, name: str, family: bool) -> list[str]:
    return [rel for rel in verification.defining_files(tree, name, family) if LIBRARY_SOURCE.fullmatch(rel)]


def compiled_signature(objects: Objects, tree: str, name: str, family: bool) -> tuple[str | None, str]:
    """(digest of what bb_NAME reaches in each library object that defines it, '') or (None, why)."""
    is_root = fingerprint.root_matcher(name, family)
    parts = []
    for rel in library_files(tree, name, family):
        obj, error = objects.get(tree, rel)
        if obj is None:
            return None, error
        code = reached_code(obj, is_root)
        if code is not None:
            parts.append(hashlib.sha256(code).hexdigest())
    if not parts:
        return None, f'no game library source defines bb_{name}'
    return hashlib.sha256('\n'.join(sorted(parts)).encode()).hexdigest(), ''


# ---- revisions -------------------------------------------------------------------------------------

def git(root: str, *args: str) -> str:
    return subprocess.run(['git', '-C', root, *args], capture_output=True, text=True, check=True).stdout


def extract(root: str, rev: str, dest: str) -> None:
    """The game/ and runtime/include/ trees of rev, written under dest."""
    paths = [p for p in ('game', 'runtime/include')
             if subprocess.run(['git', '-C', root, 'cat-file', '-e', f'{rev}:{p}'], capture_output=True).returncode == 0]
    os.makedirs(dest)
    if not paths:
        return
    data = subprocess.run(['git', '-C', root, 'archive', '--format=tar', rev, '--', *paths],
                          capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(dest, filter='data')


def show(root: str, rev: str, rel: str) -> str | None:
    r = subprocess.run(['git', '-C', root, 'show', f'{rev}:{rel}'], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


# ---- the check -------------------------------------------------------------------------------------

def tables(root: str, read) -> list[tuple]:
    """(table path, function name, family) for symbols/verification.csv and each family's table;
    read(rel) returns a table's text or None."""
    import csv
    out = [('symbols/verification.csv', None, False)]
    text = read('symbols/families.csv')
    if text:
        for row in csv.DictReader(io.StringIO(text)):
            out.append((f"symbols/families/{row['family']}.verification.csv", f"{row['system']}_{row['family']}", True))
    return out


def rows_of(text: str | None, rel: str) -> list[dict]:
    if not text:
        return []
    import csv
    lines = list(csv.reader(io.StringIO(text)))
    if not lines or lines[0] != verification.VERIFICATION_HEADER:
        return []
    return [dict(zip(verification.VERIFICATION_HEADER, r), _line=n, _table=rel) for n, r in enumerate(lines[1:], 2)]


def check(root: str, base: str, jobs: int = os.cpu_count() or 1, log=print) -> list[str]:
    """Errors for fingerprint rows the compiled code does not back."""
    head_tree = os.path.abspath(root)

    def read_head(rel):
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            return None
        with open(path) as f:
            return f.read()

    cur = verification.Currency(root)
    pending = []        # (row, name, family, recorded legacy hash) whose legacy hash moved
    base_values = None
    for rel, family_name, family in tables(root, read_head):
        for r in rows_of(read_head(rel), rel):
            name = family_name or r['function']
            parts = verification.split_value(r['source'])
            if parts is None or not cur.current(name, r['source'], family):
                continue    # legacy rows follow the legacy rule; stale rows are reported by validation
            if parts[1] != cur.legacy(name, family):
                pending.append((r, name, family, parts[1]))
    if not pending:
        return []
    base_values = {}
    for rel, family_name, family in tables(root, lambda rel: show(root, base, rel)):
        for r in rows_of(show(root, base, rel), rel):
            base_values[(rel, r['function'], r['cases'], r['option'])] = r['source']
    errs: list[str] = []
    with tempfile.TemporaryDirectory(prefix='fp-backstop-') as scratch:
        objects = Objects(os.path.join(scratch, 'objects'), jobs)
        os.makedirs(objects.scratch)
        trees: dict[str, str] = {}

        def tree(rev: str) -> str:
            if rev not in trees:
                trees[rev] = os.path.join(scratch, f'tree{len(trees)}')
                extract(root, rev, trees[rev])
            return trees[rev]

        def legacy_at(rev: str, name: str, family: bool) -> str:
            return verification.source_hash(tree(rev), name, family)

        # where each pending row's code is: (row, name, family, revision or None)
        comparisons = []
        branch = None
        for r, name, family, recorded in pending:
            key = (r['_table'], r['function'], r['cases'], r['option'])
            if base_values.get(key) == r['source']:
                if legacy_at(base, name, family) != cur.legacy(name, family):
                    comparisons.append((r, name, family, base))
                continue
            if branch is None:
                branch = [base] + git(root, 'rev-list', '--topo-order', f'--max-count={SEARCH_LIMIT}',
                                      f'{base}..HEAD').split()
            rev = next((c for c in branch if legacy_at(c, name, family) == recorded), None)
            comparisons.append((r, name, family, rev))
        pairs = set()
        for r, name, family, rev in comparisons:
            if rev is not None:
                for t in (tree(rev), head_tree):
                    pairs |= {(t, rel) for rel in library_files(t, name, family)}
        log(f'fingerprint backstop: {len(comparisons)} row(s) to compare, {len(pairs)} object(s) to compile')
        objects.compile_all(pairs)
        signatures: dict[tuple, tuple] = {}

        def signature(t, name, family):
            if (t, name, family) not in signatures:
                signatures[(t, name, family)] = compiled_signature(objects, t, name, family)
            return signatures[(t, name, family)]

        for r, name, family, rev in comparisons:
            where = f"{r['_table']}:{r['_line']}"
            label = r['cases'] + (f" with {r['option']}" if r['option'] else '')
            run_again = (f"run it again with tools/verify.py run --record {r['cases']}, or remove the row "
                         f"(docs/VERIFY.md, \"Source fingerprints\")")
            if rev is None:
                errs.append(f"{where}: the {label} result of {r['function']} is for other code: its legacy "
                            f"hash is not the code's now, and no commit since {base[:12]} has that code "
                            f"(tools/fingerprint_backstop.py): {run_again}")
                continue
            then, why_then = signature(tree(rev), name, family)
            now, why_now = signature(head_tree, name, family)
            if then is None or now is None:
                errs.append(f"{where}: the {label} result of {r['function']} is treated as stale: the "
                            f"backstop cannot compare its compiled code ({why_then or why_now}) "
                            f"(tools/fingerprint_backstop.py): {run_again}")
            elif then != now:
                errs.append(f"{where}: the {label} result of {r['function']} is for other code: its "
                            f"fingerprint is unchanged, but the compiled code of bb_{name} or of what it "
                            f"reaches differs from {rev[:12]} (tools/fingerprint_backstop.py): {run_again}")
    return errs


def default_base(root: str) -> str:
    for ref in ('origin/main', 'main'):
        r = subprocess.run(['git', '-C', root, 'merge-base', 'HEAD', ref], capture_output=True, text=True)
        if r.returncode == 0:
            return r.stdout.strip()
    raise SystemExit('fingerprint_backstop: no merge base with origin/main: fetch it, or pass --base')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--base', help='the revision the branch starts from (default: merge base with origin/main)')
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    a = ap.parse_args()
    if a.base:
        r = subprocess.run(['git', '-C', a.root, 'merge-base', a.base, 'HEAD'], capture_output=True, text=True)
        if r.returncode != 0:
            print(f'fingerprint_backstop: {a.base} has no merge base with HEAD (is the history fetched?)',
                  file=sys.stderr)
            return 1
        base = r.stdout.strip()
    else:
        base = default_base(a.root)
    errs = check(a.root, base, a.jobs)
    for e in errs:
        print(e, file=sys.stderr)
    if not errs:
        print(f'fingerprint backstop: every fingerprint row is backed (base {base[:12]})')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
