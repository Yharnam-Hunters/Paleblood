#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay a typed synthetic capture through the ordinary differential verifier."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ps4elf
import verify


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit('usage: capture_poc_integration.py CAPTURE_POC_EXECUTABLE')
    with tempfile.TemporaryDirectory(prefix='pb-capture-poc-') as root:
        emitted = subprocess.run([sys.argv[1], '--emit'], check=True, capture_output=True, text=True)
        case = json.loads(emitted.stdout)
        assert case['schema'] == 1
        assert case['capture'] == {'function': 'capture_poc_fixture', 'run_id': 'synthetic_run_1'}
        assert case['args'] == {'rdi': 'buf:out', 'rsi': 'buf:input', 'rdx': '0x00000006'}
        assert case['buffers']['input']['bytes'] == '07000000'
        assert case['buffers']['out']['bytes'] == '44332211'

        boot = os.path.join(root, 'synthetic.elf')
        code = bytes.fromhex('8b06 0fafc2 8907 c3')  # *out = *input * factor; return *out
        image = code + bytes(0x1000 - len(code))
        with open(boot, 'wb') as stream:
            stream.write(ps4elf.build(segments=[(0, image, len(image), 5)], symbols=[],
                                      relocations=[], jump_slots=[]))

        cases = os.path.join(root, 'cases')
        os.mkdir(cases)
        with open(os.path.join(cases, 'typed_fixture.json'), 'w', encoding='utf-8') as stream:
            json.dump(case, stream, sort_keys=True)
        source = os.path.join(root, 'replacement.c')
        with open(source, 'w', encoding='utf-8') as stream:
            stream.write(r'''#include <stdint.h>
typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;
                 int (*install_hook)(uint64_t, uint64_t, const void *); } host_t;
int bbgame_init(const host_t *host) { (void)host; return 0; }
int32_t replacement(int32_t *out, const int32_t *input, int32_t factor)
{ *out = *input * factor; return *out; }
''')
        library = os.path.join(root, 'replacement.so')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', library, source], check=True)

        sides = verify.run_cases(boot, cases, '0x00400000', 'replacement', library,
                                 function='capture_poc_fixture')
        report = verify.compare(
            {'schema': 1, 'address': '0x00400000', 'cases': sides['original']},
            {'schema': 1, 'address': '0x00400000', 'cases': sides['replacement']})
        assert (report['cases'], report['passed'], report['failed']) == (1, 1, 0), report

        # A changed native result must be detected by the same comparison path.
        broken_library = os.path.join(root, 'broken.so')
        broken_source = os.path.join(root, 'broken.c')
        with open(broken_source, 'w', encoding='utf-8') as stream:
            stream.write(r'''#include <stdint.h>
typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;
                 int (*install_hook)(uint64_t, uint64_t, const void *); } host_t;
int bbgame_init(const host_t *host) { (void)host; return 0; }
int32_t replacement(int32_t *out, const int32_t *input, int32_t factor)
{ *out = *input * factor + 1; return *out; }
''')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', broken_library, broken_source], check=True)
        broken = verify.run_cases(boot, cases, '0x00400000', 'replacement', broken_library,
                                 function='capture_poc_fixture')
        mismatch = verify.compare(
            {'schema': 1, 'address': '0x00400000', 'cases': sides['original']},
            {'schema': 1, 'address': '0x00400000', 'cases': broken['replacement']})
        assert mismatch['failed'] == 1, mismatch
        print('capture POC replay: 1/1 synthetic case passed; deliberate output mutation detected')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
