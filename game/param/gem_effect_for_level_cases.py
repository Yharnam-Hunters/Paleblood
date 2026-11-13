#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_gem_effect_for_level (0x0231d330); the cases are in row_search_fixtures.py.

usage: gem_effect_for_level_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_gem_effect_for_level', __doc__))
