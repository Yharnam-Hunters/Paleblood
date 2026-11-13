#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for menu_gaitem_select_entry_copy_into_pool (0x01edf560); the cases are in gaitem_entry_fixtures.py.

usage: gaitem_select_entry_copy_into_pool_cases.py OUT_DIR
"""
import sys

import gaitem_entry_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('menu_gaitem_select_entry_copy_into_pool', __doc__))
