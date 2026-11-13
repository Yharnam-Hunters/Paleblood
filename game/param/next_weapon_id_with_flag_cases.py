#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_next_weapon_id_with_flag (0x0232b6d0); the cases are in special_fixtures.py.

usage: next_weapon_id_with_flag_cases.py OUT_DIR
"""
import sys

import special_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_next_weapon_id_with_flag', __doc__))
