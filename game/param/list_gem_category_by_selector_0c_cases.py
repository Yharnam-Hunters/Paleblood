#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_list_gem_category_by_selector_0c (0x0231c380); the cases are in row_search_fixtures.py.

usage: list_gem_category_by_selector_0c_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_list_gem_category_by_selector_0c', __doc__))
