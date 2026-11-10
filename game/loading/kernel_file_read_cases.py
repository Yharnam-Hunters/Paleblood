#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_kernel_file_read (0x02a70920); the cases are in kernel_file_fixtures.py.

usage: kernel_file_read_cases.py OUT_DIR
"""
import sys

import kernel_file_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.read_cases(), __doc__))
