#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_fios_open_file (0x00857cd0); the cases are in fios_fixtures.py.

usage: fios_open_file_cases.py OUT_DIR
"""
import sys

import fios_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.open_cases('open'), __doc__))
