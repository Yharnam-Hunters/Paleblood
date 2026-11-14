#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_game_map (0x0231be50); the cases are in map_fixtures.py.

usage: find_row_game_map_cases.py OUT_DIR
"""
import sys

import map_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_game_map', __doc__))
