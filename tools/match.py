#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Matching: rebuild a function with the public compiler of its era and compare the machine code.

usage:
  match.py toolchain [--fetch]          where clang 3.4.2 is (with --fetch: download it from llvm.org)
  match.py run FUNCTION [--source FILE] [--flags ID] [--record] [--diff] [--objdiff BIN] [--elf ELF]
  match.py all [--record] [--objdiff BIN] [--elf ELF]    re-run every row of symbols/matching.csv

Optional supporting evidence, local only, like tools/verify.py (docs/MATCHING.md). `run` compiles
the function's matching source (C, or C++11) with upstream clang 3.4.2 and the PS4 SDK's documented
driver defaults, cuts the original function out of YOUR OWN dump's executable, rebuilds its
external references as relocations in a throwaway object, and compares the two:

  byte_exact     every byte equal, except the 4-byte fields the compiler leaves to the linker
                 (relocations), and a GOT load the linker relaxed to `lea` (decided at link time)
  match_percent  similarity of the two instruction sequences, with branch targets inside the
                 function as offsets and references outside it as placeholders
  objdiff        objdiff-cli's own score for the same pair of objects, when --objdiff (or
                 $BB_OBJDIFF) names a build of it

Nothing from the dump is written into the repository: the objects live in a temporary directory
that is removed afterwards. --record writes one row of numbers to symbols/matching.csv. --diff
prints both instruction sequences side by side for tuning: that is the game's disassembly, so
keep it on your machine (never in an issue, a pull request or a commit).

Exit codes: 0 byte-exact, 1 not byte-exact, 3 bad input.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import datetime
import difflib
import fcntl
import glob
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x00400000
MATCHING_CSV = 'symbols/matching.csv'
HEADER = ['address', 'name', 'compiler', 'flags', 'match_percent', 'objdiff_percent', 'byte_exact',
          'source', 'source_sha256', 'date']
SOURCE_HASH_LENGTH = 16
SOURCE_SUFFIXES = ('.c', '.cpp')

# Upstream release binaries from llvm.org. The SDK's own compiler is proprietary and is never needed
# or used. The checksum pins the exact archive (its .sig is signed by the release manager).
TOOLCHAINS = {
    'clang-3.4.2': {
        'url': 'https://releases.llvm.org/3.4.2/clang+llvm-3.4.2-x86_64-linux-gnu-ubuntu-14.04.xz',
        'sha256': 'ef0d8faeb31c731b46ef5859855766ec7eb71f9a32cc6407ac5ddb4ccc35c3dc',
        'top': 'clang+llvm-3.4.2-x86_64-linux-gnu-ubuntu-14.04',
        'keep': ('bin/clang', 'bin/clang-3.4', 'lib/clang/3.4.2/include/'),
    },
}
DEFAULT_COMPILER = 'clang-3.4.2'

# The PS4 SDK's driver defaults (Robinson, "Developer Toolchain for the PlayStation 4", LLVM Dev
# Meeting 2013; Bedwell, "PS4 CPU toolchain", EuroLLVM 2014): -fPIC -fstack-protector-strong
# -fno-omit-frame-pointer -momit-leaf-frame-pointer -fno-exceptions -fno-rtti -std=gnu++11 for the
# Jaguar CPU (btver2). Clang 3.4 has no -strong (upstream from 3.5): ps4-o2 uses the classic
# protector, which leaves leaf and small functions alone as -strong does; ps4-o2-sspall protects
# every function and stands in for -strong on a function whose original checks the canary.
# FreeBSD is the closest upstream target before x86_64-scei-ps4 (clang 3.7).
COMMON_FLAGS = ['-target', 'x86_64-unknown-freebsd', '-march=btver2', '-O2', '-fPIC',
                '-fno-omit-frame-pointer', '-momit-leaf-frame-pointer', '-fno-exceptions',
                '-nostdlibinc', '-w']
FLAG_SETS = {
    'ps4-o2': COMMON_FLAGS + ['-fstack-protector'],
    'ps4-o2-sspall': COMMON_FLAGS + ['-fstack-protector-all'],
}
DEFAULT_FLAGS = 'ps4-o2'
LANGUAGE_FLAGS = {'.c': ['-x', 'c', '-std=gnu99'], '.cpp': ['-x', 'c++', '-std=gnu++11', '-fno-rtti']}

# ELF constants (executables and relocatable objects).
PT_LOAD, PF_X = 1, 1
SHT_SYMTAB, SHT_RELA = 2, 4
STT_FUNC = 2
R_X86_64_GOTPCREL, R_X86_64_GOTPCRELX, R_X86_64_REX_GOTPCRELX = 9, 41, 42
GOT_LOADS = (R_X86_64_GOTPCREL, R_X86_64_GOTPCRELX, R_X86_64_REX_GOTPCRELX)
MOV_LOAD, LEA = 0x8b, 0x8d
FIELD = 4                       # a rel32 / disp32 field

EXIT_EXACT, EXIT_DIFFERENT, EXIT_BAD_INPUT = 0, 1, 3


class BadInput(Exception):
    pass


# ---- toolchain --------------------------------------------------------------------------------

def toolchain_root() -> str:
    """$BB_MATCH_TOOLCHAINS, else the user cache. Never inside the repository or the dump."""
    explicit = os.environ.get('BB_MATCH_TOOLCHAINS')
    if explicit:
        return explicit
    cache = os.environ.get('XDG_CACHE_HOME') or os.path.join(os.path.expanduser('~'), '.cache')
    return os.path.join(cache, 'paleblood', 'toolchains')


def toolchain_dir(compiler: str) -> str:
    return os.path.join(toolchain_root(), compiler)


def compiler_path(compiler: str) -> str:
    """$BB_MATCH_CLANG overrides (an existing build of exactly this release)."""
    return os.environ.get('BB_MATCH_CLANG') or os.path.join(toolchain_dir(compiler), 'bin', 'clang')


def fetch_toolchain(compiler: str) -> str:
    spec = TOOLCHAINS[compiler]
    dest = toolchain_dir(compiler)
    if os.path.isfile(os.path.join(dest, 'bin', 'clang')):
        return dest
    os.makedirs(toolchain_root(), exist_ok=True)
    with tempfile.TemporaryDirectory(dir=toolchain_root()) as tmp:
        archive = os.path.join(tmp, 'archive.tar.xz')
        print(f'downloading {spec["url"]}', file=sys.stderr)
        with urllib.request.urlopen(spec['url']) as response, open(archive, 'wb') as out:
            shutil.copyfileobj(response, out)
        h = hashlib.sha256()
        with open(archive, 'rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        digest = h.hexdigest()
        if digest != spec['sha256']:
            raise BadInput(f'{spec["url"]}: sha256 {digest}, expected {spec["sha256"]}')
        unpacked = os.path.join(tmp, 'unpacked')
        with tarfile.open(archive, 'r:xz') as tar:
            wanted = [m for m in tar.getmembers()
                      if any(m.name == f'{spec["top"]}/{k}' or (k.endswith('/') and m.name.startswith(f'{spec["top"]}/{k}'))
                             for k in spec['keep'])]
            tar.extractall(unpacked, members=wanted, filter='data')
        os.replace(os.path.join(unpacked, spec['top']), dest)
    return dest


def library_shim(compiler: str) -> str | None:
    """The 2014 binaries link libtinfo.so.5, which current distributions no longer ship; ncurses 6
    provides the same symbols. Returns a directory for LD_LIBRARY_PATH, or None when not needed."""
    try:
        ctypes.CDLL('libtinfo.so.5')
        return None
    except OSError:
        pass
    shim = os.path.join(toolchain_dir(compiler), 'shim')
    link = os.path.join(shim, 'libtinfo.so.5')
    if os.path.exists(link):
        return shim
    for directory in ('/usr/lib', '/usr/lib64', '/usr/lib/x86_64-linux-gnu', '/lib/x86_64-linux-gnu'):
        for candidate in ('libtinfo.so.6', 'libncursesw.so.6', 'libncurses.so.6'):
            path = os.path.join(directory, candidate)
            if os.path.exists(path):
                os.makedirs(shim, exist_ok=True)
                os.symlink(path, link)
                return shim
    raise BadInput('clang 3.4.2 needs libtinfo.so.5 (or ncurses 6 to stand in for it); install ncurses')


def run_compiler(compiler: str, args: list[str]) -> subprocess.CompletedProcess:
    clang = compiler_path(compiler)
    if not os.path.isfile(clang):
        raise BadInput(f'{clang}: no {compiler}; run tools/match.py toolchain --fetch (docs/MATCHING.md)')
    env = dict(os.environ)
    shim = library_shim(compiler)
    if shim:
        env['LD_LIBRARY_PATH'] = shim + (os.pathsep + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    return subprocess.run([clang] + args, env=env, capture_output=True, text=True)


def compile_source(compiler: str, flags: str, source: str, out: str) -> None:
    suffix = os.path.splitext(source)[1]
    if suffix not in LANGUAGE_FLAGS:
        raise BadInput(f'{source}: matching sources are C (.c) or C++11 (.cpp)')
    r = run_compiler(compiler, FLAG_SETS[flags] + LANGUAGE_FLAGS[suffix] + ['-c', source, '-o', out])
    if r.returncode:
        raise BadInput(f'{source}: {compiler} failed:\n{r.stderr.strip()}')


# ---- ELF reading --------------------------------------------------------------------------------

def original_bytes(elf: str, address: int, size: int) -> bytes:
    """The function's bytes from an executable PT_LOAD of the dump's ELF (read only)."""
    with open(elf, 'rb') as f:
        head = f.read(64)
        if head[:4] != b'\x7fELF':
            raise BadInput(f'{elf}: not an ELF file')
        phoff, = struct.unpack_from('<Q', head, 0x20)
        phnum, = struct.unpack_from('<H', head, 0x38)
        f.seek(phoff)
        table = f.read(56 * phnum)
        vaddr = address - IMAGE_BASE
        for i in range(phnum):
            p_type, p_flags, p_offset, p_vaddr, _, p_filesz = struct.unpack_from('<IIQQQQ', table, 56 * i)
            if p_type == PT_LOAD and p_flags & PF_X and p_vaddr <= vaddr and vaddr + size <= p_vaddr + p_filesz:
                f.seek(p_offset + vaddr - p_vaddr)
                return f.read(size)
    raise BadInput(f'0x{address:08x}+{size}: not inside an executable segment of {elf}')


def object_function(path: str, symbol: str) -> tuple[bytes, dict[int, int]]:
    """(bytes, {offset in the function: relocation type}) of a function in a relocatable object."""
    with open(path, 'rb') as f:
        d = f.read()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    shentsize, shnum = struct.unpack_from('<HH', d, 0x3a)
    sections = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + shentsize * i) for i in range(shnum)]
    symtab = next((s for s in sections if s[1] == SHT_SYMTAB), None)
    if symtab is None:
        raise BadInput(f'{path}: no symbol table')
    strtab = sections[symtab[6]]
    found = None
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        st_name, st_info, _, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', d, off)
        name = d[strtab[4] + st_name:d.index(b'\0', strtab[4] + st_name)].decode()
        if name == symbol and st_info & 0xf == STT_FUNC:
            found = (st_shndx, st_value, st_size)
    if found is None:
        raise BadInput(f'{path}: no function named {symbol} (C++ sources declare it extern "C")')
    shndx, value, size = found
    text = sections[shndx]
    code = d[text[4] + value:text[4] + value + size]
    relocs = {}
    for s in sections:
        if s[1] == SHT_RELA and s[7] == shndx:
            for off in range(s[4], s[4] + s[5], 24):
                r_offset, r_info, _ = struct.unpack_from('<QQq', d, off)
                if value <= r_offset < value + size:
                    relocs[r_offset - value] = r_info & 0xffffffff
    return code, relocs


# ---- disassembly ------------------------------------------------------------------------------

INSN = re.compile(r'^\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t?(.*)$')
BRANCH = re.compile(r'^(j\w+|call|jmp)\s+(?:0x)?([0-9a-f]+)\b')
RIP = re.compile(r'\[rip\+0x[0-9a-f]+\]|\[rip-0x[0-9a-f]+\]')
RIP_TARGET = re.compile(r'#\s*(?:0x)?([0-9a-f]+)')


def disassemble(code: bytes, address: int, workdir: str) -> list[tuple[int, bytes, str]]:
    """[(address, bytes, text)] per instruction, Intel syntax, from binutils' objdump."""
    path = os.path.join(workdir, f'code-{address:08x}.bin')
    with open(path, 'wb') as f:
        f.write(code)
    r = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '-M', 'intel',
                        '--insn-width=16', f'--adjust-vma=0x{address:x}', path],
                       capture_output=True, text=True)
    if r.returncode:
        raise BadInput(f'objdump failed (binutils is needed): {r.stderr.strip()}')
    out = []
    for line in r.stdout.splitlines():
        m = INSN.match(line)
        if m:
            out.append((int(m.group(1), 16), bytes.fromhex(m.group(2).replace(' ', '')), m.group(3).strip()))
    return out


def external_references(insns: list[tuple[int, bytes, str]], start: int, end: int) -> dict[int, int]:
    """{field offset in the function: target address} for every rel32/disp32 field of the original
    that reaches outside the function: what the compiler left to the linker as a relocation."""
    return {field: target for field, (target, _) in external_fields(insns, start, end).items()}


def external_fields(insns: list[tuple[int, bytes, str]], start: int, end: int) -> dict[int, tuple[int, tuple | None]]:
    """external_references, with (offset, length, mnemonic) of the instruction when it is a branch."""
    refs = {}
    for addr, raw, text in insns:
        nxt = addr + len(raw)
        m = BRANCH.match(text)
        if m and len(raw) >= 1 + FIELD:
            target = int(m.group(2), 16)
            if not start <= target < end and struct.unpack_from('<i', raw, len(raw) - FIELD)[0] == target - nxt:
                refs[nxt - FIELD - start] = (target, (addr - start, len(raw), m.group(1)))
            continue
        if RIP.search(text):
            t = RIP_TARGET.search(text)
            if not t:
                continue
            target = int(t.group(1), 16)
            pos = raw.find(struct.pack('<i', target - nxt))
            if pos > 0:
                refs[addr + pos - start] = (target, None)
    return refs


def normalize(insns: list[tuple[int, bytes, str]], start: int, fields: set[int]) -> list[str]:
    """Instruction texts with addresses taken out: an internal branch target becomes an offset
    from the function start, a reference through a relocation field becomes EXT / REL, and a GOT
    load and its linker-relaxed lea become the same GOTADDR."""
    out = []
    for addr, raw, text in insns:
        text = re.sub(r'\s*<[^>]*>', '', re.sub(r'\s+#.*$', '', text)).strip()
        relocated = any(addr - start <= f < addr - start + len(raw) for f in fields)
        m = BRANCH.match(text)
        if m:
            target = 'EXT' if relocated else f'L{int(m.group(2), 16) - start:+d}'
            text = f'{m.group(1)} {target}'
        text = re.sub(r'\s+', ' ', RIP.sub('[rip+REL]', text))
        text = re.sub(r'^(?:mov (\w+),QWORD PTR|lea (\w+),) ?\[rip\+REL\]$',
                      lambda g: f'GOTADDR {g.group(1) or g.group(2)}', text)
        out.append(text)
    return out


def masked_equal(original: bytes, ours: bytes, relocs: dict[int, int]) -> bool:
    """Byte-exact apart from relocation fields (and the mov -> lea relaxation of a GOT load)."""
    if len(original) != len(ours):
        return False
    a, b = bytearray(original), bytearray(ours)
    for off, kind in relocs.items():
        a[off:off + FIELD] = b[off:off + FIELD] = bytes(FIELD)
        if kind in GOT_LOADS and off >= 2 and b[off - 2] == MOV_LOAD and a[off - 2] == LEA:
            a[off - 2] = MOV_LOAD
    return a == b


# ---- the comparison -----------------------------------------------------------------------------

def target_object(compiler: str, symbol: str, address: int, code: bytes, fields: dict, workdir: str) -> str:
    """The original function as a relocatable object: its bytes, with each external reference as
    a relocation against a placeholder symbol, so objdiff compares like for like. A call or jump
    is written as the instruction itself, so it carries a PLT relocation as the compiler's does."""
    lines = ['.text', f'.globl {symbol}', f'.type {symbol},@function', '.p2align 4', f'{symbol}:']
    branches = {insn[0]: (target, insn) for target, insn in fields.values() if insn}
    i = 0
    while i < len(code):
        if i in branches:
            target, (_, length, mnemonic) = branches[i]
            lines.append(f'{mnemonic} ext_{target:08x}@PLT')
            i += length
        elif i in fields:
            # field = target - end of instruction, so the addend is the field's distance to that end
            value, = struct.unpack_from('<i', code, i)
            end = fields[i][0] - value - address
            lines.append(f'.long ext_{fields[i][0]:08x} - . - {end - i}')
            i += FIELD
        else:
            lines.append(f'.byte 0x{code[i]:02x}')
            i += 1
    lines.append(f'.size {symbol}, {len(code)}')
    asm = os.path.join(workdir, 'target.s')
    with open(asm, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    out = os.path.join(workdir, 'target.o')
    r = run_compiler(compiler, ['-target', 'x86_64-unknown-freebsd', '-c', '-x', 'assembler', asm, '-o', out])
    if r.returncode:
        raise BadInput(f'assembling the target object failed: {r.stderr.strip()}')
    rebuilt, _ = object_function(out, symbol)
    if len(rebuilt) != len(code):
        raise BadInput(f'the rebuilt target object has {len(rebuilt)} bytes, the original {len(code)}')
    return out


def objdiff_percent(objdiff: str, target: str, base: str, symbol: str) -> str:
    r = subprocess.run([objdiff, 'diff', '-1', target, '-2', base, '-o', '-', '--format', 'json',
                        '-c', 'functionRelocDiffs=none', symbol], capture_output=True, text=True)
    if r.returncode:
        raise BadInput(f'objdiff-cli failed: {r.stderr.strip()[:400]}')
    report = json.loads(r.stdout)
    for side in ('left', 'right'):
        for sym in (report.get(side) or {}).get('symbols', []):
            if sym.get('name') == symbol and 'match_percent' in sym:
                return f'{float(sym["match_percent"]):.1f}'
    return ''


def objdiff_version(objdiff: str) -> str:
    r = subprocess.run([objdiff, '--version'], capture_output=True, text=True)
    return r.stdout.strip()


def match(root: str, name: str, source: str, flags: str, compiler: str, elf: str,
          objdiff: str | None = None, show_diff: bool = False) -> dict:
    funcs = {r['name']: r for r in read_csv(os.path.join(root, 'symbols', 'functions.csv'))}
    if name not in funcs:
        raise BadInput(f'{name}: not in symbols/functions.csv')
    if flags not in FLAG_SETS:
        raise BadInput(f'--flags must be one of {", ".join(FLAG_SETS)}')
    if compiler not in TOOLCHAINS:
        raise BadInput(f'compiler must be one of {", ".join(TOOLCHAINS)}')
    address, size = int(funcs[name]['address'], 16), int(funcs[name]['size'])
    path = os.path.join(root, source)
    if not os.path.isfile(path):
        raise BadInput(f'{source}: no such file')
    original = original_bytes(elf, address, size)
    with tempfile.TemporaryDirectory(prefix='pb-match-') as work:
        ours_obj = os.path.join(work, 'ours.o')
        compile_source(compiler, flags, path, ours_obj)
        ours, relocs = object_function(ours_obj, name)
        o_insns = disassemble(original, address, work)
        c_insns = disassemble(ours, address, work)
        fields = external_fields(o_insns, address, address + size)
        refs = set(fields)
        o_norm = normalize(o_insns, address, refs)
        c_norm = normalize(c_insns, address, set(relocs))
        percent = 100.0 * difflib.SequenceMatcher(None, o_norm, c_norm, autojunk=False).ratio()
        exact = masked_equal(original, ours, relocs)
        result = {'function': name, 'address': f'0x{address:08x}', 'size': size, 'compiled_size': len(ours),
                  'compiler': compiler, 'flags': flags, 'source': source,
                  'match_percent': f'{100.0 if exact else min(percent, 99.9):.1f}',
                  'byte_exact': 'yes' if exact else 'no', 'objdiff_percent': ''}
        if objdiff:
            target = target_object(compiler, name, address, original, fields, work)
            result['objdiff_percent'] = objdiff_percent(objdiff, target, ours_obj, name)
            result['objdiff'] = objdiff_version(objdiff)
        if show_diff and not exact:
            width = max((len(x) for x in o_norm), default=0)
            for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, o_norm, c_norm, autojunk=False).get_opcodes():
                for k in range(max(i2 - i1, j2 - j1)):
                    left = o_norm[i1 + k] if i1 + k < i2 else ''
                    right = c_norm[j1 + k] if j1 + k < j2 else ''
                    mark = ' ' if tag == 'equal' else '|'
                    print(f'{left:<{width}} {mark} {right}', file=sys.stderr)
    return result


# ---- the record ---------------------------------------------------------------------------------

def read_csv(path: str) -> list[dict]:
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        rows = list(csv.reader(f))
    if not rows:
        return []
    return [dict(zip(rows[0], r), _line=n) for n, r in enumerate(rows[1:], 2)]


INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def source_files(root: str, source: str) -> list[str]:
    """The matching source and every header it includes with quotes, recursively."""
    seen: set[str] = set()
    todo = [os.path.normpath(os.path.join(root, source))]
    while todo:
        path = todo.pop()
        if path in seen or not os.path.isfile(path):
            continue
        seen.add(path)
        with open(path, errors='replace') as f:
            for inc in INCLUDE.findall(f.read()):
                todo.append(os.path.normpath(os.path.join(os.path.dirname(path), inc)))
    return sorted(os.path.relpath(p, root).replace(os.sep, '/') for p in seen)


def source_hash(root: str, source: str) -> str:
    h = hashlib.sha256()
    for rel in source_files(root, source):
        h.update(rel.encode() + b'\0')
        with open(os.path.join(root, rel), 'rb') as f:
            h.update(f.read() + b'\0')
    return h.hexdigest()[:SOURCE_HASH_LENGTH]


def write_rows(path: str, rows: list[dict]) -> None:
    out = io.StringIO()
    w = csv.writer(out, lineterminator='\n')
    w.writerow(HEADER)
    for r in sorted(rows, key=lambda r: r['address']):
        w.writerow([r[k] for k in HEADER])
    with open(path, 'w', newline='') as f:
        f.write(out.getvalue())


def record(root: str, result: dict) -> dict:
    row = {'address': result['address'], 'name': result['function'], 'compiler': result['compiler'],
           'flags': result['flags'], 'match_percent': result['match_percent'],
           'objdiff_percent': result['objdiff_percent'], 'byte_exact': result['byte_exact'],
           'source': result['source'], 'source_sha256': source_hash(root, result['source']),
           'date': datetime.date.today().isoformat()}
    path = os.path.join(root, MATCHING_CSV)
    with open(path + '.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rows = [r for r in read_csv(path) if r['address'] != row['address']]
        rows.append(row)
        write_rows(path, rows)
    return row


def byte_exact_functions(root: str) -> set[str]:
    """Names with a byte-exact row whose matching source is unchanged since it was matched."""
    out = set()
    for r in read_csv(os.path.join(root, MATCHING_CSV)):
        src = r.get('source', '')
        if (r.get('byte_exact') == 'yes' and src.startswith('game/') and '..' not in src.split('/')
                and os.path.isfile(os.path.join(root, src)) and r.get('source_sha256') == source_hash(root, src)):
            out.add(r.get('name'))
    return out


# ---- the CI check (format only: CI has no dump) ------------------------------------------------

PERCENT = re.compile(r'^(?:100|[1-9]?\d)\.\d$')
DATE = re.compile(r'^\d{4}-\d{2}-\d{2}$')
ADDR = re.compile(r'^0x[0-9a-f]{8}$')


def check(root: str, funcs: list[dict]) -> list[str]:
    """Errors in symbols/matching.csv: its format, that each row names a tracked function, and
    that the matching source still is the code that was matched."""
    path = os.path.join(root, MATCHING_CSV)
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        rows = list(csv.reader(f))
    if not rows or rows[0] != HEADER:
        return [f'{MATCHING_CSV}: header must be {",".join(HEADER)}']
    errs = []
    by_addr = {f['address']: f for f in funcs}
    prev = None
    for n, values in enumerate(rows[1:], 2):
        where = f'{MATCHING_CSV}:{n}'
        if len(values) != len(HEADER):
            errs.append(f'{where}: expected {len(HEADER)} columns, got {len(values)}')
            continue
        r = dict(zip(HEADER, values))
        if not ADDR.match(r['address']):
            errs.append(f"{where}: address '{r['address']}' must be 0x + 8 lowercase hex digits")
            continue
        if prev is not None and r['address'] <= prev:
            errs.append(f'{where}: addresses must be unique and ascending')
        prev = r['address']
        f = by_addr.get(r['address'])
        if f is None:
            errs.append(f"{where}: {r['address']} has no row in symbols/functions.csv")
        elif f['name'] != r['name']:
            errs.append(f"{where}: name '{r['name']}' differs from symbols/functions.csv ('{f['name']}')")
        if r['compiler'] not in TOOLCHAINS:
            errs.append(f"{where}: compiler must be one of {', '.join(TOOLCHAINS)}")
        if r['flags'] not in FLAG_SETS:
            errs.append(f"{where}: flags must be one of {', '.join(FLAG_SETS)}")
        if not PERCENT.match(r['match_percent']):
            errs.append(f"{where}: match_percent '{r['match_percent']}' must be 0.0 to 100.0, one decimal")
        if r['objdiff_percent'] and not PERCENT.match(r['objdiff_percent']):
            errs.append(f"{where}: objdiff_percent '{r['objdiff_percent']}' must be empty or 0.0 to 100.0")
        if r['byte_exact'] not in ('yes', 'no'):
            errs.append(f"{where}: byte_exact must be yes or no")
        elif (r['byte_exact'] == 'yes') != (r['match_percent'] == '100.0'):
            errs.append(f"{where}: byte_exact is {r['byte_exact']} but match_percent is {r['match_percent']}")
        if not DATE.match(r['date']):
            errs.append(f"{where}: date must be YYYY-MM-DD")
        src = r['source']
        full = os.path.normpath(os.path.join(root, src))
        if not src.startswith('game/') or not src.endswith(SOURCE_SUFFIXES) or '..' in src.split('/'):
            errs.append(f"{where}: source '{src}' must be a .c or .cpp file under game/")
            continue
        if not os.path.isfile(full):
            errs.append(f"{where}: source {src} does not exist")
            continue
        with open(full, errors='replace') as fh:
            if not re.search(rf'\b{re.escape(r["name"])}\s*\(', fh.read()):
                errs.append(f"{where}: {src} does not define {r['name']}")
        if r['source_sha256'] != source_hash(root, src):
            errs.append(f"{where}: {src} changed since it was matched: re-run "
                        f"tools/match.py run {r['name']} --record (docs/MATCHING.md)")
    return errs


# ---- command line -------------------------------------------------------------------------------

def default_elf() -> str | None:
    if os.environ.get('BB_ELF'):
        return os.environ['BB_ELF']
    data = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    path = os.path.join(data, 'elf', 'eboot.elf')
    return path if os.path.isfile(path) else None


def print_result(result: dict) -> None:
    extra = f", objdiff {result['objdiff_percent']}%" if result.get('objdiff_percent') else ''
    verdict = 'byte-exact' if result['byte_exact'] == 'yes' else f"{result['match_percent']}%"
    print(f"{result['function']} {result['address']} ({result['size']} bytes, ours {result['compiled_size']}): "
          f"{verdict}{extra}  [{result['compiler']} {result['flags']}, {result['source']}]")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='command', required=True)
    t = sub.add_parser('toolchain', help='show or download the compiler')
    t.add_argument('--fetch', action='store_true')
    t.add_argument('--compiler', default=DEFAULT_COMPILER, choices=sorted(TOOLCHAINS))
    for name in ('run', 'all'):
        p = sub.add_parser(name)
        if name == 'run':
            p.add_argument('function')
            p.add_argument('--source', help='the matching source, relative to the repository (default: its row)')
            p.add_argument('--flags', help=f'one of {", ".join(FLAG_SETS)} (default: its row, else {DEFAULT_FLAGS})')
            p.add_argument('--diff', action='store_true', help='print both instruction sequences (local only)')
        p.add_argument('--record', action='store_true', help=f'write the result to {MATCHING_CSV}')
        p.add_argument('--elf', help='your dump\'s eboot.elf (default $BB_ELF, then $BB_DATA_ROOT/elf/eboot.elf)')
        p.add_argument('--objdiff', default=os.environ.get('BB_OBJDIFF'), help='objdiff-cli binary (optional)')
        p.add_argument('--json', action='store_true', help='print the result as JSON')
    a = ap.parse_args(argv)
    try:
        if a.command == 'toolchain':
            if a.fetch:
                fetch_toolchain(a.compiler)
            clang = compiler_path(a.compiler)
            if not os.path.isfile(clang):
                print(f'{a.compiler}: not installed; run tools/match.py toolchain --fetch', file=sys.stderr)
                return EXIT_BAD_INPUT
            r = run_compiler(a.compiler, ['--version'])
            print(f'{clang}\n{r.stdout.strip()}')
            return 0
        elf = a.elf or default_elf()
        if not elf or not os.path.isfile(elf):
            raise BadInput('no executable: pass --elf or set BB_ELF (your dump\'s eboot.elf, tools/prepare_dump.sh)')
        rows = {r['name']: r for r in read_csv(os.path.join(ROOT, MATCHING_CSV))}
        if a.command == 'run':
            row = rows.get(a.function, {})
            source = a.source or row.get('source')
            if not source:
                raise BadInput(f'{a.function}: no row in {MATCHING_CSV}; pass --source')
            jobs = [(a.function, source, a.flags or row.get('flags') or DEFAULT_FLAGS,
                     row.get('compiler') or DEFAULT_COMPILER)]
        else:
            jobs = [(r['name'], r['source'], r['flags'], r['compiler']) for r in rows.values()]
        worst = EXIT_EXACT
        changed = 0
        for name, source, flags, compiler in jobs:
            result = match(ROOT, name, source, flags, compiler, elf, a.objdiff, getattr(a, 'diff', False))
            if a.json:
                print(json.dumps(result))
            else:
                print_result(result)
            old = rows.get(name)
            if a.command == 'all' and old and (old['match_percent'], old['byte_exact']) != (result['match_percent'], result['byte_exact']):
                changed += 1
                print(f"  recorded {old['match_percent']}% byte_exact={old['byte_exact']}", file=sys.stderr)
            if a.record:
                record(ROOT, result)
            if result['byte_exact'] != 'yes':
                worst = EXIT_DIFFERENT
        if a.command == 'all':
            print(f'{len(jobs)} function(s), {changed} differ from the record', file=sys.stderr)
        return worst
    except BadInput as e:
        print(f'match: {e}', file=sys.stderr)
        return EXIT_BAD_INPUT


if __name__ == '__main__':
    sys.exit(main())
