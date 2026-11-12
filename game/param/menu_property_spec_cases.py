#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for param_menu_property_spec (0x0206ecc0); the cases are in special_fixtures.py.

usage: menu_property_spec_cases.py OUT_DIR
"""
import sys

import special_fixtures as fixtures

if __name__ == '__main__':
    sys.exit(fixtures.write('param_menu_property_spec', __doc__))
