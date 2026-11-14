#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_next_row_id_magic (0x02321680); the cases are in row_iteration_fixtures.py.

usage: next_row_id_magic_cases.py OUT_DIR
"""
import sys

import row_iteration_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_next_row_id_magic', __doc__))
