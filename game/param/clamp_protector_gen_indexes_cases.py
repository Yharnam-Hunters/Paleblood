#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_clamp_protector_gen_indexes (0x023235b0); the cases are in row_search_fixtures.py.

usage: clamp_protector_gen_indexes_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_clamp_protector_gen_indexes', __doc__))
