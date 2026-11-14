#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_kernel_file_write (0x02a70950); the cases are in kernel_file_fixtures.py.

usage: kernel_file_write_cases.py OUT_DIR
"""
import sys

import kernel_file_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.write_cases(), __doc__))
