#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_return_points_of_map (0x02326e60); the cases are in map_fixtures.py.

usage: return_points_of_map_cases.py OUT_DIR
"""
import sys

import map_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_return_points_of_map', __doc__))
