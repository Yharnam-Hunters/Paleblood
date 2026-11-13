#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_clear_id_list_equip_param_weapon (0x0232bc80); the cases are in row_iteration_fixtures.py.

usage: clear_id_list_equip_param_weapon_cases.py OUT_DIR
"""
import sys

import row_iteration_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_clear_id_list_equip_param_weapon', __doc__))
