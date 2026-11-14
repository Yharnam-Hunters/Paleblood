#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_return_point_entity (0x02326d60); the cases are in map_fixtures.py.

usage: return_point_entity_cases.py OUT_DIR
"""
import sys

import map_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_return_point_entity', __doc__))
