#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The compiled-code backstop of source fingerprints: a fingerprint row stays current only where
the compiled code agrees.

usage: fingerprint_backstop.py [--base REV] [--root DIR] [--jobs N] [--compilers CC:CXX,...]

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
  the static initializers of every object it enters, and, where a header of its file set changed,
  the code other sources of the Linux build define for the symbols it reaches outside its object
  (every source that includes a changed header is compiled on both trees, every other one once,
  and references are followed from object to object).

Bytes, sizes, flags, alignments and relocations (by place in this walk, and by name where nothing
compiled defines the symbol) must be identical under every compiler of the CI build matrix (gcc and
clang). Any difference, a source that does not compile, an object without the replacement, or a
changed line under a condition on a platform not compiled here makes the row stale, with a message. The code the row stands for is found as follows:

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
import difflib
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
# runtime/syslib/audio_ajm.c includes the vendored decoder's header (CMakeLists.txt, atrac9)
ATRAC9 = 'third_party/libatrac9/src'
# Sources only the macOS build compiles. Rows record runs of the Linux build, which is what is compared.
NOT_LINUX = re.compile(r'.*_darwin\.(c|cpp)')
SEARCH_LIMIT = 300          # commits of the branch searched for a row's recorded code
STARTUP_SECTIONS = ('.init_array', '.preinit_array', '.ctors', '.fini_array', '.dtors')
EXCEPTION_TABLES = ('.gcc_except_table',)
# Every compiler of the CI build matrix: equal code under one says nothing about another.
DEFAULT_COMPILERS = 'gcc:g++,clang:clang++'
# Conditions on a platform the backstop does not compile for: a changed line under one is stale.
PLATFORM = re.compile(r'__APPLE__|__MACH__|TARGET_OS_|_WIN32|_WIN64|_MSC_VER|__aarch64__|__arm64__|__arm__|'
                      r'__FreeBSD__|__ANDROID__|__linux__|__unix__|__x86_64__|__i386__|__SSE|__AVX|__ELF__')


# ---- reading an ELF64 relocatable object --------------------------------------------------------

SHT_SYMTAB, SHT_RELA, SHT_NOBITS, SHT_SYMTAB_SHNDX = 2, 4, 8, 18
SHN_UNDEF, SHN_ABS, SHN_COMMON, SHN_XINDEX = 0, 0xfff1, 0xfff2, 0xffff
STT_SECTION, STT_FUNC = 3, 2
# flags that change what the code does with a section: writable, allocated, executable, merged,
# strings, TLS
FLAG_MASK = 0x1 | 0x2 | 0x4 | 0x10 | 0x20 | 0x400


class Section:
    def __init__(self, index, name, kind, flags, data, size, link, info, align, entsize):
        self.index, self.name, self.kind, self.flags, self.align = index, name, kind, flags, align
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
        for i, (name, kind, flags, _, offset, size, link, info, align, entsize) in enumerate(headers):
            body = b'' if kind == SHT_NOBITS else data[offset:offset + size]
            self.sections.append(Section(i, cstring(strtab, name), kind, flags, body, size, link, info, align, entsize))
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


def reached_code(objs: dict, starts: list[str], is_root) -> bytes | None:
    """A canonical description of everything the replacement's code reaches, starting in the start
    objects and following references to symbols the other objects define, or None where no start
    object defines the replacement."""
    defined: dict[str, tuple] = {}      # global name -> (rel, symbol): a strong definition first
    for rel in sorted(objs):
        for sym in objs[rel].symbols:
            if sym.binding in (1, 2) and sym.shndx not in (SHN_UNDEF, SHN_ABS, SHN_COMMON):
                if sym.name not in defined or (defined[sym.name][1].binding == 2 and sym.binding == 1):
                    defined[sym.name] = (rel, sym)
    roots = sorted(((rel, s) for rel in starts for s in objs[rel].symbols
                    if s.type == STT_FUNC and s.shndx not in (SHN_UNDEF, SHN_ABS, SHN_COMMON)
                    and is_root(s.name.split('.', 1)[0])), key=lambda p: (p[1].name, p[0]))
    if not roots:
        return None
    order: dict[tuple, int] = {}
    queue: list[tuple] = []
    entered: set[str] = set()
    out = io.BytesIO()

    def visit(rel: str, index: int) -> int:
        if rel not in entered:
            # an object's static initializers run before anything in it is used
            entered.add(rel)
            for s in objs[rel].sections:
                if s.name.startswith(STARTUP_SECTIONS):
                    out.write(f'startup {s.name} {visit(rel, s.index)}\n'.encode())
                elif s.name.startswith(EXCEPTION_TABLES):
                    # which exceptions a landing pad catches: reached from unwind data, not from code
                    out.write(f'exceptions {visit(rel, s.index)}\n'.encode())
        if (rel, index) not in order:
            order[(rel, index)] = len(order)
            queue.append((rel, index))
        return order[(rel, index)]

    def target(rel: str, index: int) -> str:
        obj = objs[rel]
        sym = obj.symbols[index] if index < len(obj.symbols) else None
        if sym is None:
            return 'none'
        if sym.shndx in (SHN_UNDEF, SHN_COMMON):
            where = defined.get(sym.name)
            if where is None:
                return f'external {sym.name} {sym.size if sym.shndx == SHN_COMMON else ""}'
            other, osym = where
            return f'external {sym.name} at {visit(other, osym.shndx)} {osym.value} {osym.size}'
        if sym.shndx == SHN_ABS:
            return f'absolute {sym.value}'
        place = visit(rel, sym.shndx)
        if sym.type == STT_SECTION:
            return f'section {place}'
        # a symbol other objects can name keeps its name; a local one is known by its place
        name = sym.name if sym.binding != 0 else ''
        return f'symbol {place} {sym.value} {sym.size} {name}'

    for rel, sym in roots:
        out.write(f'root {sym.name} {visit(rel, sym.shndx)} {sym.value} {sym.size}\n'.encode())
    done = 0
    while done < len(queue):
        rel, index = queue[done]
        s = objs[rel].sections[index]
        done += 1
        out.write(f'section {order[(rel, index)]} {s.kind} {s.flags & FLAG_MASK} {s.size} {s.entsize} '
                  f'{s.align}\n'.encode())
        out.write(hashlib.sha256(s.data).digest())
        for offset, kind, sym_index, addend in sorted(s.relocations):
            out.write(f'reloc {offset} {kind} {addend} {target(rel, sym_index)}\n'.encode())
    return out.getvalue()


# ---- compiling a tree ----------------------------------------------------------------------------

def compilers(spec: str | None = None) -> list[tuple[str, str]]:
    """(C compiler, C++ compiler) pairs to compare under: every compiler the CI build matrix uses."""
    spec = spec or os.environ.get('BB_BACKSTOP_COMPILERS') or DEFAULT_COMPILERS
    return [tuple(pair.split(':', 1)) for pair in spec.split(',') if pair]


def compile_object(tree: str, rel: str, out: str, pair: tuple[str, str] = ('cc', 'c++')) -> str | None:
    """Compiles one source of the tree to out; None on success, else the compiler's complaint."""
    cxx = rel.endswith('.cpp')
    cmd = shlex.split(pair[1] if cxx else pair[0]) + ['-c', '-std=gnu++20' if cxx else '-std=gnu11'] + \
        GAME_FLAGS + COMPARE_FLAGS + ['-I', 'runtime/include', '-I', ATRAC9, rel, '-o', out]
    env = dict(os.environ, SOURCE_DATE_EPOCH='0', LC_ALL='C')
    try:
        r = subprocess.run(cmd, cwd=tree, capture_output=True, text=True, env=env, timeout=600)
    except (OSError, subprocess.TimeoutExpired) as e:
        return str(e)
    if r.returncode != 0:
        return (r.stderr.strip().splitlines() or ['the compiler failed'])[-1]
    return None


class Objects:
    """Compiled sources per compiler and tree, each compiled once."""

    def __init__(self, scratch: str, jobs: int):
        self.scratch = scratch
        self.jobs = jobs
        self.done: dict[tuple, tuple] = {}     # (compiler pair, tree, rel) -> (ObjectFile | None, error)

    def compile_all(self, keys: set[tuple]) -> None:
        todo = sorted(k for k in keys if k not in self.done)
        with concurrent.futures.ThreadPoolExecutor(max(1, self.jobs)) as pool:
            for key, result in zip(todo, pool.map(self._compile, todo)):
                self.done[key] = result

    def _compile(self, key: tuple) -> tuple:
        pair, tree, rel = key
        out = os.path.join(self.scratch, hashlib.sha256(repr(key).encode()).hexdigest()[:24] + '.o')
        error = compile_object(tree, rel, out, pair)
        if error:
            return None, f'{rel} does not compile with {pair[1] if rel.endswith(".cpp") else pair[0]}: {error}'
        try:
            with open(out, 'rb') as f:
                return ObjectFile(f.read()), ''
        except (OSError, ValueError, struct.error, IndexError) as e:
            return None, f'{rel}: cannot read the object: {e}'
        finally:
            if os.path.exists(out):
                os.unlink(out)

    def get(self, key: tuple) -> tuple:
        if key not in self.done:
            self.compile_all({key})
        return self.done[key]


def library_files(tree: str, name: str, family: bool) -> list[str]:
    return [rel for rel in verification.defining_files(tree, name, family) if LIBRARY_SOURCE.fullmatch(rel)]


_universe: dict[str, list[str]] = {}


def universe(tree: str) -> list[str]:
    """Every source the replacement's calls can reach in the Linux build: the game library's and
    every .c and .cpp under runtime/ (the runtime library, the system libraries, the host). A source
    of it that does not compile makes a cross-object comparison fail: its calls cannot be followed."""
    if tree not in _universe:
        out = []
        for top in ('game', 'runtime'):
            for dp, _, fns in os.walk(os.path.join(tree, top)):
                for fn in fns:
                    rel = os.path.relpath(os.path.join(dp, fn), tree).replace(os.sep, '/')
                    if LIBRARY_SOURCE.fullmatch(rel) or (top == 'runtime' and fn.endswith(('.c', '.cpp'))
                                                         and not NOT_LINUX.fullmatch(rel)):
                        out.append(rel)
        _universe[tree] = sorted(out)
    return _universe[tree]


_includes: dict[tuple, frozenset] = {}


def includes(tree: str, rel: str) -> frozenset:
    """Every header rel includes from game/ or runtime/include/, transitively (as the legacy hash
    resolves them)."""
    key = (tree, rel)
    if key not in _includes:
        search = [os.path.join(tree, 'game'), os.path.join(tree, 'runtime', 'include')]
        seen: set[str] = set()
        todo = [os.path.join(tree, rel)]
        while todo:
            path = os.path.normpath(todo.pop())
            if path in seen or not os.path.isfile(path):
                continue
            seen.add(path)
            with open(path, errors='replace') as f:
                text = f.read()
            for inc in verification.INCLUDE.findall(text):
                for base in [os.path.dirname(path)] + search:
                    candidate = os.path.normpath(os.path.join(base, inc))
                    if os.path.isfile(candidate):
                        todo.append(candidate)
                        break
        _includes[key] = frozenset(os.path.relpath(p, tree).replace(os.sep, '/') for p in seen) - {rel}
    return _includes[key]


def read(tree: str, rel: str) -> str | None:
    path = os.path.join(tree, rel)
    if not os.path.isfile(path):
        return None
    with open(path, errors='replace') as f:
        return f.read()


def conditions(lines: list[str]) -> list[tuple]:
    """For each line, the #if/#elif/#else lines it sits under."""
    stack: list[list[str]] = []
    out = []
    for line in lines:
        m = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if m and m.group(1).startswith('if'):
            stack.append([line])
        elif m and m.group(1) in ('elif', 'else') and stack:
            stack[-1].append(line)
        out.append(tuple(c for frame in stack for c in frame) + ((line,) if m else ()))
        if m and m.group(1) == 'endif' and stack:
            stack.pop()
    return out


def platform_change(then: str | None, now: str | None) -> bool:
    """Whether a changed line of the file sits under (or is) a condition on a platform this
    backstop does not compile for (the compilers are compared here; the platforms are not)."""
    a, b = (then or '').splitlines(), (now or '').splitlines()
    ca, cb = conditions(a), conditions(b)
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if op == 'equal':
            continue
        spots = ca[max(0, i1 - 1):max(i2, i1 + 1)] + cb[max(0, j1 - 1):max(j2, j1 + 1)]
        if any(PLATFORM.search(c) for spot in spots for c in spot):
            return True
    return False


def compare(objects: Objects, then_tree: str, now_tree: str, name: str, family: bool) -> str:
    """'' when everything bb_NAME reaches compiles to the same code on both trees under every
    compiler, else why not."""
    files = set(verification.source_files(then_tree, name, family)) | set(verification.source_files(now_tree, name, family))
    changed = {rel for rel in files if read(then_tree, rel) != read(now_tree, rel)}
    for rel in sorted(changed):
        if platform_change(read(then_tree, rel), read(now_tree, rel)):
            return f'{rel} changed under a platform condition the backstop does not compile for'
    then_defining, now_defining = library_files(then_tree, name, family), library_files(now_tree, name, family)
    if not now_defining:
        return f'no game library source defines bb_{name}'
    # what else the code calls or reads may change where a changed header is used: every source that
    # includes one is compiled on both trees, every other source once, so calls can be followed
    headers = {rel for rel in changed if not LIBRARY_SOURCE.fullmatch(rel)}
    then_objects = {rel: then_tree for rel in then_defining}
    now_objects = {rel: now_tree for rel in now_defining}
    if headers:
        for rel in sorted(set(universe(then_tree)) | set(universe(now_tree))):
            then_has = os.path.isfile(os.path.join(then_tree, rel))
            now_has = os.path.isfile(os.path.join(now_tree, rel))
            affected = (then_has and includes(then_tree, rel) & headers) or (now_has and includes(now_tree, rel) & headers)
            if rel not in then_objects:
                then_objects[rel] = then_tree if (affected and then_has) or not now_has else now_tree
            if rel not in now_objects:
                now_objects[rel] = now_tree if now_has else then_tree
    for pair in compilers():
        objects.compile_all({(pair, t, rel) for side in (then_objects, now_objects) for rel, t in side.items()})
        digests = []
        for side, starts in ((then_objects, then_defining), (now_objects, now_defining)):
            objs = {}
            for rel, t in side.items():
                obj, error = objects.get((pair, t, rel))
                if obj is None:
                    return error
                objs[rel] = obj
            code = reached_code(objs, starts, fingerprint.root_matcher(name, family))
            if code is None:
                return f'no game library source defines bb_{name}'
            digests.append(hashlib.sha256(code).hexdigest())
        if digests[0] != digests[1]:
            return f'differs with {pair[0]}/{pair[1]}'
    return ''


# ---- revisions -------------------------------------------------------------------------------------

def git(root: str, *args: str) -> str:
    return subprocess.run(['git', '-C', root, *args], capture_output=True, text=True, check=True).stdout


def extract(root: str, rev: str, dest: str) -> None:
    """The game/, runtime/ and decoder header trees of rev, written under dest."""
    paths = [p for p in ('game', 'runtime', ATRAC9)
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
    _universe.clear()
    _includes.clear()
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
        log(f'fingerprint backstop: {len(comparisons)} row(s) to compare')
        verdicts: dict[tuple, str] = {}
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
            key = (rev, name, family)
            if key not in verdicts:
                verdicts[key] = compare(objects, tree(rev), head_tree, name, family)
            why = verdicts[key]
            if why.startswith('differs'):
                errs.append(f"{where}: the {label} result of {r['function']} is for other code: its "
                            f"fingerprint is unchanged, but the compiled code of bb_{name} or of what it "
                            f"reaches {why} from {rev[:12]} (tools/fingerprint_backstop.py): {run_again}")
            elif why:
                errs.append(f"{where}: the {label} result of {r['function']} is treated as stale: the "
                            f"backstop cannot show its compiled code unchanged ({why}) "
                            f"(tools/fingerprint_backstop.py): {run_again}")
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
    ap.add_argument('--compilers', help=f'C:C++ compiler pairs to compare under (default {DEFAULT_COMPILERS})')
    a = ap.parse_args()
    if a.compilers:
        os.environ['BB_BACKSTOP_COMPILERS'] = a.compilers
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
