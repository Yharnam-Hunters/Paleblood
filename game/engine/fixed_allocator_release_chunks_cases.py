#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/fixed_allocator_release_chunks.cpp (layouts in fixed_allocator_release_cases.py),
for tools/verify.py run.

usage: fixed_allocator_release_chunks_cases.py OUT_DIR
"""
import sys

from fixed_allocator_release_cases import main

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main([sys.argv[1], 'release_chunks'])
