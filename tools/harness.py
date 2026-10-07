#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run one function on one captured case, natively, and print a verify.py result case.

usage:
  harness.py --boot BOOT.bin (--case CASE.json | --cases DIR) --address 0xXXXXXXXX
             [--replacement SYMBOL --lib LIB.so] [--timeout SECONDS]

The boot image is the scaffold's prepared image of your own dump (bbport `scripts/prepare.py`,
format BBPROBE2). It is mapped at the scaffold's host address, relocated, and every imported
library function becomes a stub that the case scripts. Game functions listed in the case's
`stubs` are replaced by scripted stubs too (a jump at their entry), on both sides. Without --replacement the original
function at ADDRESS (a PS4 virtual address) runs; with it, SYMBOL from LIB runs after LIB's
bbgame_init (given a loader that installs no hooks). Both see the same memory and the same
scripted library calls. Design and file formats: docs/VERIFY.md.

Prints one JSON object: return value, callee-saved registers, every changed byte of writable
memory, every library call. With --cases, the image is mapped once and every case runs in a
fork of it (a crash or a hang loses only that case); prints {file stem: result}. Runs at low
priority (BB_NICE, default 10). Exits 0, or 3 on bad input.
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import json
import os
import struct
import sys

IMAGE = 0x800000000         # host address of guest offset 0, as in the scaffold
EBOOT_BASE = 0x400000       # PS4 virtual address of guest offset 0
SANDBOX = 0x900000000       # case buffers
SANDBOX_SIZE = 16 << 20
DATA = 0x910000000          # data imports, one page each
CODE = 0x920000000          # trampoline and import stubs
PAGE = 4096
STACK_WINDOW = 1 << 20
RUNAWAY = 1000              # unscripted calls before a case is abandoned

PROT_RWX = 7
MAP_PRIVATE, MAP_ANONYMOUS, MAP_FIXED_NOREPLACE = 0x02, 0x20, 0x100000

# call_capture(block): see docs/VERIFY.md. Block layout: 0 target, 8..48 rdi..r9, 56 rax,
# 64 rdx, 72 xmm0, 88 xmm1, 104 rbx, 112 rbp, 120 r12, 128 r13, 136 r14, 144 r15,
# 152 rsp before the call, 160 rsp after it. Callee-saved registers hold sentinels. xmm0 and
# xmm1 are loaded from their slots before the call (float arguments) and stored back after it.
TRAMPOLINE = bytes.fromhex(
    '53554154415541564157574989fb48bb111111111111111148bd222222222222222249bc3333333333333333'
    '49bd444444444444444449be555555555555555549bf66666666666666664989a398000000498b7b08498b73'
    '10498b5318498b4b204d8b43284d8b4b304d8b13f3410f6f4348f3410f6f4b5831c041ffd24c8b1c24498943'
    '3849895340f3410f7f4348f3410f7f4b5849895b6849896b704d8963784d89ab800000004d89b3880000004d'
    '89bb900000004989a3a00000005f415f415e415d415c5d5bc3')
SENTINELS = {'rbx': 0x1111111111111111, 'rbp': 0x2222222222222222, 'r12': 0x3333333333333333,
             'r13': 0x4444444444444444, 'r14': 0x5555555555555555, 'r15': 0x6666666666666666}
ARG_REGS = ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')
# Native stub for series entries (a wait that polls the clock calls it hundreds of thousands of
# times): copies the next 16-byte value to *arg, counts the call, returns the scripted value;
# when the series is used up it jumps to the Python dispatcher. State block: 0 remaining,
# 8 next value, 16 native calls, 24 Python fallback, 32 return value. Variants by argument
# register; the state address replaces 0x1122334455667788.
SERIES_STUB = {
    0: bytes.fromhex('49bb887766554433221149833b00742349ff0b498b43084c8b104c89174c8b50084c895708'
                     '498343081049ff4310498b4320c3498b4318ffe0'),
    1: bytes.fromhex('49bb887766554433221149833b00742349ff0b498b43084c8b104c89164c8b50084c895608'
                     '498343081049ff4310498b4320c3498b4318ffe0'),
}
SERIES = 0x930000000        # per-case native series stubs and their state
# Internal-function stubs enter through a native prologue that saves xmm0 and xmm1 (float
# arguments, compared with "argf32") at +64, then jumps to the Python dispatcher.
XSTUB = 0x940000000
# Library imports enter through a native wrapper (IMPSTUB_SIZE bytes each): it saves xmm0 and
# xmm1 at +64 (float arguments, compared with "argf32"), calls the Python dispatcher and copies
# its result into xmm0 too, so a float return is scripted as its bit pattern ("ret": "0x3f800000").
IMPSTUB = 0x950000000
IMPSTUB_SIZE = 96
XSTUB_SIZE = 96
FS_LOAD = bytes.fromhex('64488b042500000000')     # mov rax, fs:[0]
ARCH_SET_GS, SYS_ARCH_PRCTL = 0x1001, 158
NID_SALT = bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')


class BadInput(Exception):
    pass


def nid(name: str) -> str:
    digest = hashlib.sha1(name.encode('ascii') + NID_SALT).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip('=').replace('/', '-')


libc = ctypes.CDLL(None, use_errno=True)
libc.mmap.restype = ctypes.c_void_p
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]


def mmap_fixed(address: int, size: int) -> None:
    size = (size + PAGE - 1) // PAGE * PAGE
    got = libc.mmap(address, size, PROT_RWX, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
    if got != address:
        raise BadInput(f'cannot map 0x{address:x} ({size} bytes): errno {ctypes.get_errno()}')


def read_boot(path: str) -> dict:
    with open(path, 'rb') as f:
        d = f.read()
    magic, size, entry, nloads, nrelocs, nnames, _caps = struct.unpack_from('<8sQQQQQQ', d, 0)
    if magic != b'BBPROBE2':
        raise BadInput(f'{path}: expected a BBPROBE2 boot image (bbport scripts/prepare.py)')
    p = 56
    loads = [struct.unpack_from('<QQQ', d, p + 24 * i) for i in range(nloads)]
    p += 24 * nloads
    names = [d[p + 128 * i:p + 128 * (i + 1)].split(b'\0', 1)[0].decode() for i in range(nnames)]
    p += 128 * nnames
    relocs = [struct.unpack_from('<QQqq', d, p + 32 * i) for i in range(nrelocs)]
    p += 32 * nrelocs
    image = d[p:p + size]
    if len(image) != size:
        raise BadInput(f'{path}: truncated image')
    return {'size': size, 'loads': loads, 'names': names, 'relocs': relocs, 'image': image}


def hexint(v) -> int:
    return v if isinstance(v, int) else int(str(v), 0)


STUB_SLOTS = 64             # internal-function stubs per case


class Machine:
    """The image mapped and relocated once, import stubs in place. Library calls dispatch to
    the current Run, so cases can share one mapping (each runs in its own fork)."""

    def __init__(self, boot: dict):
        self.boot = boot
        self.current: 'Run | None' = None
        self.callbacks = []
        b = boot
        mmap_fixed(IMAGE, b['size'])
        ctypes.memmove(IMAGE, b['image'], b['size'])
        mmap_fixed(SANDBOX, SANDBOX_SIZE)
        mmap_fixed(XSTUB, STUB_SLOTS * XSTUB_SIZE)
        names = b['names']
        mmap_fixed(DATA, max(1, len(names)) * PAGE)
        mmap_fixed(CODE, PAGE + 16 * (max(1, len(names)) + STUB_SLOTS))
        ctypes.memmove(CODE, TRAMPOLINE, len(TRAMPOLINE))
        self.proto = ctypes.CFUNCTYPE(ctypes.c_uint64, *[ctypes.c_uint64] * 6)
        mmap_fixed(IMPSTUB, max(1, len(names)) * IMPSTUB_SIZE)
        for i in range(len(names)):
            cb = self.proto(lambda a, b_, c, d, e, f, i=i: self.current.dispatch(i, (a, b_, c, d, e, f)))
            self.callbacks.append(cb)
            wrapper = IMPSTUB + IMPSTUB_SIZE * i
            save = wrapper + 64
            # movabs r11, save; movdqu [r11], xmm0; movdqu [r11+16], xmm1; sub rsp, 8;
            # movabs rax, cb; call rax; add rsp, 8; movq xmm0, rax; ret
            code = (b'\x49\xbb' + struct.pack('<Q', save) + b'\xf3\x41\x0f\x7f\x03' + b'\xf3\x41\x0f\x7f\x4b\x10' +
                    b'\x48\x83\xec\x08' + b'\x48\xb8' + struct.pack('<Q', ctypes.cast(cb, ctypes.c_void_p).value) +
                    b'\xff\xd0' + b'\x48\x83\xc4\x08' + b'\x66\x48\x0f\x6e\xc0' + b'\xc3')
            ctypes.memmove(wrapper, code, len(code))
            stub = b'\x48\xb8' + struct.pack('<Q', wrapper) + b'\xff\xe0'
            ctypes.memmove(CODE + PAGE + 16 * i, stub, len(stub))
        for target, kind, value, addend in b['relocs']:
            if kind == 0:
                v = IMAGE + value
            elif kind == 1:
                v = CODE + PAGE + 16 * value
            else:
                v = DATA + PAGE * value + addend
            ctypes.memmove(IMAGE + target, struct.pack('<Q', v), 8)
        # As the runtime's loader does: the eboot reads its thread pointer with `mov rax, fs:[0]`;
        # glibc owns FS, so that exact instruction in executable segments reads GS instead. A
        # case sets the thread pointer with "gs".
        for vaddr, memsz, flags in b['loads']:
            if flags & 1:
                code = ctypes.string_at(IMAGE + vaddr, memsz)
                at = code.find(FS_LOAD)
                while at >= 0:
                    ctypes.memmove(IMAGE + vaddr + at, b'\x65', 1)
                    at = code.find(FS_LOAD, at + len(FS_LOAD))


class Run:
    def __init__(self, boot: dict, case: dict):
        self.boot, self.case = boot, case
        self.calls: list[dict] = []
        self.errors: list[str] = []
        self.block = (ctypes.c_uint64 * 22)()
        self.scripts: dict[str, list] = {}
        for entry in case.get('imports', []):
            self.scripts.setdefault(nid(entry['name']), []).append(entry)
        self.buffers: dict[str, tuple[int, int]] = {}
        self.callbacks = []
        self.native: dict[int, dict] = {}      # import index -> series stub state
        self.keep = []
        self.stub_scripts: dict[int, list] = {}
        for entry in case.get('stubs', []):
            self.stub_scripts.setdefault(hexint(entry['address']), []).append(entry)

    # memory layout ---------------------------------------------------------------------
    def setup(self, machine: 'Machine') -> None:
        """Lay out this case on a mapped machine: buffers, stubs, memory presets."""
        machine.current = self
        names = self.boot['names']
        proto = machine.proto
        series_imports = []
        for i, raw in enumerate(names):
            entries = self.scripts.get(raw.split('#')[0], [])
            args = {int(e['series']['arg']) for e in entries if 'series' in e}
            if args and args <= {0, 1} and len(args) == 1:
                series_imports.append((i, args.pop()))
        if series_imports:
            mmap_fixed(SERIES, PAGE * (1 + len(series_imports) // 32))
            for n, (i, arg) in enumerate(series_imports):
                state = SERIES + 128 * n
                code = state + 48
                ctypes.memmove(state, bytes(48), 48)
                ctypes.memmove(state + 24, struct.pack('<Q', ctypes.cast(machine.callbacks[i], ctypes.c_void_p).value), 8)
                stub = SERIES_STUB[arg].replace(struct.pack('<Q', 0x1122334455667788), struct.pack('<Q', state))
                ctypes.memmove(code, stub, len(stub))
                jump = b'\x48\xb8' + struct.pack('<Q', code) + b'\xff\xe0'
                ctypes.memmove(CODE + PAGE + 16 * i, jump, len(jump))
                self.native[i] = {'state': state, 'record': None}
        if len(self.stub_scripts) > STUB_SLOTS:
            raise BadInput(f'more than {STUB_SLOTS} stubbed functions')
        off = 0
        for name in sorted(self.case.get('buffers', {})):
            spec = self.case['buffers'][name]
            size = int(spec['size'])
            data = bytes.fromhex(spec.get('bytes', ''))
            if len(data) > size:
                raise BadInput(f'buffer {name}: more bytes than its size')
            self.buffers[name] = (SANDBOX + off, size)
            ctypes.memmove(SANDBOX + off, data + bytes(size - len(data)), size)
            off = (off + size + 15) // 16 * 16
            if off > SANDBOX_SIZE:
                raise BadInput('buffers exceed the sandbox')
        for i, address in enumerate(sorted(self.stub_scripts)):
            save = XSTUB + XSTUB_SIZE * i + 64
            cb = proto(lambda a, b_, c, d, e, f, address=address, save=save:
                       self.dispatch_stub(address, (a, b_, c, d, e, f), save))
            self.callbacks.append(cb)
            code = XSTUB + XSTUB_SIZE * i
            # movabs r11, save; movdqu [r11], xmm0; movdqu [r11+16], xmm1; sub rsp, 8;
            # movabs rax, cb; call rax; add rsp, 8; movq xmm0, rax; ret (a float return is
            # scripted as its bit pattern, as for imports)
            stub = (b'\x49\xbb' + struct.pack('<Q', save) + b'\xf3\x41\x0f\x7f\x03' + b'\xf3\x41\x0f\x7f\x4b\x10' +
                    b'\x48\x83\xec\x08' + b'\x48\xb8' + struct.pack('<Q', ctypes.cast(cb, ctypes.c_void_p).value) +
                    b'\xff\xd0' + b'\x48\x83\xc4\x08' + b'\x66\x48\x0f\x6e\xc0' + b'\xc3')
            ctypes.memmove(code, stub, len(stub))
            jump = b'\xff\x25\x00\x00\x00\x00' + struct.pack('<Q', code)
            ctypes.memmove(self.guest(address), jump, len(jump))
        if 'gs' in self.case:
            # The guest thread pointer (gs:[0] must hold it too, as a TCB does).
            if libc.syscall(SYS_ARCH_PRCTL, ARCH_SET_GS, ctypes.c_ulong(self.value(self.case['gs']))):
                raise BadInput(f'arch_prctl(ARCH_SET_GS) failed: errno {ctypes.get_errno()}')
        for m in self.case.get('memory', []):
            target = m['addr']
            addr = self.value(target) if isinstance(target, str) and target.startswith('buf:') else self.guest(hexint(target))
            if 'pointer' in m:
                data = struct.pack('<Q', self.value(f"buf:{m['pointer']}"))
            elif 'guest' in m:
                data = struct.pack('<Q', self.guest(hexint(m['guest'])))
            else:
                data = bytes.fromhex(m['bytes'])
            ctypes.memmove(addr, data, len(data))

    def guest(self, ps4: int) -> int:
        if not EBOOT_BASE <= ps4 < EBOOT_BASE + self.boot['size']:
            raise BadInput(f'0x{ps4:x} is outside the image')
        return IMAGE + ps4 - EBOOT_BASE

    def value(self, v) -> int:
        if isinstance(v, str) and v.startswith('buf:'):
            name, _, off = v[4:].partition('+')
            if name not in self.buffers:
                raise BadInput(f'unknown buffer {name}')
            return self.buffers[name][0] + (int(off, 0) if off else 0)
        if isinstance(v, str) and v.startswith('guest:'):
            return self.guest(int(v[6:], 0))
        return hexint(v) & 0xffffffffffffffff

    def describe(self, v: int) -> str:
        top = self.block[19]
        if top and top - STACK_WINDOW <= v <= top:
            return 'stack'
        if IMAGE <= v < IMAGE + self.boot['size']:
            return f'0x{v - IMAGE + EBOOT_BASE:08x}'
        for name, (start, size) in self.buffers.items():
            if start <= v < start + size:
                return f'buf:{name}+0x{v - start:x}'
        return f'0x{v:x}'

    # library calls ---------------------------------------------------------------------
    def finish_series(self, index: int) -> None:
        """Fold the calls a native series stub served into the record of its first call."""
        n = self.native.get(index)
        if n and n['record'] is not None:
            served = ctypes.c_uint64.from_address(n['state'] + 16).value
            n['record']['count'] = 1 + served
            ctypes.c_uint64.from_address(n['state'] + 16).value = 0
            ctypes.c_uint64.from_address(n['state']).value = 0
            n['record'] = None

    def dispatch(self, index: int, args: tuple) -> int:
        self.finish_series(index)
        raw = self.boot['names'][index]
        key = raw.split('#')[0]
        queue = self.scripts.get(key)
        if not queue:
            self.errors.append(f'unscripted library call {raw}')
            if len(self.errors) > RUNAWAY:
                # The function keeps calling past its script (a wait that never ends): stop now.
                print(f'harness: {RUNAWAY} unscripted calls; the function does not finish on this case',
                      file=sys.stderr)
                os._exit(4)
            self.calls.append({'import': raw, 'args': []})
            return 0
        entry = queue[0]
        series = entry.get('series')
        if series:
            # One entry for many consecutive calls: each call writes the next value.
            i = entry.setdefault('_next', 0)
            entry['_next'] = i + 1
            if entry['_next'] >= len(series['values']):
                queue.pop(0)
        else:
            queue.pop(0)
        argc = int(entry.get('argc', 0))
        self.calls.append({'import': entry['name'], 'args': [self.describe(a) for a in args[:argc]]})
        if entry.get('argf32'):
            save = IMPSTUB + IMPSTUB_SIZE * index + 64
            self.calls[-1]['argf32'] = [ctypes.string_at(save + 16 * int(r), 4).hex() for r in entry['argf32']]
        if series:
            if series.get('format') != 'timeval_us' or int(series.get('offset', 0)):
                raise BadInput(f"unsupported series {series.get('format')} at offset {series.get('offset', 0)}")
            pack = lambda v: struct.pack('<qq', int(v) // 1_000_000, int(v) % 1_000_000)
            ctypes.memmove(args[int(series['arg'])], pack(series['values'][i]), 16)
            ret = hexint(entry.get('ret', 0)) & 0xffffffffffffffff
            n = self.native.get(index)
            rest = series['values'][i + 1:]
            if n and rest:
                # The native stub serves the rest of this series without coming back here.
                buf = ctypes.create_string_buffer(b''.join(pack(v) for v in rest))
                self.keep.append(buf)
                ctypes.memmove(n['state'], struct.pack('<QQQ', len(rest), ctypes.addressof(buf), 0), 24)
                ctypes.memmove(n['state'] + 32, struct.pack('<Q', ret), 8)
                n['record'] = self.calls[-1]
                if queue and queue[0] is entry:
                    queue.pop(0)
            return ret
        for w in entry.get('writes', []):
            data = bytes.fromhex(w['bytes'])
            if 'size' in w and len(data) != int(w['size']):
                self.errors.append(f"{entry['name']}: write of {len(data)} bytes, the case says {w['size']}")
            ctypes.memmove(args[int(w['arg'])] + int(w.get('offset', 0)), data, len(data))
        return hexint(entry.get('ret', 0)) & 0xffffffffffffffff

    def dispatch_stub(self, address: int, args: tuple, save: int = 0) -> int:
        queue = self.stub_scripts.get(address)
        name = f'0x{address:08x}'
        if not queue:
            self.errors.append(f'unscripted call to stubbed function {name}')
            self.calls.append({'call': name, 'args': []})
            return 0
        entry = queue.pop(0)
        picks = entry.get('argi', list(range(int(entry.get('argc', 0)))))
        record = {'call': name, 'args': [self.describe(args[i]) for i in picks]}
        for r in entry.get('argf32', []):
            # Float arguments: xmm0 / xmm1 as their 32-bit pattern.
            record.setdefault('argf32', []).append(ctypes.string_at(save + 16 * int(r), 4).hex())
        for m in entry.get('argmem', []):
            at = args[int(m['arg'])] + int(m.get('offset', 0))
            record.setdefault('argmem', []).append(ctypes.string_at(at, int(m['size'])).hex())
        self.calls.append(record)
        for w in entry.get('writes', []):
            # What the real function stored through a pointer argument (e.g. a constructor).
            if 'pointer' in w:
                data = struct.pack('<Q', self.value(f"buf:{w['pointer']}"))
            elif 'guest' in w:
                data = struct.pack('<Q', self.guest(hexint(w['guest'])))
            else:
                data = bytes.fromhex(w['bytes'])
            ctypes.memmove(args[int(w['arg'])] + int(w.get('offset', 0)), data, len(data))
        return self.value(entry.get('ret', 0))

    # execution -------------------------------------------------------------------------
    def snapshot(self) -> dict[int, bytes]:
        regions = {}
        for vaddr, memsz, flags in self.boot['loads']:
            if flags & 2:
                regions[IMAGE + vaddr] = ctypes.string_at(IMAGE + vaddr, memsz)
        for start, size in self.buffers.values():
            regions[start] = ctypes.string_at(start, size)
        return regions

    def run(self, target: int) -> dict:
        args = self.case.get('args', {})
        unknown = set(args) - set(ARG_REGS) - {'xmm0', 'xmm1'}
        if unknown:
            raise BadInput(f'unknown argument registers {sorted(unknown)}')
        self.block[0] = target
        for i, reg in enumerate(ARG_REGS):
            self.block[1 + i] = self.value(args.get(reg, 0))
        for reg, off in (('xmm0', 72), ('xmm1', 88)):
            raw = bytes.fromhex(args.get(reg, '')).ljust(16, b'\0')[:16]
            ctypes.memmove(ctypes.addressof(self.block) + off, raw, 16)
        before = self.snapshot()
        ctypes.CFUNCTYPE(None, ctypes.c_void_p)(CODE)(ctypes.addressof(self.block))
        for index in self.native:
            self.finish_series(index)
        after = self.snapshot()
        out: dict = {'id': self.case.get('id', 'case')}
        returns = self.case.get('returns', 'i64')
        rax, rdx = self.block[7], self.block[8]
        xmm0 = bytes(ctypes.string_at(ctypes.addressof(self.block) + 72, 16))
        xmm1 = bytes(ctypes.string_at(ctypes.addressof(self.block) + 88, 16))
        ret = {'i8': {'rax': f'0x{rax & 0xff:x}'},
               'i32': {'rax': f'0x{rax & 0xffffffff:x}'},
               'i64': {'rax': self.describe(rax)},
               'i128': {'rax': f'0x{rax:x}', 'rdx': f'0x{rdx:x}'},
               'f32': {'xmm0': xmm0[:4].hex()},
               'f64': {'xmm0': xmm0[:8].hex()},
               'vec': {'xmm0': xmm0.hex(), 'xmm1': xmm1.hex()},
               'void': {}}.get(returns)
        if ret is None:
            raise BadInput(f'unknown returns type {returns}')
        out['ret'] = ret
        regs = dict(zip(SENTINELS, self.block[13:19]))
        out['preserved'] = {r: 'kept' if regs[r] == SENTINELS[r] else f'0x{regs[r]:x}' for r in SENTINELS}
        out['preserved']['rsp'] = 'kept' if self.block[20] == self.block[19] else 'unbalanced'
        writes = []
        for start, old in before.items():
            new = after[start]
            i, n = 0, len(old)
            while i < n:
                if old[i] == new[i]:
                    i += 1
                    continue
                j = i
                while j < n and old[j] != new[j]:
                    j += 1
                writes.append({'addr': self.describe(start + i), 'bytes': new[i:j].hex()})
                i = j
        out['writes'] = writes
        out['calls'] = self.calls
        if self.errors:
            out['errors'] = self.errors
        return out


def load_replacement(lib_path: str, symbol: str, size: int) -> int:
    """dlopen the game library, run its bbgame_init with a loader that installs no hooks, and
    return the address of SYMBOL."""
    lib = ctypes.CDLL(lib_path)
    hook = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p)(lambda o, s, t: 0)
    load_replacement.keep = (lib, hook)

    class Host(ctypes.Structure):
        _fields_ = [('version', ctypes.c_uint32), ('image', ctypes.c_void_p),
                    ('image_size', ctypes.c_uint64), ('install_hook', ctypes.c_void_p)]
    host = Host(1, IMAGE, size, ctypes.cast(hook, ctypes.c_void_p).value)
    if lib.bbgame_init(ctypes.byref(host)):
        raise BadInput('bbgame_init failed')
    return ctypes.cast(getattr(lib, symbol), ctypes.c_void_p).value


def run_case(machine: Machine, case: dict, address: str, target: int | None) -> dict:
    run = Run(machine.boot, case)
    run.setup(machine)
    return run.run(target if target is not None else run.guest(int(address, 0)))


def batch(machine: Machine, cases_dir: str, address: str, target: int | None, timeout: int) -> dict:
    """Every *.json in CASES_DIR, each in a fork of the mapped machine: a crash or a hang loses
    only that case. Returns {file stem: result}."""
    import glob
    import signal
    out = {}
    for path in sorted(glob.glob(os.path.join(cases_dir, '*.json'))):
        stem = os.path.splitext(os.path.basename(path))[0]
        with open(path) as f:
            case = json.load(f)
        r, w = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(r)
            signal.alarm(timeout)
            try:
                res = run_case(machine, case, address, target)
                code = 0
            except BadInput as e:
                res, code = {'bad_input': str(e)}, 3
            with os.fdopen(w, 'w') as f:
                json.dump(res, f, sort_keys=True)
            os._exit(code)
        os.close(w)
        with os.fdopen(r) as f:
            data = f.read()
        _, status = os.waitpid(pid, 0)
        if os.WIFSIGNALED(status):
            sig = os.WTERMSIG(status)
            out[stem] = {'fault': f'timeout after {timeout} s' if sig == signal.SIGALRM else f'signal {sig}'}
        elif os.WEXITSTATUS(status) == 3:
            raise BadInput(f'{stem}: {json.loads(data)["bad_input"]}')
        elif os.WEXITSTATUS(status) != 0 or not data:
            out[stem] = {'fault': f'exit {os.WEXITSTATUS(status)}'}
        else:
            out[stem] = json.loads(data)
        out[stem]['id'] = stem
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--boot', required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--case', help='one case file; prints its result')
    g.add_argument('--cases', help='a directory of case files; prints {file stem: result}')
    ap.add_argument('--address', required=True)
    ap.add_argument('--replacement')
    ap.add_argument('--lib')
    ap.add_argument('--timeout', type=int, default=20, help='per case, batch mode')
    ap.add_argument('--patch', help='a shadPS4 patch file; with --patch-name, its bytes are written '
                    'into the image first (to check an option against the patch it replaces)')
    ap.add_argument('--patch-name')
    a = ap.parse_args()
    # Verification is background work: keep the machine responsive (BB_NICE=0 to disable).
    try:
        os.nice(int(os.environ.get('BB_NICE', '10')))
    except OSError:
        pass
    try:
        boot = read_boot(a.boot)
        machine = Machine(boot)
        if a.patch:
            if not a.patch_name:
                raise BadInput('--patch needs --patch-name')
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            from patch_overlap import patch_bytes
            for addr, data in patch_bytes(a.patch, a.patch_name):
                if not EBOOT_BASE <= addr < EBOOT_BASE + boot['size'] - len(data):
                    raise BadInput(f'patch line at 0x{addr:08x} is outside the image')
                ctypes.memmove(IMAGE + addr - EBOOT_BASE, data, len(data))
        target = None
        if a.replacement:
            if not a.lib:
                raise BadInput('--replacement needs --lib')
            target = load_replacement(a.lib, a.replacement, boot['size'])
        if a.case:
            with open(a.case) as f:
                result = run_case(machine, json.load(f), a.address, target)
        else:
            result = batch(machine, a.cases, a.address, target, a.timeout)
    except (OSError, ValueError, KeyError, BadInput, json.JSONDecodeError, AttributeError) as e:
        print(f'harness: {e}', file=sys.stderr)
        return 3
    json.dump(result, sys.stdout, sort_keys=True)
    print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
