#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate symbols/functions.csv, symbols/ghidra_functions.csv and game/hooks.csv.

usage: validate_functions.py [--root DIR]
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR = re.compile(r'^0x[0-9a-f]{8}$')
IMAGE_BASE = 0x00400000
NAME = re.compile(r'^[a-z][a-z0-9]*(_[a-z0-9]+)+$')
STATUSES = ('original', 'replaced', 'verified')
FUNC_HEADER = ['address', 'size', 'name', 'system', 'status', 'notes']
EXPORT_HEADER = ['address', 'size']
HOOK_HEADER = ['address', 'replacement', 'system']
MAX_NOTE = 200


def load(path: str, header: list[str], errs: list[str]) -> list[dict]:
    rel = os.path.basename(os.path.dirname(path)) + '/' + os.path.basename(path)
    if not os.path.isfile(path):
        errs.append(f'{rel}: missing')
        return []
    with open(path, newline='') as f:
        rd = csv.reader(f)
        head = next(rd, None)
        if head != header:
            errs.append(f'{rel}: header must be {",".join(header)}')
            return []
        rows = []
        for n, r in enumerate(rd, 2):
            if len(r) != len(header):
                errs.append(f'{rel}:{n}: expected {len(header)} columns, got {len(r)}')
                continue
            d = dict(zip(header, r))
            d['_line'] = n
            rows.append(d)
    return rows


def check_sorted_unique(rows: list[dict], rel: str, errs: list[str]) -> None:
    prev = None
    for r in rows:
        if not ADDR.match(r['address']):
            errs.append(f"{rel}:{r['_line']}: address '{r['address']}' must be 0x + 8 lowercase hex digits")
            continue
        if int(r['address'], 16) < IMAGE_BASE:
            errs.append(f"{rel}:{r['_line']}: address {r['address']} is below the image base "
                        f"0x{IMAGE_BASE:08x}; addresses are PS4 virtual addresses (QUIRKS.md)")
            continue
        if prev is not None and r['address'] <= prev:
            errs.append(f"{rel}:{r['_line']}: addresses must be unique and ascending")
        prev = r['address']


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=DEFAULT_ROOT)
    root = ap.parse_args().root
    errs: list[str] = []

    export = load(os.path.join(root, 'symbols', 'ghidra_functions.csv'), EXPORT_HEADER, errs)
    check_sorted_unique(export, 'symbols/ghidra_functions.csv', errs)
    exp_size: dict[str, int] = {}
    end = -1
    for r in export:
        if not r['size'].isdigit() or int(r['size']) < 1:
            errs.append(f"symbols/ghidra_functions.csv:{r['_line']}: size must be a positive integer")
            continue
        if not ADDR.match(r['address']):
            continue
        a, s = int(r['address'], 16), int(r['size'])
        if a < end:
            errs.append(f"symbols/ghidra_functions.csv:{r['_line']}: overlaps the previous function")
        end = max(end, a + s)
        exp_size[r['address']] = s

    systems_dir = os.path.join(root, 'game')
    systems = sorted(d for d in os.listdir(systems_dir)
                     if os.path.isdir(os.path.join(systems_dir, d))) if os.path.isdir(systems_dir) else []

    funcs = load(os.path.join(root, 'symbols', 'functions.csv'), FUNC_HEADER, errs)
    check_sorted_unique(funcs, 'symbols/functions.csv', errs)
    names: set[str] = set()
    by_addr: dict[str, dict] = {}
    for r in funcs:
        where = f"symbols/functions.csv:{r['_line']}"
        if not r['size'].isdigit() or int(r['size']) < 1:
            errs.append(f'{where}: size must be a positive integer')
        if r['system'] not in systems:
            errs.append(f"{where}: system '{r['system']}' has no game/{r['system']}/ directory")
        if not NAME.match(r['name']):
            errs.append(f"{where}: name '{r['name']}' must be snake_case with a system prefix")
        elif not r['name'].startswith(r['system'] + '_'):
            errs.append(f"{where}: name '{r['name']}' must start with '{r['system']}_'")
        if r['name'] in names:
            errs.append(f"{where}: duplicate name '{r['name']}'")
        names.add(r['name'])
        if r['status'] not in STATUSES:
            errs.append(f"{where}: status '{r['status']}' must be one of {'|'.join(STATUSES)}")
        if len(r['notes']) > MAX_NOTE:
            errs.append(f'{where}: notes longer than {MAX_NOTE} characters (put detail in the issue or docs/systems/)')
        if export:
            if r['address'] not in exp_size:
                errs.append(f"{where}: {r['address']} is not a function in the Ghidra export")
            elif r['size'].isdigit() and int(r['size']) != exp_size[r['address']]:
                errs.append(f"{where}: size {r['size']} differs from the export ({exp_size[r['address']]})")
        by_addr[r['address']] = r

    hooks = load(os.path.join(root, 'game', 'hooks.csv'), HOOK_HEADER, errs)
    check_sorted_unique(hooks, 'game/hooks.csv', errs)
    hooked: set[str] = set()
    for h in hooks:
        where = f"game/hooks.csv:{h['_line']}"
        f = by_addr.get(h['address'])
        if f is None:
            errs.append(f'{where}: no row in symbols/functions.csv for {h["address"]}')
            continue
        hooked.add(h['address'])
        if f['status'] not in ('replaced', 'verified'):
            errs.append(f"{where}: {f['name']} is '{f['status']}', only replaced or verified functions are hooked")
        if h['system'] != f['system']:
            errs.append(f"{where}: system '{h['system']}' differs from functions.csv ('{f['system']}')")
        if f['size'].isdigit() and int(f['size']) < 14:
            errs.append(f"{where}: {f['name']} is {f['size']} bytes; a hook needs at least 14 (QUIRKS.md)")
        if h['replacement'] != 'bb_' + f['name']:
            errs.append(f"{where}: replacement must be 'bb_{f['name']}'")
    for r in funcs:
        if r['status'] in ('replaced', 'verified') and r['address'] not in hooked:
            errs.append(f"symbols/functions.csv:{r['_line']}: {r['name']} is {r['status']} but has no game/hooks.csv entry")

    for e in errs:
        print(f'validate: {e}', file=sys.stderr)
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
