#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for input_mouse_b_open_for_initial_user (0x02a8bf30); the cases are in devices_fixtures.py.

usage: mouse_b_open_for_initial_user_cases.py OUT_DIR
"""
import sys

import devices_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write(fixtures.mouse_open_cases('mouse_b'), __doc__))
