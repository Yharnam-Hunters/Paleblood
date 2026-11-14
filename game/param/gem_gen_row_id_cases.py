#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_gem_gen_row_id (0x0231dbe0); the cases are in row_search_fixtures.py.

usage: gem_gen_row_id_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_gem_gen_row_id', __doc__))
