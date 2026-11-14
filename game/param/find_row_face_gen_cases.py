#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_find_row_face_gen (0x02317560); the cases are in face_fixtures.py.

usage: find_row_face_gen_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_find_row_face_gen', __doc__))
