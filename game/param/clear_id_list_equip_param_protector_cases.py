#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_clear_id_list_equip_param_protector (0x023255c0); the cases are in row_iteration_fixtures.py.

usage: clear_id_list_equip_param_protector_cases.py OUT_DIR
"""
import sys

import row_iteration_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_clear_id_list_equip_param_protector', __doc__))
