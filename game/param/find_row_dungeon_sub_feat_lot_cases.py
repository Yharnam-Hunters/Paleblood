#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_dungeon_sub_feat_lot (0x02316c70); the cases are in find_row_fixtures.py.

usage: find_row_dungeon_sub_feat_lot_cases.py OUT_DIR
"""
import sys

import find_row_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_dungeon_sub_feat_lot', __doc__))
