#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_allocate_aligned.cpp, for tools/verify.py run (the
layout and scripted calls of scaleform_heap_allocate_cases.py, with the bins' aligned allocation in
place of the plain one).

usage: scaleform_heap_allocate_aligned_cases.py OUT_DIR
"""
import json
import os
import sys

from scaleform_heap_allocate_cases import LOCK, UNLOCK, add_page, bin_hit, case, large

ADDRESS = '0x0233ca70'
BIN_ALIGNED = '0x0233ae40'


def aligned_hit(below=True, above=True):
    stub = bin_hit(below, above)
    stub.update({'address': BIN_ALIGNED, 'argc': 4})
    for write in stub['writes']:
        write['arg'] = 3
    return stub


ALIGNED_MISS = {'address': BIN_ALIGNED, 'argc': 4, 'ret': 0}


def c(cid, request, alignment, locked, stubs, imports=()):
    out = case(cid, request, locked, stubs, imports=imports)
    out['address'] = ADDRESS
    out['args'] = {'rdi': 'buf:eng', 'rsi': request, 'rdx': alignment, 'rcx': 'buf:info', 'r8': locked}
    return out


def cases():
    pair = [LOCK, UNLOCK]
    return [
        c('small_hit', 0x30, 0x40, 0, [aligned_hit()]),
        c('small_hit_rounded', 0x31, 0x100, 0, [aligned_hit(above=False)]),
        c('small_alignment_raised_to_16', 0x20, 8, 0, [aligned_hit(below=False)]),
        c('small_alignment_16', 0x20, 0x10, 0, [aligned_hit(False, False)]),
        c('small_alignment_15', 0x20, 0xf, 0, [aligned_hit()]),
        c('small_largest', 0x200, 0x20, 0, [aligned_hit()]),
        c('small_request_zero', 0, 0x20, 0, [aligned_hit()]),
        c('small_miss_page_added', 0x40, 0x40, 0, [ALIGNED_MISS, add_page(True), aligned_hit()]),
        c('small_miss_no_retry', 0x40, 0x40, 0, [ALIGNED_MISS, add_page(False)]),
        c('small_locked_page_added', 0x40, 0x40, 1, [ALIGNED_MISS, add_page(True, True), aligned_hit()]),
        c('small_locked_no_retry', 0x40, 0x40, 1, [ALIGNED_MISS, add_page(False, True)]),
        c('large_unlocked', 0x201, 0x40, 0, [large(True)], imports=pair),
        c('large_rounded_to_8', 0x203, 4, 0, [large(True)], imports=pair),
        c('large_alignment_7', 0x300, 7, 0, [large(True)], imports=pair),
        c('large_alignment_8', 0x300, 8, 0, [large(True)], imports=pair),
        c('large_request_below_alignment', 0x300, 0x1000, 0, [large(True)], imports=pair),
        c('large_unlocked_retry', 0x400, 0x40, 0, [large(False, True), large(True)], imports=pair),
        c('large_unlocked_fails', 0x400, 0x40, 0, [large(False, True), large(False, False)], imports=pair),
        c('large_locked', 0x400, 0x40, 1, [large(True)]),
        c('large_locked_retry', 0x400, 0x40, 1, [large(False, True), large(True)]),
        c('large_locked_fails', 0x400, 0x40, 1, [large(False, False)]),
        c('locked_flag_byte', 0x400, 0x40, 0x100, [large(True)], imports=pair),
    ]


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    all_cases = cases()
    for one in all_cases:
        with open(os.path.join(out, f"{one['id']}.json"), 'w') as f:
            json.dump(one, f, indent=1)
    print(f'{len(all_cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
