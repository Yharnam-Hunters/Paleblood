#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_behavior_param (0x023133c0); the cases are in row_search_fixtures.py.

usage: find_row_behavior_param_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_behavior_param', __doc__))
