#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_check_set_equip_param_weapon (0x0232bce0); the cases are in special_fixtures.py.

usage: check_set_equip_param_weapon_cases.py OUT_DIR
"""
import sys

import special_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_check_set_equip_param_weapon', __doc__))
