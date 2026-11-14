#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_menu_property_layout_row (0x020648f0); the cases are in fallback_fixtures.py.

usage: menu_property_layout_row_cases.py OUT_DIR
"""
import sys

import fallback_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_menu_property_layout_row', __doc__))
