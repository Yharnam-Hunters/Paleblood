#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/segregated_regular_heap_allocate_top.cpp (layouts in
segregated_regular_heap_cases.py), for tools/verify.py run.

usage: segregated_regular_heap_allocate_top_cases.py OUT_DIR
"""
import sys

from segregated_regular_heap_cases import main

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main([sys.argv[1], 'allocate_top'])
