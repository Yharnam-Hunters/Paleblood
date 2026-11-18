#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Every function of the executable, with its first address and real length, from the executable's own tables.

usage: function_bounds.py --check [--root DIR]        the committed list is well formed and leaves no room
                                                      for another function (no executable needed)
       function_bounds.py --elf ELF [--root DIR]      print address,size,kind,name of every function
       function_bounds.py --elf ELF --summary         the counts, and what differs from the committed list
       function_bounds.py --elf ELF --write           regenerate symbols/ghidra_functions.csv and
                                                      symbols/function_kinds.csv

The executable is the contributor's own copy and is only read. Nothing of it is written anywhere: the
list holds addresses and lengths only, the table of kinds addresses and one of two words.

Where a function starts, strongest evidence first:
  unwind   an entry of the unwind table (.eh_frame, found through the GNU_EH_FRAME program header). The
           compiler writes one per function, with its exact first address and byte length.
  header   the entry point, and the initialisation and termination functions the dynamic table names.
  listed   a start of the committed list that has no unwind entry (a disassembler found it). One that
           lies inside an unwind entry is a label in that function, not a function: it is dropped.
  pointer  the target of a pointer the loader fills in (a relative entry of the relocation table) that
           no unwind entry covers. In practice these are the static initialisers of the constructor
           list, which are built without unwind information.
  call, jump, lea
           the target of a direct call, of a direct jump that leaves its function, or of a RIP-relative
           `lea`, when no known function covers it. A `lea` target counts only at a 16-byte boundary
           and when it is not padding.

How long it is: the length of the unwind entry. Without an entry, the last byte reached by following
the code from the start, both ways at every branch, up to the next known start. A call to a function
that never returns ends a path; such a function is one that is the last instruction of at least
NEVER_MIN unwind entries and of at least half of its call sites.

Every byte accounted for: from the first function to the end of the last unwind entry, each byte is in
exactly one function or is padding between two (nop, int3 or zero bytes). Anything else is reported,
and --write then refuses to write the list.

kind, and the name that is printed for a function nobody has named yet:
  thunk        the whole function is one direct jump       thunk_<address>
  static_init  the constructor list points at it           static_init_<address>
  function     anything else                               function_<address>
The list keeps its two columns (address and size), which every other tool reads. The kind of each
thunk and static initialiser is stored beside it in symbols/function_kinds.csv (address,kind); a
function that is not in that table is of kind `function`. The name is the kind, an underscore and
the eight hex digits of the address, so it is not stored a second time.

--check needs no executable. It checks the header, the address format and order, positive sizes, that
no function overlaps the next, that every function starts at a 16-byte boundary, and that the padding
between one function's end and the next start is at most MAX_GAP bytes, so the list tiles the code.
Of the table of kinds it checks the header, the format and order, that the kind is one of the two
stored ones, and that every address is a function of the list.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = 'symbols/ghidra_functions.csv'
HEADER = ['address', 'size']
KINDS_TABLE = 'symbols/function_kinds.csv'
KINDS_HEADER = ['address', 'kind']
ADDRESS = re.compile(r'0x[0-9a-f]{8}\Z')
BASE = 0x400000                 # repository address = ELF virtual address + BASE
ALIGN = 16                      # every function starts at a multiple of this
MAX_GAP = 2 * ALIGN - 1         # padding between two functions is shorter than two alignment slots
NEVER_MIN = 5

PT_LOAD, PT_DYNAMIC, PT_GNU_EH_FRAME, PT_SCE_DYNLIBDATA = 1, 2, 0x6474e550, 0x61000000
DT_RELA, DT_RELASZ, DT_INIT, DT_FINI, DT_SCE_RELA, DT_SCE_RELASZ = 7, 8, 12, 13, 0x6100002f, 0x61000031
R_X86_64_RELATIVE = 8
RELA = struct.Struct('<QQq')    # where, type and symbol, addend
POINTER = 8
ALL_ONES = b'\xff' * POINTER    # the word that opens a constructor list

STOP = {'ret', 'retf', 'ud2', 'hlt', 'int3', 'iretd', 'iretq'}
PAD = {'nop', 'int3'}
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
DIRECT = re.compile(r'0x[0-9a-f]+\Z')
KINDS = ('thunk', 'static_init', 'function')
STORED_KINDS = KINDS[:2]        # what the table of kinds holds; a function it does not name is a `function`
SOURCES = ('unwind', 'header', 'listed', 'pointer', 'call', 'jump', 'lea')


class Image:
    """The executable: its program headers, and its bytes by repository address."""

    def __init__(self, path: str):
        with open(path, 'rb') as f:
            self.data = f.read()
        if self.data[:4] != b'\x7fELF' or self.data[4] != 2:
            raise SystemExit(f'{path}: not a 64-bit ELF file')
        self.entry = struct.unpack_from('<Q', self.data, 0x18)[0] + BASE
        phoff, = struct.unpack_from('<Q', self.data, 0x20)
        phentsize, phnum = struct.unpack_from('<HH', self.data, 0x36)
        self.headers = []       # (type, file offset, virtual address, file size)
        for i in range(phnum):
            kind, _, offset, vaddr, _, filesz = struct.unpack_from('<IIQQQQ', self.data, phoff + phentsize * i)
            self.headers.append((kind, offset, vaddr, filesz))
        self.loads = sorted((vaddr + BASE, offset, filesz) for kind, offset, vaddr, filesz in self.headers
                            if kind == PT_LOAD and filesz)
        self.starts = [start for start, _, _ in self.loads]

    def header(self, kind: int):
        return next((h for h in self.headers if h[0] == kind), None)

    def offset(self, address: int) -> int | None:
        """The file offset of a repository address, or None when no loaded segment holds it."""
        at = bisect.bisect_right(self.starts, address) - 1
        if at >= 0:
            start, offset, size = self.loads[at]
            if address < start + size:
                return offset + address - start
        return None

    def read(self, address: int, length: int) -> bytes:
        """Up to length bytes at a repository address (fewer at the end of its segment)."""
        at = bisect.bisect_right(self.starts, address) - 1
        if at < 0:
            return b''
        start, offset, size = self.loads[at]
        if address >= start + size:
            return b''
        return self.data[offset + address - start:offset + min(address - start + length, size)]


# ---- the unwind table --------------------------------------------------------------------------------

def uleb(data: bytes, at: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def encoded(data: bytes, at: int, encoding: int, here: int, datarel: int):
    """(value, next offset) of a pointer in the exception-header encoding; here is its own address."""
    if encoding == 0xff:
        return None, at
    form, base = encoding & 0x0f, encoding & 0x70
    if form in (0x00, 0x04):
        value, size = struct.unpack_from('<Q', data, at)[0], 8
    elif form == 0x03:
        value, size = struct.unpack_from('<I', data, at)[0], 4
    elif form == 0x0b:
        value, size = struct.unpack_from('<i', data, at)[0], 4
    elif form == 0x0c:
        value, size = struct.unpack_from('<q', data, at)[0], 8
    else:
        raise SystemExit(f'unwind table: pointer form {form:#x} is not handled')
    if base == 0x10:
        value += here
    elif base == 0x30:
        value += datarel
    elif base != 0x00:
        raise SystemExit(f'unwind table: pointer base {base:#x} is not handled')
    return value, at + size


def pointer_encoding(data: bytes, cie: int) -> int:
    """The encoding of the addresses in the entries that use this common header."""
    _, ident, version = struct.unpack_from('<IIB', data, cie)
    if ident != 0:
        raise SystemExit('unwind table: an entry does not point at a common header')
    at = cie + 9
    end = data.index(b'\0', at)
    augmentation = data[at:end].decode()
    at = end + 1
    _, at = uleb(data, at)              # code alignment
    _, at = uleb(data, at)              # data alignment (its sign does not matter for skipping it)
    if version == 1:
        at += 1                         # return-address register, one byte
    else:
        _, at = uleb(data, at)
    encoding = 0x00
    if augmentation.startswith('z'):
        _, at = uleb(data, at)
        for letter in augmentation[1:]:
            if letter == 'R':
                encoding = data[at]
                at += 1
            elif letter == 'L':
                at += 1
            elif letter == 'P':
                _, at = encoded(data, at + 1, data[at] & 0x0f, 0, 0)
    return encoding


def unwind_entries(image: Image) -> list[tuple[int, int]]:
    """Sorted (first address, length) of every entry of the unwind table."""
    header = image.header(PT_GNU_EH_FRAME)
    if header is None:
        raise SystemExit('no GNU_EH_FRAME program header: the unwind table cannot be found')
    _, offset, vaddr, _ = header
    data = image.data
    version, frame_encoding, count_encoding, table_encoding = data[offset:offset + 4]
    if version != 1:
        raise SystemExit(f'unwind table: header version {version} is not handled')
    at = offset + 4
    _, at = encoded(data, at, frame_encoding, vaddr + at - offset, vaddr)
    count, at = encoded(data, at, count_encoding, vaddr + at - offset, vaddr)
    common, out = {}, []
    for _ in range(count or 0):
        start, at = encoded(data, at, table_encoding, vaddr + at - offset, vaddr)
        entry, at = encoded(data, at, table_encoding, vaddr + at - offset, vaddr)
        where = image.offset(entry + BASE)
        if where is None:
            raise SystemExit(f'unwind table: entry {entry:#x} is outside the loaded segments')
        length, back = struct.unpack_from('<Ii', data, where)
        if length == 0xffffffff:
            raise SystemExit('unwind table: 64-bit entries are not handled')
        cie = where + 4 - back
        if cie not in common:
            common[cie] = pointer_encoding(data, cie)
        encoding = common[cie]
        begin, after = encoded(data, where + 8, encoding, entry + 8, vaddr)
        span, _ = encoded(data, after, encoding & 0x0f, 0, 0)
        if begin != start:
            raise SystemExit(f'unwind table: the entry for {start:#x} describes {begin:#x}')
        out.append((begin + BASE, span))
    return sorted(out)


# ---- the relocation table ----------------------------------------------------------------------------

def dynamic_tags(image: Image) -> dict[int, int]:
    """The first value of every tag of the dynamic table."""
    dynamic = image.header(PT_DYNAMIC)
    tags: dict[int, int] = {}
    if dynamic is not None:
        _, offset, _, size = dynamic
        for at in range(offset, offset + size - 15, 16):
            tag, value = struct.unpack_from('<qQ', image.data, at)
            tags.setdefault(tag, value)
    return tags


def header_starts(image: Image) -> set[int]:
    """The entry point and the initialisation and termination functions, as repository addresses."""
    tags = dynamic_tags(image)
    return {image.entry} | {tags[tag] + BASE for tag in (DT_INIT, DT_FINI) if tag in tags}


def relocated_pointers(image: Image) -> dict[int, int]:
    """where -> target of every pointer the loader fills in (relative relocations), repository addresses."""
    tags = dynamic_tags(image)
    if DT_SCE_RELA in tags:             # offsets into the dynamic-library data segment
        library = image.header(PT_SCE_DYNLIBDATA)
        if library is None:
            raise SystemExit('relocation table: no dynamic-library data segment')
        table, size = library[1] + tags[DT_SCE_RELA], tags.get(DT_SCE_RELASZ, 0)
    elif DT_RELA in tags:
        table, size = image.offset(tags[DT_RELA] + BASE), tags.get(DT_RELASZ, 0)
        if table is None:
            raise SystemExit('relocation table: outside the loaded segments')
    else:
        return {}
    out = {}
    for at in range(table, table + size - RELA.size + 1, RELA.size):
        where, info, addend = RELA.unpack_from(image.data, at)
        if info & 0xffffffff == R_X86_64_RELATIVE:
            out[where + BASE] = addend + BASE
    return out


def constructor_list(image: Image, pointers: dict[int, int], first: int, end: int) -> set[int]:
    """The functions of the constructor list: consecutive relocated pointers into the code [first, end)
    that follow a word of all ones."""
    out: set[int] = set()
    run: list[int] = []

    def close() -> None:
        if run:
            at = image.offset(run[0] - POINTER)
            if at is not None and image.data[at:at + POINTER] == ALL_ONES:
                out.update(pointers[where] for where in run)

    for where in sorted(w for w, target in pointers.items() if first <= target < end):
        if run and where != run[-1] + POINTER:
            close()
            run = []
        run.append(where)
    close()
    return out


# ---- the code ----------------------------------------------------------------------------------------

def disassembler():
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    except ImportError:
        raise SystemExit('reading the executable needs the capstone Python module (pip install capstone)')
    return Cs(CS_ARCH_X86, CS_MODE_64)


def direct(operands: str) -> int | None:
    return int(operands, 16) if DIRECT.match(operands) else None


def taken(address: int, size: int, operands: str) -> int | None:
    """The address a RIP-relative operand names."""
    m = RIP.search(operands)
    if not m:
        return None
    return address + size + (int(m.group(2), 16) if m.group(1) == '+' else -int(m.group(2), 16))


def scan_entries(image: Image, md, table):
    """Direct calls, jumps out and taken addresses of every unwind entry; the callees that never return;
    and the entries whose bytes do not all decode (a jump table inside)."""
    call, jump, lea, last = (collections.Counter() for _ in range(4))
    tables = set()
    for start, length in table:
        final, done = None, 0
        for address, size, mnemonic, operands in md.disasm_lite(image.read(start, length), start):
            done += size
            if mnemonic != 'nop':
                final = (mnemonic, operands)
            if mnemonic == 'call':
                target = direct(operands)
                if target is not None:
                    call[target] += 1
            elif mnemonic[0] == 'j' or mnemonic.startswith('loop'):
                target = direct(operands)
                if target is not None and not start <= target < start + length:
                    jump[target] += 1
            elif mnemonic == 'lea':
                target = taken(address, size, operands)
                if target is not None:
                    lea[target] += 1
        if done != length:
            tables.add(start)
        elif final and final[0] == 'call' and direct(final[1]) is not None:
            last[direct(final[1])] += 1
    never = {target for target, n in last.items() if n >= NEVER_MIN and n * 2 >= call[target]}
    return call, jump, lea, never, tables


def walk(image: Image, md, start: int, limit: int, never: set[int]):
    """(length, notes, calls, jumps out, taken addresses) of the code reachable from start below limit."""
    seen, work, end = set(), [start], start
    notes, calls, out, leas = set(), set(), set(), set()
    while work:
        at = work.pop()
        if at in seen:
            continue
        stopped, position = False, at
        for address, size, mnemonic, operands in md.disasm_lite(image.read(at, limit - at), at):
            if address in seen:
                stopped = True
                break
            seen.add(address)
            position = address + size
            end = max(end, position)
            if mnemonic == 'call':
                target = direct(operands)
                if target is not None:
                    calls.add(target)
                    if target in never:
                        stopped = True
                        break
                continue
            if mnemonic == 'jmp':
                target = direct(operands)
                if target is None:
                    notes.add('indirect jump')
                elif start <= target < limit:
                    work.append(target)
                else:
                    out.add(target)
                stopped = True
                break
            if mnemonic[0] == 'j' or mnemonic.startswith('loop'):
                target = direct(operands)
                if target is not None:
                    if start <= target < limit:
                        work.append(target)
                    else:
                        out.add(target)
                continue
            if mnemonic in STOP:
                stopped = True
                break
            if mnemonic == 'lea':
                target = taken(address, size, operands)
                if target is not None:
                    leas.add(target)
        if not stopped:
            notes.add('runs into the next function' if position >= limit else 'bytes that do not decode')
    return end - start, notes, calls, out, leas


class Bounds:
    """What find() found."""

    def __init__(self):
        self.entries: list[tuple[int, int]] = []        # the unwind table
        self.functions: dict[int, list] = {}            # address -> [length, source, note]
        self.labels: list[tuple[int, int]] = []         # (listed start, the function it lies in)
        self.constructors: set[int] = set()
        self.never: set[int] = set()
        self.leftover: list[tuple[int, int, str]] = []  # (address, bytes, 'code' or 'data')
        self.padding = 0
        self.overlaps = 0
        self.kinds: dict[int, str] = {}

    def problems(self) -> list[str]:
        out = [f'0x{a:08x}: {n} bytes of {kind} that no function covers' for a, n, kind in self.leftover]
        if self.overlaps:
            out.append(f'{self.overlaps} functions overlap the one before')
        out += [f'0x{a:08x}: no code at this start' for a, f in sorted(self.functions.items()) if f[0] < 1]
        return out

    def doubts(self) -> list[str]:
        """Walked functions whose end is the next start rather than an instruction that ends them."""
        return [f'0x{a:08x}: {f[2]}' for a, f in sorted(self.functions.items())
                if 'runs into' in f[2] or 'do not decode' in f[2]]


def find(image: Image, listed: set[int]) -> Bounds:
    """Every function of the executable. listed: the starts of the committed list."""
    md = disassembler()
    found = Bounds()
    table = found.entries = unwind_entries(image)
    if not table:
        raise SystemExit('the unwind table is empty')
    entry_starts = [start for start, _ in table]
    first, code_end = table[0][0], table[-1][0] + table[-1][1]
    pointers = relocated_pointers(image)

    def in_entry(address: int) -> int | None:
        """The start of the unwind entry that covers address, if one does."""
        at = bisect.bisect_right(entry_starts, address) - 1
        if at >= 0 and address < entry_starts[at] + table[at][1]:
            return entry_starts[at]
        return None

    call, jump, lea, never, tables = scan_entries(image, md, table)
    found.never = never
    func = found.functions
    for start, length in table:
        func[start] = [length, 'unwind', 'jump table inside' if start in tables else '']

    pending: dict[int, str] = {}
    for address in sorted(header_starts(image)):
        if (address not in func and in_entry(address) is None and BASE < address < code_end
                and image.offset(address) is not None):
            pending[address] = 'header'
    for address in sorted(listed):
        if address in func or address in pending:
            continue
        owner = in_entry(address)
        if owner is not None:
            found.labels.append((address, owner))
        else:
            pending[address] = 'listed'
    for address in sorted(set(pointers.values())):
        if address in func or address in pending or not first <= address < code_end:
            continue
        if in_entry(address) is None:
            pending[address] = 'pointer'

    walked: dict[int, int] = {}                         # start -> end, for functions without an entry
    walked_starts: list[int] = []

    def covered(address: int) -> bool:
        if in_entry(address) is not None:
            return True
        at = bisect.bisect_right(walked_starts, address) - 1
        return at >= 0 and address < walked[walked_starts[at]]

    round_number = 0
    while pending:
        round_number += 1
        starts = sorted(set(entry_starts) | set(walked_starts) | set(pending))
        more: dict[str, set[int]] = {'call': set(), 'jump': set(), 'lea': set()}
        for address in sorted(pending):
            at = bisect.bisect_right(starts, address)
            limit = starts[at] if at < len(starts) else code_end
            if address < code_end:
                limit = min(limit, code_end)
            length, notes, calls, out, leas = walk(image, md, address, limit, never)
            func[address] = [length, pending[address], '; '.join(sorted(notes))]
            walked[address] = address + length
            more['call'] |= calls
            more['jump'] |= out
            more['lea'] |= leas
        walked_starts = sorted(walked)
        pending = {}
        if round_number == 1:
            more['call'] |= set(call)
            more['jump'] |= set(jump)
            more['lea'] |= set(lea)
        for source in ('call', 'jump', 'lea'):
            for address in sorted(more[source]):
                if address in func or address in pending or not first <= address < code_end:
                    continue
                if covered(address):
                    continue
                if source == 'lea':
                    # A taken address counts only when what is there looks like the start of a function.
                    head = next(md.disasm_lite(image.read(address, ALIGN), address), None)
                    if address % ALIGN or head is None or head[2] in PAD:
                        continue
                pending[address] = source

    # Every byte from the first function to the end of the last unwind entry.
    spans = sorted((a, a + f[0]) for a, f in func.items() if f[0] > 0)
    at = spans[0][0]
    for begin, end in spans + [(code_end, code_end)]:
        if begin < at:
            found.overlaps += 1
        elif begin > at:
            raw = image.read(at, begin - at)
            position, run = at, None
            for address, size, mnemonic, _ in md.disasm_lite(raw, at):
                position = address + size
                if mnemonic in PAD or raw[address - at:position - at] == bytes(size):
                    found.padding += size
                    if run:
                        found.leftover.append((run[0], run[1] - run[0], 'code' if run[2] else 'data'))
                        run = None
                else:
                    ends = mnemonic in STOP or mnemonic == 'jmp'
                    run = [run[0], position, run[2] or ends] if run else [address, position, ends]
            if run:
                found.leftover.append((run[0], run[1] - run[0], 'code' if run[2] else 'data'))
            if position < begin:
                if image.read(position, begin - position) == bytes(begin - position):
                    found.padding += begin - position
                else:
                    found.leftover.append((position, begin - position, 'data'))
        at = max(at, end)

    found.constructors = constructor_list(image, pointers, first, code_end)
    for address, (length, _, _) in func.items():
        if address in found.constructors:
            found.kinds[address] = 'static_init'
            continue
        head = next(md.disasm_lite(image.read(address, length), address), None)
        whole_jump = head is not None and head[2] == 'jmp' and head[1] == length and direct(head[3]) is not None
        found.kinds[address] = 'thunk' if whole_jump else 'function'
    return found


# ---- the committed list ------------------------------------------------------------------------------

def read_list(path: str) -> tuple[list[tuple[int, int]], list[str]]:
    """((address, size) rows, errors) of a list file. Rows that do not parse are reported and left out."""
    rows, errors = [], []
    rel = os.path.basename(path)
    if not os.path.isfile(path):
        return rows, [f'{rel}: missing']
    with open(path, newline='') as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header != HEADER:
            errors.append(f'{rel}:1: header must be {",".join(HEADER)}')
        for number, row in enumerate(reader, 2):
            if len(row) != 2 or not ADDRESS.match(row[0]):
                errors.append(f'{rel}:{number}: expected 0x followed by eight lower-case hex digits, then a size')
            elif not row[1].isdigit() or int(row[1]) < 1:
                errors.append(f'{rel}:{number}: size must be a positive integer')
            else:
                rows.append((int(row[0], 16), int(row[1])))
    return rows, errors


def check_list(path: str) -> list[str]:
    """Format and tiling errors of a list file (see the module description)."""
    rows, errors = read_list(path)
    rel = os.path.basename(path)
    for number, ((address, size), following) in enumerate(zip(rows, rows[1:] + [None]), 2):
        if address % ALIGN:
            errors.append(f'{rel}:{number}: 0x{address:08x} is not at a {ALIGN}-byte boundary')
        if following is None:
            continue
        gap = following[0] - (address + size)
        if following[0] <= address:
            errors.append(f'{rel}:{number + 1}: 0x{following[0]:08x} is not after the row before (sorted, unique)')
        elif gap < 0:
            errors.append(f'{rel}:{number}: 0x{address:08x} runs {-gap} bytes into the next function')
        elif gap > MAX_GAP:
            errors.append(f'{rel}:{number}: {gap} bytes after 0x{address:08x} belong to no function '
                          f'(padding is at most {MAX_GAP})')
    return errors


def check_kinds(path: str, list_path: str) -> list[str]:
    """Format errors of the table of kinds, and rows that name no function of the list."""
    rel = os.path.basename(path)
    if not os.path.isfile(path):
        return [f'{rel}: missing']
    starts = {address for address, _ in read_list(list_path)[0]}
    errors, before = [], -1
    with open(path, newline='') as f:
        reader = csv.reader(f)
        if next(reader, None) != KINDS_HEADER:
            errors.append(f'{rel}:1: header must be {",".join(KINDS_HEADER)}')
        for number, row in enumerate(reader, 2):
            if len(row) != 2 or not ADDRESS.match(row[0]):
                errors.append(f'{rel}:{number}: expected 0x followed by eight lower-case hex digits, then a kind')
                continue
            address = int(row[0], 16)
            if row[1] not in STORED_KINDS:
                errors.append(f'{rel}:{number}: kind must be one of {", ".join(STORED_KINDS)}')
            if address <= before:
                errors.append(f'{rel}:{number}: {row[0]} is not after the row before (sorted, unique)')
            if address not in starts:
                errors.append(f'{rel}:{number}: {row[0]} is not a function of {os.path.basename(list_path)}')
            before = address
    return errors


def write_list(path: str, found: Bounds) -> None:
    with open(path, 'w', newline='') as f:
        f.write(','.join(HEADER) + '\n')
        for address, (length, _, _) in sorted(found.functions.items()):
            f.write(f'0x{address:08x},{length}\n')


def write_kinds(path: str, found: Bounds) -> None:
    with open(path, 'w', newline='') as f:
        f.write(','.join(KINDS_HEADER) + '\n')
        for address, kind in sorted(found.kinds.items()):
            if kind in STORED_KINDS:
                f.write(f'0x{address:08x},{kind}\n')


def summary(found: Bounds, listed: dict[int, int]) -> list[str]:
    func = found.functions
    with_entry = {start for start, _ in found.entries}
    kinds = collections.Counter(found.kinds.values())
    out = [f'unwind entries                    {len(found.entries):7d}',
           f'functions                         {len(func):7d}  ({sum(f[0] for f in func.values())} bytes)',
           f'  with an unwind entry            {len(with_entry):7d}',
           f'  without one (length by walking) {len(func) - len(with_entry):7d}',
           '  by kind: ' + ', '.join(f'{k} {kinds[k]}' for k in KINDS),
           f'  with a jump table inside        {sum(1 for f in func.values() if f[2] == "jump table inside"):7d}',
           f'functions of the constructor list {len(found.constructors):7d}',
           f'functions that never return       {len(found.never):7d}',
           f'padding between functions         {found.padding:7d} bytes',
           f'bytes no function covers          {sum(n for _, n, _ in found.leftover):7d}',
           '',
           f'against the committed list ({len(listed)} rows):']
    kept = [a for a in listed if a in func]
    longer = sum(1 for a in kept if func[a][0] > listed[a])
    shorter = sum(1 for a in kept if func[a][0] < listed[a])
    added = [a for a in func if a not in listed]
    out += [f'  rows kept                       {len(kept):7d}',
            f'    same length                   {len(kept) - longer - shorter:7d}',
            f'    longer in the executable      {longer:7d}',
            f'    shorter in the executable     {shorter:7d}',
            f'  rows dropped (labels)           {len(found.labels):7d}',
            f'  rows added                      {len(added):7d}']
    by = collections.Counter((found.kinds[a], func[a][1]) for a in added)
    for (kind, source), n in sorted(by.items(), key=lambda item: -item[1]):
        out.append(f'    {kind:12s} found by {source:8s} {n:7d}')
    for address, owner in found.labels:
        out.append(f'  label 0x{address:08x} lies in the function at 0x{owner:08x}')
    doubts = found.doubts()
    if doubts:
        out += ['', 'walked up to the next start (check by hand):'] + ['  ' + d for d in doubts]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--elf', help='the target executable (read only)')
    ap.add_argument('--check', action='store_true', help='check the committed list; needs no executable')
    ap.add_argument('--summary', action='store_true', help='print the counts instead of the functions')
    ap.add_argument('--write', action='store_true', help=f'regenerate {LIST} and {KINDS_TABLE}')
    args = ap.parse_args()
    path = os.path.join(args.root, LIST)
    kinds_path = os.path.join(args.root, KINDS_TABLE)

    if args.check:
        errors = check_list(path) + check_kinds(kinds_path, path)
        for e in errors[:50]:
            print(e, file=sys.stderr)
        if len(errors) > 50:
            print(f'... and {len(errors) - 50} more', file=sys.stderr)
        if not errors:
            print(f'{LIST}, {KINDS_TABLE}: ok')
        return 1 if errors else 0
    if not args.elf:
        ap.error('give --elf ELF, or --check')

    rows, _ = read_list(path)
    listed = dict(rows)
    found = find(Image(args.elf), set(listed))
    problems = found.problems()
    for p in problems:
        print(p, file=sys.stderr)
    if args.summary:
        print('\n'.join(summary(found, listed)))
    elif not args.write:
        out = csv.writer(sys.stdout, lineterminator='\n')
        out.writerow(HEADER + ['kind', 'name'])
        for address, (length, _, _) in sorted(found.functions.items()):
            kind = found.kinds[address]
            out.writerow([f'0x{address:08x}', length, kind, f'{kind}_{address:08x}'])
    if args.write:
        if problems:
            print(f'{LIST}: not written, the executable is not fully accounted for', file=sys.stderr)
            return 1
        write_list(path, found)
        write_kinds(kinds_path, found)
        stored = sum(1 for kind in found.kinds.values() if kind in STORED_KINDS)
        print(f'{LIST}: {len(found.functions)} functions written', file=sys.stderr)
        print(f'{KINDS_TABLE}: {stored} thunks and static initialisers written', file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
