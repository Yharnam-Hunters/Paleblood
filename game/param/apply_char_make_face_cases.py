#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_apply_char_make_face (0x018e4110); the cases are in face_fixtures.py.

usage: apply_char_make_face_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_apply_char_make_face', __doc__))
