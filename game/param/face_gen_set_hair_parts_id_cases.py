#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_face_gen_set_hair_parts_id (0x023189f0); the cases are in face_fixtures.py.

usage: face_gen_set_hair_parts_id_cases.py OUT_DIR
"""
import sys

import face_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_face_gen_set_hair_parts_id', __doc__))
