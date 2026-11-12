#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_equip_param_protector (0x02323840); the cases are in special_fixtures.py.

usage: find_row_equip_param_protector_cases.py OUT_DIR
"""
import sys

import special_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_equip_param_protector', __doc__))
