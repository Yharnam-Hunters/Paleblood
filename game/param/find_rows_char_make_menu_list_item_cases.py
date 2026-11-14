#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_rows_char_make_menu_list_item (0x01f83130); the cases are in special_fixtures.py.

usage: find_rows_char_make_menu_list_item_cases.py OUT_DIR
"""
import sys

import special_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_rows_char_make_menu_list_item', __doc__))
