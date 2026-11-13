#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_face_set_gender (0x0231a6e0); the cases are in face_fixtures.py.

usage: face_set_gender_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_face_set_gender', __doc__))
