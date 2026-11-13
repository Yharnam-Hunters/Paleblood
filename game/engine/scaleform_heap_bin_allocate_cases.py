#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_bin_allocate.cpp (layouts in scaleform_heap_cases.py), for
tools/verify.py run.

usage: scaleform_heap_bin_allocate_cases.py OUT_DIR
"""
import sys

from scaleform_heap_cases import main

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main([sys.argv[1], 'bin_allocate'])
