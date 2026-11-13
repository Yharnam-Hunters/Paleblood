#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for menu_gaitem_status_entry_copy (0x01f1bca0); the cases are in gaitem_entry_fixtures.py.

usage: gaitem_status_entry_copy_cases.py OUT_DIR
"""
import sys

import gaitem_entry_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('menu_gaitem_status_entry_copy', __doc__))
