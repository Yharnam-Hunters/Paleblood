#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_check_repository_for_id (0x0231ad20); the cases are in fallback_fixtures.py.

usage: check_repository_for_id_cases.py OUT_DIR
"""
import sys

import fallback_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_check_repository_for_id', __doc__))
