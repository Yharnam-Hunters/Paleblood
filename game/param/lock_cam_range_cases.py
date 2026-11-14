#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_lock_cam_range (0x01c92e90); the cases are in fallback_fixtures.py.

usage: lock_cam_range_cases.py OUT_DIR
"""
import sys

import fallback_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_lock_cam_range', __doc__))
