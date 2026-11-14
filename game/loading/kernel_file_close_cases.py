#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_kernel_file_close (0x02a709c0); the cases are in kernel_file_fixtures.py.

usage: kernel_file_close_cases.py OUT_DIR
"""
import sys

import kernel_file_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.close_cases(), __doc__))
