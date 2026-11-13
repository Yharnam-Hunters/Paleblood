#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_face_set_body_scale (0x0231a480); the cases are in face_fixtures.py.

usage: face_set_body_scale_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_face_set_body_scale', __doc__))
