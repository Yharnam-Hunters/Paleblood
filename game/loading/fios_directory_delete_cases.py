#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_fios_directory_delete (0x0084d5a0); the cases are in fios_fixtures.py.

usage: fios_directory_delete_cases.py OUT_DIR
"""
import sys

import fios_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.delete_cases(), __doc__))
