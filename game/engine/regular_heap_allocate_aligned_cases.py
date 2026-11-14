#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/regular_heap_allocate_aligned.cpp (layouts in regular_heap_allocation_cases.py),
for tools/verify.py run.

usage: regular_heap_allocate_aligned_cases.py OUT_DIR
"""
import sys

from regular_heap_allocation_cases import main

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main([sys.argv[1], 'allocate_aligned'])
