#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_gem_drop_modify (0x0231cd30); the cases are in find_row_fixtures.py.

usage: find_row_gem_drop_modify_cases.py OUT_DIR
"""
import sys

import find_row_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_gem_drop_modify', __doc__))
