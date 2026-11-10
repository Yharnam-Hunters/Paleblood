#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for input_pad_set_vibration (0x02a9c790); the cases are in devices_fixtures.py.

usage: pad_set_vibration_cases.py OUT_DIR
"""
import sys

import devices_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.pad_cases(), __doc__))
