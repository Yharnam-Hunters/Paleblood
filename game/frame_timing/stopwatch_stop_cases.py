#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_stopwatch_stop (0x01448620); the cases are in stopwatch_fixtures.py.

usage: stopwatch_stop_cases.py OUT_DIR
"""
import sys

import stopwatch_fixtures

if __name__ == '__main__':
    sys.exit(stopwatch_fixtures.main('stop', __doc__))
