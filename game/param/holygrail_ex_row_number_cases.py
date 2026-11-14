#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_holygrail_ex_row_number (0x0231ff70); the cases are in row_search_fixtures.py.

usage: holygrail_ex_row_number_cases.py OUT_DIR
"""
import sys

import row_search_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_holygrail_ex_row_number', __doc__))
