# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic PS4-style executables for the tests: loadable segments, a PT_DYNAMIC table and
PT_SCE_DYNLIBDATA holding the string, symbol and relocation tables (the layout runtime/loader.c
reads)."""
import struct

R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_X86_64_RELATIVE = 1, 6, 7, 8
STT_OBJECT, STT_FUNC = 1, 2


DT_SCE_EXPORT_LIB, DT_SCE_IMPORT_LIB = 0x61000013, 0x61000015


def build(segments, symbols, relocations, jump_slots=(), entry=0, libraries=()):
    """segments: (vaddr, bytes, memsz, flags); symbols: (name, type, defined_value or None);
    relocations / jump_slots: (target, type, symbol index (1-based, 0 for none), addend);
    libraries: (DT_SCE_IMPORT_LIB or DT_SCE_EXPORT_LIB, name, id)."""
    strtab = b'\0'
    library_tags = []
    for tag, name, ident in libraries:
        library_tags.append((tag, (ident << 48) | (1 << 32) | len(strtab)))
        strtab += name.encode() + b'\0'
    syms = struct.pack('<IBBHQQ', 0, 0, 0, 0, 0, 0)
    for name, stype, value in symbols:
        syms += struct.pack('<IBBHQQ', len(strtab), (1 << 4) | stype, 0, 0 if value is None else 1, value or 0, 0)
        strtab += name.encode() + b'\0'
    rela = b''.join(struct.pack('<QQq', t, (s << 32) | ty, a) for t, ty, s, a in relocations)
    jmprel = b''.join(struct.pack('<QQq', t, (s << 32) | ty, a) for t, ty, s, a in jump_slots)
    dynlib = strtab.ljust((len(strtab) + 15) // 16 * 16, b'\0')
    o_sym = len(dynlib)
    dynlib += syms
    o_rela = len(dynlib)
    dynlib += rela
    o_jmp = len(dynlib)
    dynlib += jmprel
    tags = [(0x61000035, 0), (0x61000037, len(strtab)), (0x61000039, o_sym), (0x6100003f, len(syms)),
            (0x6100002f, o_rela), (0x61000031, len(rela)), (0x61000029, o_jmp), (0x6100002d, len(jmprel)),
            *library_tags, (0, 0)]
    dynamic = b''.join(struct.pack('<qQ', t, v) for t, v in tags)
    nph = len(segments) + 2
    offset = (64 + 56 * nph + 0xfff) // 0x1000 * 0x1000
    body, phdrs = b'', []
    for vaddr, data, memsz, flags in segments:
        phdrs.append((1, flags, offset + len(body), vaddr, len(data), memsz))
        body += data
        body = body.ljust((len(body) + 15) // 16 * 16, b'\0')
    phdrs.append((2, 6, offset + len(body), 0, len(dynamic), len(dynamic)))
    body += dynamic
    phdrs.append((0x61000000, 4, offset + len(body), 0, len(dynlib), 0))
    body += dynlib
    header = b'\x7fELF' + bytes([2, 1, 1, 9]) + bytes(8)
    header += struct.pack('<HHIQQQIHHHHHH', 0xfe10, 62, 1, entry, 64, 0, 0, 64, 56, nph, 64, 0, 0)
    # type, flags, offset, vaddr, paddr, filesz, memsz, align
    ph = b''.join(struct.pack('<IIQQQQQQ', t, fl, off, va, va, fs, ms, 0x10) for t, fl, off, va, fs, ms in phdrs)
    return (header + ph).ljust(offset, b'\0') + body
