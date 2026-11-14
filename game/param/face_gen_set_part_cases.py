#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_face_gen_set_part (0x02317cf0); the cases are in face_fixtures.py.

usage: face_gen_set_part_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_face_gen_set_part', __doc__))
