#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_time_seed_words (0x02e61280); the cases are in time_of_day_fixtures.py.

usage: time_seed_words_cases.py OUT_DIR
"""
import sys

import time_of_day_fixtures

if __name__ == '__main__':
    sys.exit(time_of_day_fixtures.main('frame_timing_time_seed_words', __doc__))
