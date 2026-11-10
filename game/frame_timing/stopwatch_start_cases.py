#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_stopwatch_start (0x01448580); the cases are in stopwatch_fixtures.py.

usage: stopwatch_start_cases.py OUT_DIR
"""
import sys

import stopwatch_fixtures

if __name__ == '__main__':
    sys.exit(stopwatch_fixtures.main('start', __doc__))
