#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_time_of_day_us (0x02e61140); the cases are in time_of_day_fixtures.py.

usage: time_of_day_us_cases.py OUT_DIR
"""
import sys

import time_of_day_fixtures

if __name__ == '__main__':
    sys.exit(time_of_day_fixtures.main('frame_timing_time_of_day_us', __doc__))
