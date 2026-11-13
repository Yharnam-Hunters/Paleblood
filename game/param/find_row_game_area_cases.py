#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_game_area (0x0231b4c0); the cases are in fallback_fixtures.py.

usage: find_row_game_area_cases.py OUT_DIR
"""
import sys

import fallback_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_game_area', __doc__))
