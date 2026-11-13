#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_item_lot_lvdep (0x023202a0); the cases are in fallback_fixtures.py.

usage: find_row_item_lot_lvdep_cases.py OUT_DIR
"""
import sys

import fallback_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_item_lot_lvdep', __doc__))
