#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_kernel_file_open (0x02a70890); the cases are in kernel_file_fixtures.py.

usage: kernel_file_open_cases.py OUT_DIR
"""
import sys

import kernel_file_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.open_cases(), __doc__))
