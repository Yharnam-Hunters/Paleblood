#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay cases recorded by tracing (test_capture_trace or test_capture_hooks --emit) through
tools/verify.py.

Each synthetic guest function is traced once, as the runtime would trace a hooked original; the
recorded case must then pass the ordinary verifier, original against a native replacement, and a
replacement with one deliberate change per function must fail it."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness
import ps4elf
import verify

FUNCTIONS = {  # name: (address, replacement symbol)
    'capture_trace_scale': ('0x00400000', 'scale'),
    'capture_trace_stack_args': ('0x00400040', 'stack_args'),
    'capture_trace_scale_float': ('0x00400080', 'scale_float'),
    'capture_trace_twice': ('0x004000c0', 'twice'),
    'capture_trace_copy': ('0x00400140', 'copy'),
    'capture_trace_use_import': ('0x004002c0', 'use_import'),
}

REPLACEMENT = r'''#include <stdint.h>
#include <string.h>
typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;
                 int (*install_hook)(uint64_t, uint64_t, const void *); } host_t;
static unsigned char *image;
int bbgame_init(const host_t *host) { image = host->image; return 0; }
typedef struct { int32_t pad, value; } Inner;
typedef struct { int64_t pad; Inner *inner; } Outer;
#ifndef BROKEN
#define BROKEN 0
#endif
int32_t scale(int32_t *out, const Outer *s, int32_t factor)
{ int32_t global; memcpy(&global, image + 0x1000, 4); *out = s->inner->value * factor + global + BROKEN; return *out; }
int64_t stack_args(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, int64_t g7, int64_t g8)
{ (void)b; (void)c; (void)d; (void)e; (void)f; return g7 * 10 + g8 + a + BROKEN; }
float scale_float(float x, const float *p) { return x * *p + (float)BROKEN; }
static int32_t increment(int32_t *p) { return ++*p; }
int32_t twice(int32_t *p) { increment(p); return increment(p) + BROKEN; }
void copy(char *dst, const char *src, size_t n) { memcpy(dst, src, n); if (BROKEN) dst[n - 1] ^= 1; }
int32_t use_import(int32_t *out)
{ int32_t (*fill)(int32_t *, int32_t); memcpy(&fill, image + 0x1800, sizeof fill); return fill(out, 7) + *out + BROKEN; }
'''


def compile_library(root: str, name: str, broken: bool) -> str:
    source = os.path.join(root, name + '.c')
    with open(source, 'w', encoding='utf-8') as stream:
        stream.write(REPLACEMENT)
    library = os.path.join(root, name + '.so')
    subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', *(['-DBROKEN=1'] if broken else []),
                    '-o', library, source], check=True)
    return library


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit('usage: capture_trace_integration.py EMITTER [FUNCTION ...]')
    expected = sys.argv[2:] or list(FUNCTIONS)
    with tempfile.TemporaryDirectory(prefix='pb-capture-trace-') as root:
        captures = os.path.join(root, 'captures')
        os.mkdir(captures)
        image_path = os.path.join(root, 'image.bin')
        made = subprocess.run([sys.argv[1], '--image', image_path], capture_output=True, text=True)
        if 'built without Capstone' in made.stdout:
            print('capture trace replay: built without Capstone; skipped')
            return 0
        assert made.returncode == 0, made.stderr
        with open(image_path, 'rb') as stream:
            image = stream.read()
        boot = os.path.join(root, 'synthetic.elf')
        with open(boot, 'wb') as stream:
            # The import's slot is a jump slot named for its NID, as in an executable: the verifier
            # binds its scripted stub there.
            stream.write(ps4elf.build(segments=[(0, image[:0x1000], 0x1000, 5), (0x1000, image[0x1000:], 0x1000, 6)],
                                      symbols=[(harness.nid('testFill') + '#A#A', ps4elf.STT_FUNC, None)],
                                      relocations=[], jump_slots=[(0x1800, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)]))
        # The recording run finds the import's slot from the executable and names it from this
        # list, as a game run does from the eboot and symbols/imports.csv.
        names = os.path.join(root, 'imports.csv')
        with open(names, 'w', encoding='utf-8') as stream:
            stream.write('importer,library,symbol,kind,provider\neboot,libTest,testFill,function,\n')
        emitted = subprocess.run([sys.argv[1], '--emit', captures], capture_output=True, text=True,
                                 env={**os.environ, 'BB_ELF': boot, 'BB_CAPTURE_IMPORTS': names})
        assert emitted.returncode == 0, emitted.stderr
        good = compile_library(root, 'replacement', broken=False)
        bad = compile_library(root, 'broken', broken=True)
        for name in expected:
            address, symbol = FUNCTIONS[name]
            cases = os.path.join(captures, name)
            for path in os.listdir(cases):
                with open(os.path.join(cases, path), encoding='utf-8') as stream:
                    case = json.load(stream)
                assert case['capture']['function'] == name, case
                assert case['address'] == address, case
            sides = verify.run_cases(boot, cases, address, symbol, good, function=name)
            report = verify.compare({'schema': 1, 'address': address, 'cases': sides['original']},
                                    {'schema': 1, 'address': address, 'cases': sides['replacement']})
            assert report['cases'] >= 1 and report['failed'] == 0, (name, report)
            broken = verify.run_cases(boot, cases, address, symbol, bad, function=name)
            mismatch = verify.compare({'schema': 1, 'address': address, 'cases': sides['original']},
                                      {'schema': 1, 'address': address, 'cases': broken['replacement']})
            assert mismatch['failed'] == mismatch['cases'], (name, mismatch)
        print(f'capture trace replay: {len(expected)}/{len(expected)} traced functions passed; '
              'every deliberate change detected')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
