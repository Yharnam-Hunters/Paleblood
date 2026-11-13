#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List what the renderer's shader translator lacks, over every shader of your own copy of the game
(docs/RENDERER.md, "Coverage survey").

Reads the shader bundles as shader_census.py does (<app0>/dvdroot_ps4/shader/*.shaderbnd*.dcx), but by
their BND4 entries, so that each shader keeps its name and its stage. Each entry is a shader file: a
"Shdr" header (byte 8 the stage, byte 9 the length in dwords of the register block after the header's
16 bytes), the register block (from its third dword the stage's registers, as sceGnmSet*Shader takes
them: SPI_SHADER_PGM_LO/HI, RSRC1, RSRC2, then the stage's own), the GCN code, and the "OrbShdr" block,
whose 24-bit field after its version byte is the code's length in bytes.

The game's executable holds more shader files of its own (the engine's: post-processing, the interface,
tone mapping): --elf (default <data root>/elf/eboot.elf when it exists) is searched for their "Shdr"
headers too, each named by its file and offset.

Each distinct program goes to build/pbgcn_survey with its stage and the settings its registers give;
the translator, in survey mode, names every instruction or feature it cannot translate. A program that
translates is checked with spirv-val (--spirv-val, default the one on PATH; Vulkan 1.2 rules) when
present. It prints counts, reasons and example shader names only: no game code or data leaves your
machine or enters the repository. --report writes the same as Markdown to a file of your choosing,
which must be outside the repository.

usage: shader_survey.py [--app0 DIR] [--elf FILE] [--survey PATH] [--spirv-val PATH] [--jobs N]
                        [--report FILE] [--results FILE] [--only NAME_SUBSTRING]
--results writes each program's names, stage, "OrbShdr" shader hash and reasons as JSON (outside the
repository), to join with other lists of shaders (a capture's, a run's).
"""
import argparse
import collections
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DCX_MAGIC, DCX_DEFLATE = b'DCX\0', b'DFLT'
DCX_COMPRESSED_SIZE_OFFSET, DCX_METHOD_OFFSET, DCX_DATA_OFFSET = 0x20, 0x28, 0x4c
BND4_MAGIC = b'BND4'
SHADER_MAGIC, SHADER_INFO_MAGIC = b'Shdr', b'OrbShdr'
SHADER_FILE_START = b'Shdr\x07\x00'  # the magic and major version 7, as the game's shader files start
SHADER_INFO_HASH_OFFSET = 16  # the 64-bit shader hash in the OrbShdr block
SHADER_HEADER_BYTES = 16
SHADER_INFO_LENGTH_OFFSET = 8  # after the 7-byte magic and the version byte
REGISTERS_FROM = 2             # the register block's dword where the stage's registers start
# the header's stage byte, and a vertex-type file's stage by its extension (a .dpo is a domain shader)
STAGES = {1: 'vertex', 2: 'pixel', 3: 'geometry', 4: 'compute', 5: 'export', 6: 'local', 7: 'hull'}
# words of the stage's registers (as runtime/renderer/render_calls.c decodes them)
PGM_RSRC1, PGM_RSRC2 = 2, 3
VS_OUT_CNTL = 6
PS_Z_FORMAT, PS_INPUT_ENA, PS_INPUT_ADDR, PS_DB_SHADER_CONTROL = 4, 6, 7, 10
CS_NUM_THREAD_X = 4
DEPTH_EXPORTS = 0x1 | 0x6 | 0x100  # DB_SHADER_CONTROL Z, stencil and mask export enables
LDS_UNIT = 512
# the pixel inputs the Vulkan executor draws (render_executor.c's draw()): perspective centre, position x/y, front face
DRAWN_PIXEL_INPUTS = 1 << 1 | 1 << 8 | 1 << 9 | 1 << 12
SURVEY_TIMEOUT = 60


def field(word: int, low: int, bits: int) -> int:
    return (word >> low) & ((1 << bits) - 1)


def default_app0() -> str:
    root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    return os.environ.get('BB_APP0') or os.path.join(root, 'merged')


def unpack_dcx(data: bytes) -> bytes:
    if data[:4] != DCX_MAGIC or data[DCX_METHOD_OFFSET:DCX_METHOD_OFFSET + 4] != DCX_DEFLATE:
        raise ValueError('not a deflate DCX container')
    size = struct.unpack_from('>I', data, DCX_COMPRESSED_SIZE_OFFSET)[0]
    return zlib.decompress(data[DCX_DATA_OFFSET:DCX_DATA_OFFSET + size])


def bnd4_entries(archive: bytes):
    """(name, data) of each file of a BND4 archive: the file count at 0xc, the entry table's offset at
    0x10 and its entry size at 0x20, names UTF-16 when byte 0x30 is set; an entry holds its size at 8,
    its data offset at 0x18 and its name offset in its last four bytes."""
    unicode = archive[0x30] != 0
    count = struct.unpack_from('<I', archive, 0xc)[0]
    table, entry_size = struct.unpack_from('<Q', archive, 0x10)[0], struct.unpack_from('<Q', archive, 0x20)[0]
    for k in range(count):
        entry = table + k * entry_size
        size = struct.unpack_from('<Q', archive, entry + 8)[0]
        offset = struct.unpack_from('<I', archive, entry + 0x18)[0]
        name_at = struct.unpack_from('<I', archive, entry + entry_size - 4)[0]
        if unicode:
            end = name_at
            while archive[end:end + 2] != b'\0\0':
                end += 2
            name = archive[name_at:end].decode('utf-16le')
        else:
            name = archive[name_at:archive.index(b'\0', name_at)].decode('latin-1')
        yield name, archive[offset:offset + size]


def shader_files(app0: str):
    """(bundle, name, data) of every shader file in the bundles, nested archives opened."""
    for path in sorted(glob.glob(os.path.join(app0, 'dvdroot_ps4', 'shader', '*.shaderbnd*.dcx'))):
        pending = [(os.path.basename(path), unpack_dcx(open(path, 'rb').read()), '')]
        while pending:
            bundle, archive, prefix = pending.pop()
            for name, data in bnd4_entries(archive):
                if data[:4] == DCX_MAGIC:
                    data = unpack_dcx(data)
                if data[:4] == BND4_MAGIC:
                    pending.append((bundle, data, prefix + name + '/'))
                else:
                    yield bundle, prefix + name, data


def default_elf() -> str:
    root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    return os.environ.get('BB_ELF') or os.path.join(root, 'elf', 'eboot.elf')


def embedded_shaders(path: str):
    """(file, name, data) of each shader file inside an executable: from each "Shdr" header to the end of its
    OrbShdr block."""
    data = open(path, 'rb').read()
    at = data.find(SHADER_FILE_START)
    while at >= 0:
        if data[at - 3:at] == SHADER_INFO_MAGIC[:3]:  # the "Shdr" inside an OrbShdr block, not a header
            at = data.find(SHADER_FILE_START, at + 1)
            continue
        info = data.find(SHADER_INFO_MAGIC, at)
        if info < 0:
            break
        yield os.path.basename(path), f'{os.path.basename(path)}+{at:#x}', data[at:info + SHADER_INFO_HASH_OFFSET + 8]
        at = data.find(SHADER_FILE_START, at + 1)


def shader_hash(data: bytes) -> int:
    info = data.find(SHADER_INFO_MAGIC)
    return struct.unpack_from('<Q', data, info + SHADER_INFO_HASH_OFFSET)[0] if info >= 0 and info + 24 <= len(data) else 0


def parse_shader(data: bytes):
    """(stage byte, register words, code) of a shader file, or None."""
    at = data.find(SHADER_MAGIC)
    info = data.find(SHADER_INFO_MAGIC, at + 1) if at >= 0 else -1
    if at < 0 or info < 0:
        return None
    stage, header_dwords = data[at + 8], data[at + 9]
    start = at + SHADER_HEADER_BYTES + header_dwords * 4
    length = struct.unpack_from('<I', data, info + SHADER_INFO_LENGTH_OFFSET)[0] >> 8
    if length == 0 or length % 4 or start + length > info:
        return None
    registers = struct.unpack_from('<%dI' % header_dwords, data, at + SHADER_HEADER_BYTES)[REGISTERS_FROM:]
    return stage, registers, data[start:start + length]


def survey_arguments(stage: str, words) -> list:
    """pbgcn_survey's settings from the stage's registers, and the executor's own refusals (reasons
    the draw or dispatch would stop with before translating)."""
    rsrc2 = words[PGM_RSRC2]
    settings, refusals = [f'user={field(rsrc2, 1, 5)}'], []
    if stage == 'compute':
        threads = words[CS_NUM_THREAD_X:CS_NUM_THREAD_X + 3]
        settings += [f'threads={threads[0]},{threads[1]},{threads[2]}', f'groups={field(rsrc2, 7, 3)}',
                     f'lds={field(rsrc2, 15, 9) * LDS_UNIT}']
        if field(rsrc2, 10, 1):
            refusals.append('executor: a compute shader that takes its workgroup\'s size information (TG_SIZE_EN)')
    elif stage == 'pixel':
        enable, address = words[PS_INPUT_ENA], words[PS_INPUT_ADDR]
        settings.append(f'inputs={address:#x}')
        if words[PS_Z_FORMAT]:
            settings.append(f'depth={words[PS_DB_SHADER_CONTROL] & DEPTH_EXPORTS:#x}')
        if enable != address or not enable & 2 or enable & ~DRAWN_PIXEL_INPUTS:
            refusals.append(f'executor: pixel-shader inputs {enable:#x} (address {address:#x}) beyond the drawn ones')
    elif stage in ('vertex', 'domain'):
        settings.append(f'control={words[VS_OUT_CNTL]:#x}')
    elif stage == 'local':
        settings.append(f'lds={field(rsrc2, 7, 9) * LDS_UNIT}')
    elif stage == 'hull' and field(rsrc2, 8, 1):
        refusals.append('executor: a hull shader taking its workgroup\'s size (TG_SIZE_EN)')
    return settings, refusals


NUMBER = re.compile(r'0x[0-9a-fA-F]+|\d+')


def reason_key(reason: str) -> str:
    """A reason with its numbers made N, so that one gap is counted once however its registers fall."""
    return NUMBER.sub('N', reason)


def run_one(survey: str, spirv_val: str, stage: str, settings: list, code: bytes):
    with tempfile.TemporaryDirectory() as scratch:
        spirv_path = os.path.join(scratch, 'shader.spv')
        try:
            run = subprocess.run([survey, stage] + settings + [f'spirv={spirv_path}'], input=code, capture_output=True,
                                 timeout=SURVEY_TIMEOUT)
        except subprocess.TimeoutExpired:
            return ['@0 survey: the translator took longer than %d s' % SURVEY_TIMEOUT], None
        reasons = [line for line in run.stdout.decode(errors='replace').splitlines() if line.strip()]
        if run.returncode == 2:
            reasons.append('@0 survey: ' + run.stderr.decode(errors='replace').strip())
        if run.returncode or not spirv_val or not os.path.exists(spirv_path):
            return reasons, None
        check = subprocess.run([spirv_val, '--target-env', 'vulkan1.2', spirv_path], capture_output=True)
        return reasons, (check.stdout + check.stderr).decode(errors='replace').strip() if check.returncode else ''


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--app0', default=None)
    ap.add_argument('--elf', action='append', default=None, help='an executable to search for shader files (repeatable)')
    ap.add_argument('--results', default=None, help='write each program\'s result as JSON to this file (outside the repository)')
    ap.add_argument('--survey', default=os.path.join(ROOT, 'build', 'pbgcn_survey'))
    ap.add_argument('--spirv-val', default=shutil.which('spirv-val') or '')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--report', default=None, help='also write the list as Markdown to this file (outside the repository)')
    ap.add_argument('--only', default=None, help='survey only the shaders whose names contain this')
    ap.add_argument('--examples', type=int, default=3)
    a = ap.parse_args()
    for output in (a.report, a.results):
        if output and os.path.realpath(output).startswith(os.path.realpath(ROOT) + os.sep):
            print('shader_survey: the report names game shaders: write it outside the repository', file=sys.stderr)
            return 2
    elves = a.elf if a.elf is not None else [path for path in [default_elf()] if os.path.exists(path)]
    if not os.path.exists(a.survey):
        print(f'shader_survey: no {a.survey} (build the pbgcn_survey target)', file=sys.stderr)
        return 2

    programs, names, unreadable, files = {}, collections.defaultdict(list), [], 0
    hashes = {}
    sources = [shader_files(a.app0 or default_app0())] + [embedded_shaders(path) for path in elves]
    for bundle, name, data in (entry for source in sources for entry in source):
        if a.only and a.only not in name:
            continue
        files += 1
        parsed = parse_shader(data)
        if not parsed or parsed[0] not in STAGES:
            unreadable.append(f'{bundle}:{name}')
            continue
        stage = STAGES[parsed[0]]
        if stage == 'vertex' and name.lower().endswith('.dpo'):
            stage = 'domain'
        settings, refusals = survey_arguments(stage, parsed[1])
        key = hashlib.sha1(stage.encode() + b' '.join(s.encode() for s in settings) + parsed[2]).hexdigest()
        programs[key] = (stage, settings, refusals, parsed[2])
        names[key].append(name.replace('\\', '/'))
        hashes.setdefault(key, shader_hash(data))
    if not programs:
        print('shader_survey: no shaders found (set --app0 or BB_APP0)', file=sys.stderr)
        return 2

    gaps = collections.defaultdict(lambda: {'programs': 0, 'files': 0, 'stages': collections.Counter(), 'examples': []})
    by_stage, invalid = collections.Counter(), []
    translated = collections.Counter()
    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        futures = {pool.submit(run_one, a.survey, a.spirv_val, p[0], p[1], p[3]): key for key, p in programs.items()}
        for future in concurrent.futures.as_completed(futures):
            key = futures[future]
            stage, _, refusals, _ = programs[key]
            reasons, validation = future.result()
            by_stage[stage] += 1
            results.append({'names': names[key], 'stage': stage, 'hash': f'{hashes[key]:#018x}', 'reasons': refusals + reasons,
                            'spirv_val': validation})
            seen = set()
            for line in refusals + reasons:
                text = line.split(' ', 1)[1] if line.startswith('@') else line
                gap = reason_key(text)
                if gap in seen:
                    continue
                seen.add(gap)
                g = gaps[gap]
                g['programs'] += 1
                g['files'] += len(names[key])
                g['stages'][stage] += 1
                if len(g['examples']) < a.examples:
                    g['examples'].append(f'{names[key][0]} ({line})')
            if not reasons and not refusals:
                translated[stage] += 1
                if validation:
                    invalid.append((names[key][0], validation.splitlines()[0] if validation else ''))

    lines = [f'# Translator coverage survey', '',
             f'{files} shader files, {len(programs)} distinct programs; {sum(translated.values())} translate in full '
             f'({", ".join(f"{s} {translated[s]}/{by_stage[s]}" for s in sorted(by_stage))}).',
             f'spirv-val: {"not run" if not a.spirv_val else f"{len(invalid)} translated programs fail validation"}.', '',
             '| programs | files | stages | gap | examples |', '|---:|---:|---|---|---|']
    for gap, g in sorted(gaps.items(), key=lambda item: -item[1]['programs']):
        stages = ', '.join(f'{s} {n}' for s, n in g['stages'].most_common())
        examples = '<br>'.join(e.replace('|', '\\|') for e in g['examples'])
        lines.append(f'| {g["programs"]} | {g["files"]} | {stages} | {gap.replace("|", chr(92) + "|")} | {examples} |')
    if invalid:
        lines += ['', '## spirv-val failures', '']
        lines += [f'- {name}: {message}' for name, message in invalid[:50]]
    if unreadable:
        lines += ['', f'{len(unreadable)} files without a readable shader: ' + ', '.join(unreadable[:10])]
    text = '\n'.join(lines) + '\n'
    print(text)
    if a.report:
        with open(a.report, 'w') as out:
            out.write(text)
    if a.results:
        with open(a.results, 'w') as out:
            json.dump(results, out, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
