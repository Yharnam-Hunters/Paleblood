#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_face_set_asymmetry (0x0231ac00); the cases are in face_fixtures.py.

usage: face_set_asymmetry_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_face_set_asymmetry', __doc__))
