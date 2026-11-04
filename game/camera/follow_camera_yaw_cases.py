#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the camera yaw getter; no gameplay recordings."""
from follow_camera_pitch_cases import DIRECTIONS, case, write_cases


ADDRESS = '0x0183fb20'
CASES = [case(ADDRESS, cid, direction) for cid, direction in DIRECTIONS]
CASES += [
    case(ADDRESS, 'fallback_nan_payload', (0.0, 9.0, 0.0, 1.0), yaw=0xff812345),
    case(ADDRESS, 'fallback_negative_zero', (-0.0, 9.0, -0.0, 1.0), yaw=0x80000000),
    case(ADDRESS, 'returned_negative_zero', (3.0, 9.0, 4.0, 1.0), result=0x80000000),
    case(ADDRESS, 'returned_nan_payload', (3.0, 9.0, 4.0, 1.0), result=0xff812345),
]


if __name__ == '__main__':
    raise SystemExit(write_cases(CASES))
