#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Append analyst sections and catalog function seeds to an exact-target ELF for REA.

The output is a private analysis view, not a runtime executable or original symbol recovery.
Every loadable byte, virtual address and program header remains identical to the source.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
CATALOG_BASE = 0x400000
LOAD_TYPES = (1, 0x61000010)
ELF_HEADER_SIZE = 64
PROGRAM_HEADER_SIZE = 56
SECTION_HEADER_SIZE = 64
SYMBOL_SIZE = 24
MAX_IMAGE_SIZE = 1 << 30


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pinned_digest(root: Path, filename: str) -> str:
    matches = []
    for line in (root / filename).read_text().splitlines():
        if line.strip():
            digest, name = line.split(None, 1)
            if name.strip().lstrip('*') == 'eboot.elf':
                matches.append(digest)
    if len(matches) != 1 or len(matches[0]) != 64:
        raise ValueError(f'{filename}: expected one eboot.elf SHA-256 pin')
    try:
        bytes.fromhex(matches[0])
    except ValueError as error:
        raise ValueError(f'{filename}: invalid SHA-256 pin') from error
    return matches[0].lower()


def parse_loads(data: bytes) -> tuple[list[dict], int, int]:
    if len(data) < ELF_HEADER_SIZE or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('source must be an ELF64 little-endian image')
    fields = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    (_kind, machine, version, _entry, phoff, shoff, _flags, ehsize,
     phentsize, phnum, _shentsize, shnum, shstrndx) = fields
    if machine != 62 or version != 1 or ehsize != ELF_HEADER_SIZE:
        raise ValueError('source must have a valid x86-64 ELF header')
    if shoff or shnum or shstrndx:
        raise ValueError('source already has section metadata; refusing to rewrite it')
    if phentsize != PROGRAM_HEADER_SIZE or not phnum or phnum == 0xffff:
        raise ValueError('unsupported program-header layout')
    if phoff < ELF_HEADER_SIZE or phoff + phnum * phentsize > len(data):
        raise ValueError('program-header table is outside the source file')
    loads = []
    for index in range(phnum):
        kind, flags, offset, address, _physical, file_size, memory_size, _align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + index * phentsize)
        if kind not in LOAD_TYPES:
            continue
        if file_size > memory_size or offset + file_size > len(data):
            raise ValueError(f'load segment {index}: invalid file-backed extent')
        if address + memory_size > MAX_IMAGE_SIZE:
            raise ValueError(f'load segment {index}: unsupported loaded-image size')
        # The two changed e_sh* ranges must not also be bytes in the runtime image.
        if any(offset < end and start < offset + file_size for start, end in ((40, 48), (58, 64))):
            raise ValueError('section-header fields overlap a file-backed load segment')
        loads.append({'program_header_index': index, 'file_offset': offset,
                      'virtual_address': address, 'file_size': file_size,
                      'memory_size': memory_size, 'flags': flags})
    if not loads or not any(row['file_size'] and row['flags'] & 1 for row in loads):
        raise ValueError('source has no file-backed executable load segment')
    return loads, phoff, phnum * phentsize


def loaded_digest(data: bytes, loads: list[dict]) -> str:
    image = bytearray(max(row['virtual_address'] + row['memory_size'] for row in loads))
    for row in loads:
        start, size = row['virtual_address'], row['file_size']
        offset = row['file_offset']
        image[start:start + size] = data[offset:offset + size]
    return sha256(image)


def catalog_functions(catalog: bytes, names: bytes, loads: list[dict]) -> tuple[list[dict], int]:
    rows = list(csv.DictReader(io.StringIO(catalog.decode('utf-8'))))
    if not rows or not {'address', 'size'} <= rows[0].keys():
        raise ValueError('function catalog must contain address and size rows')
    functions = {}
    for row in rows:
        address, size = int(row['address'], 0), int(row['size'], 0)
        if address in functions or address < CATALOG_BASE or size <= 0:
            raise ValueError('function catalog has duplicate or invalid address/size rows')
        value = address - CATALOG_BASE
        owners = [index for index, load in enumerate(loads) if load['flags'] & 1
                  and load['virtual_address'] <= value < load['virtual_address'] + load['file_size']]
        if len(owners) != 1:
            raise ValueError(f'catalog address {address:#x} is not in one file-backed executable load')
        functions[address] = {'address': address, 'value': value, 'catalog_size': size,
                              'load_index': owners[0]}
    named = {}
    used_names = set()
    for row in csv.DictReader(io.StringIO(names.decode('utf-8'))):
        address = int(row['address'], 0)
        name = row['name']
        if address not in functions:
            raise ValueError(f'named address {address:#x} is absent from the function catalog')
        if not name or '\0' in name or any(char.isspace() for char in name):
            raise ValueError(f'invalid analyst symbol name at {address:#x}')
        if address in named or name in used_names:
            raise ValueError('analyst symbol addresses and names must be unique')
        named[address] = name
        used_names.add(name)
    result = []
    for address, function in sorted(functions.items()):
        if address in named:
            name = named[address]
        else:
            name = f'pb_catalog_{address:08x}'
            while name in used_names:
                name += '_seed'
            used_names.add(name)
        result.append({**function, 'name': name})
    return result, len(named)


def analysis_sections(loads: list[dict], functions: list[dict]) -> tuple[list[dict], dict[int, int]]:
    sections = []
    function_sections = {}
    counts = {}

    def add(name, address, offset, size, flags):
        index = counts.get(name, 0)
        counts[name] = index + 1
        sections.append({'name': name if not index else f'{name}.{index}', 'type': 1,
                         'flags': flags, 'address': address, 'offset': offset, 'size': size,
                         'link': 0, 'info': 0, 'alignment': 1, 'entry_size': 0})
        return len(sections)  # section zero is the required null entry

    for index, load in enumerate(loads):
        size = load['file_size']
        if not size:
            continue
        address, offset = load['virtual_address'], load['file_offset']
        flags = 2 | (1 if load['flags'] & 2 else 0)
        if load['flags'] & 1:
            seeds = [row for row in functions if row['load_index'] == index]
            # Catalog sizes count bytes in bodies which may have gaps. This prefix extent is
            # only an importer hint; it never becomes st_size or a recovered function bound.
            extent = min(max((row['value'] + row['catalog_size'] for row in seeds),
                             default=address + size), address + size)
            text_size = extent - address
            function_sections[index] = add('.text', address, offset, text_size, flags | 4)
            if text_size < size:
                add('.rodata', extent, offset + text_size, size - text_size, flags)
        else:
            add('.data', address, offset, size, flags)
    return sections, function_sections


def append_metadata(original: bytes, loads: list[dict], functions: list[dict]) -> tuple[bytes, list[dict]]:
    sections, function_sections = analysis_sections(loads, functions)
    symbol_index = len(sections) + 1
    string_index = symbol_index + 1
    section_string_index = string_index + 1
    strings = bytearray(b'\0')
    symbols = bytearray(SYMBOL_SIZE)  # null local symbol; all catalog seeds are global
    for row in functions:
        name_offset = len(strings)
        strings.extend(row['name'].encode('utf-8') + b'\0')
        symbols.extend(struct.pack('<IBBHQQ', name_offset, 0x12, 0,
                                   function_sections[row['load_index']], row['value'], 0))
    output = bytearray(original)

    def append(name, data, kind, alignment=1, link=0, info=0, entry_size=0):
        output.extend(b'\0' * (-len(output) % alignment))
        sections.append({'name': name, 'type': kind, 'flags': 0, 'address': 0,
                         'offset': len(output), 'size': len(data), 'link': link, 'info': info,
                         'alignment': alignment, 'entry_size': entry_size})
        output.extend(data)

    append('.symtab', symbols, 2, alignment=8, link=string_index, info=1, entry_size=SYMBOL_SIZE)
    append('.strtab', strings, 3)
    section_names = bytearray(b'\0')
    name_offsets = {}
    for name in [row['name'] for row in sections] + ['.shstrtab']:
        name_offsets[name] = len(section_names)
        section_names.extend(name.encode('ascii') + b'\0')
    append('.shstrtab', section_names, 3)
    output.extend(b'\0' * (-len(output) % 8))
    section_offset = len(output)
    output.extend(bytes(SECTION_HEADER_SIZE))
    for row in sections:
        output.extend(struct.pack('<IIQQQQIIQQ', name_offsets[row['name']], row['type'], row['flags'],
                                  row['address'], row['offset'], row['size'], row['link'], row['info'],
                                  row['alignment'], row['entry_size']))
    if len(sections) + 1 >= 0xff00:
        raise ValueError('too many sections for the supported ELF layout')
    struct.pack_into('<Q', output, 40, section_offset)
    struct.pack_into('<HHH', output, 58, SECTION_HEADER_SIZE, len(sections) + 1, section_string_index)
    return bytes(output), sections


def private_destination(path: Path, root: Path, source: Path) -> Path:
    path = path.resolve()
    if path == root or root in path.parents:
        raise ValueError('analysis output and provenance must be outside the repository')
    if path == source:
        raise ValueError('analysis output and provenance must not replace the original')
    if path.exists():
        raise FileExistsError(f'analysis destination already exists: {path}')
    return path


def create_analysis_image(source: Path, output: Path, provenance: Path, *, repo_root: Path = ROOT) -> dict:
    """Create two exclusive private files after validating the exact pinned image and catalog."""
    root = Path(repo_root).resolve()
    source = Path(source).resolve(strict=True)
    output = private_destination(Path(output), root, source)
    provenance = private_destination(Path(provenance), root, source)
    if output == provenance:
        raise ValueError('analysis output and provenance need distinct paths')
    original = source.read_bytes()
    original_sha = sha256(original)
    if original_sha != pinned_digest(root, 'target.sha256'):
        raise ValueError('source SHA-256 does not match pinned eboot.elf')
    loads, phoff, phsize = parse_loads(original)
    original_loaded = loaded_digest(original, loads)
    if original_loaded != pinned_digest(root, 'target.image.sha256'):
        raise ValueError('source loaded-image SHA-256 does not match the pinned target')
    catalog = (root / 'symbols/ghidra_functions.csv').read_bytes()
    names = (root / 'symbols/functions.csv').read_bytes()
    functions, named = catalog_functions(catalog, names, loads)
    derived, sections = append_metadata(original, loads, functions)
    derived_loaded = loaded_digest(derived, loads)
    preserved = {'loaded_image_equal': original_loaded == derived_loaded,
                 'program_headers_equal': original[phoff:phoff + phsize] == derived[phoff:phoff + phsize],
                 'original_bytes_equal_except_section_header_fields':
                     original[:40] == derived[:40] and original[48:58] == derived[48:58]
                     and original[64:] == derived[64:len(original)],
                 'load_segments_equal': all(
                     original[row['file_offset']:row['file_offset'] + row['file_size']]
                     == derived[row['file_offset']:row['file_offset'] + row['file_size']] for row in loads)}
    if not all(preserved.values()):
        raise ValueError('analysis metadata would change the original load image')
    report = {
        'schema_version': 1, 'kind': 'paleblood_rea_analysis_image',
        'original': {'sha256': original_sha, 'loaded_image_sha256': original_loaded, 'size': len(original)},
        'derived': {'sha256': sha256(derived), 'loaded_image_sha256': derived_loaded, 'size': len(derived)},
        'catalog': {'sha256': sha256(catalog), 'function_symbols': len(functions), 'named_symbols': named,
                    'generated_symbols': len(functions) - named},
        'names': {'sha256': sha256(names)},
        'address_transform': {'catalog_image_base': '0x00400000', 'elf_image_base': '0x00000000',
                              'subtract': CATALOG_BASE,
                              'formula': 'elf_virtual_address = catalog_address - 0x400000'},
        'section_extent_policy': 'catalog_max_start_plus_size_analysis_hint',
        'preservation': preserved,
        'load_segments': [{**row, 'sha256': sha256(original[row['file_offset']:
                                                         row['file_offset'] + row['file_size']])} for row in loads],
        'sections': sections,
        'limitations': [
            'Sections and STT_FUNC names are analyst-supplied analysis hints, not original symbols.',
            'Catalog starts seed analysis; they are not independent function discovery.',
            'Catalog sizes count body bytes and do not prove contiguous function extents; all st_size values are zero.',
            'The .text prefix ends at the catalog maximum start plus size as an importer hint only; code/data bounds remain unverified.',
            'This view establishes no runtime behavior, replacement equivalence or verified-function progress.',
            'The derived file is for static analysis only; use the unmodified pinned ELF for execution and verification.',
        ],
    }
    created = []
    try:
        with output.open('xb') as image:
            created.append(output)
            with provenance.open('xb') as evidence:
                created.append(provenance)
                image.write(derived)
                evidence.write((json.dumps(report, sort_keys=True, indent=2) + '\n').encode())
    except BaseException:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='unmodified exact-target eboot.elf')
    parser.add_argument('--output', type=Path, required=True, help='new private analysis ELF (parent must exist)')
    parser.add_argument('--provenance', type=Path, required=True, help='new private provenance JSON (parent must exist)')
    args = parser.parse_args()
    try:
        report = create_analysis_image(args.source, args.output, args.provenance)
    except (ValueError, OSError, KeyError, struct.error) as error:
        print(f'rea_image: {error}', file=sys.stderr)
        return 2
    print(json.dumps(report, sort_keys=True, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
