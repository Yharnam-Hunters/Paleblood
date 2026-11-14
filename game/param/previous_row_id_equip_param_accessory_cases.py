#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_previous_row_id_equip_param_accessory (0x023121b0); the cases are in row_iteration_fixtures.py.

usage: previous_row_id_equip_param_accessory_cases.py OUT_DIR
"""
import sys

import row_iteration_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_previous_row_id_equip_param_accessory', __doc__))
