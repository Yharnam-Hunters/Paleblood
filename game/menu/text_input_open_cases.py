#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for menu_text_input_open (0x024162a0); the cases are in text_input_fixtures.py.

usage: text_input_open_cases.py OUT_DIR
"""
import sys

import text_input_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('menu_text_input_open', __doc__))
