#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_row_count_equip_param_goods (0x0231e610); the cases are in row_iteration_fixtures.py.

usage: row_count_equip_param_goods_cases.py OUT_DIR
"""
import sys

import row_iteration_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_row_count_equip_param_goods', __doc__))
