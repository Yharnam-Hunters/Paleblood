#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for input_mouse_construct (0x02a8c210); the cases are in devices_fixtures.py.

usage: mouse_construct_cases.py OUT_DIR
"""
import sys

import devices_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.construct_cases(), __doc__))
