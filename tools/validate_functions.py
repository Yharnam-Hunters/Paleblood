#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate symbols/functions.csv, symbols/ghidra_functions.csv, game/hooks.csv and the evidence tables
(symbols/reviews.csv, verification.csv, matching.csv, families).

usage: validate_functions.py [--root DIR]
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys

import families as fam
import family_emit
import match
import verification

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR = re.compile(r'^0x[0-9a-f]{8}$')
IMAGE_BASE = 0x00400000
NAME = re.compile(r'^[a-z][a-z0-9]*(_[a-z0-9]+)+$')
STATUSES = ('original', 'replaced', 'edge-verified', 'verified')
HOOKED = ('replaced', 'edge-verified', 'verified')
FUNC_HEADER = ['address', 'size', 'name', 'system', 'status', 'notes']
EXPORT_HEADER = ['address', 'size']
HOOK_HEADER = ['address', 'replacement', 'system', 'returns']
# Return classes a recorded case can compare (docs/VERIFY.md), and the C/C++ return types of each.
RETURN_CLASSES = {'void': {'void'}, 'i8': {'bool', 'int8_t', 'uint8_t', 'char'},
                  'i32': {'int', 'unsigned', 'int32_t', 'uint32_t'},
                  'i64': {'int64_t', 'uint64_t', 'size_t', 'intptr_t', 'uintptr_t', 'pointer'},
                  'i128': {'__int128', 'unsigned __int128'}, 'f32': {'float'}, 'f64': {'double'}}
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
        if f['status'] not in HOOKED:
            errs.append(f"{where}: {f['name']} is '{f['status']}', only replaced, edge-verified or verified functions are hooked")
        if h['system'] != f['system']:
            errs.append(f"{where}: system '{h['system']}' differs from functions.csv ('{f['system']}')")
        if f['size'].isdigit() and int(f['size']) < 14:
            errs.append(f"{where}: {f['name']} is {f['size']} bytes; a hook needs at least 14 (QUIRKS.md)")
        if h['replacement'] != 'bb_' + f['name']:
            errs.append(f"{where}: replacement must be 'bb_{f['name']}'")
        elif h['returns'] not in RETURN_CLASSES:
            errs.append(f"{where}: returns '{h['returns']}' is not one of {', '.join(sorted(RETURN_CLASSES))}")
        else:
            declared = replacement_return_type(root, h['replacement'])
            if declared is not None and declared not in RETURN_CLASSES[h['returns']]:
                errs.append(f"{where}: returns '{h['returns']}' but {h['replacement']} returns '{declared}'")
    for r in funcs:
        if r['status'] in HOOKED and r['address'] not in hooked:
            errs.append(f"symbols/functions.csv:{r['_line']}: {r['name']} is {r['status']} but has no game/hooks.csv entry")

    errs += check_reviews(root, funcs)
    errs += verification.check(root, funcs)
    errs += match.check(root, funcs)
    errs += check_families(root, export, set(by_addr), {h['address'] for h in hooks}, systems)

    for e in errs:
        print(f'validate: {e}', file=sys.stderr)
    return 1 if errs else 0


REVIEW_HEADER = ['address', 'name', 'date', 'reviewer', 'verdict', 'notes']
EXTERN_C = re.compile(r'extern "C"\s+([\w ]+?)\s*(\*?)\s*\b(bb_\w+)\s*\(')
FIELD_LIMIT = {'u8': 0xff, 'd8': 0x7f, 'u32': 0xffffffff}


def replacement_definitions(root: str, system: str) -> dict[str, str]:
    """bb_ symbol -> declared return type ('pointer' for any pointer) for every extern "C"
    definition under game/<system>/."""
    out = {}
    for dp, _, fns in os.walk(os.path.join(root, 'game', system)):
        for fn in fns:
            if fn.endswith(('.c', '.cc', '.cpp')):
                with open(os.path.join(dp, fn), errors='replace') as f:
                    for m in EXTERN_C.finditer(f.read()):
                        out[m.group(3)] = 'pointer' if m.group(2) else ' '.join(m.group(1).split()).replace('const ', '')
    return out


def check_families(root: str, export: list[dict], named: set[str], hooked: set[str], systems: list[str]) -> list[str]:
    """symbols/families.csv and its member and verification tables (tools/families.py)."""
    errs: list[str] = []
    try:
        rows = fam.families(root)
    except ValueError as e:
        return [str(e)]
    order = [r['address'] for r in export if ADDR.match(r['address'])]
    next_start = {a: int(b, 16) for a, b in zip(order, order[1:])}
    size_of = {r['address']: r['size'] for r in export}
    reviews = {}
    review_path = os.path.join(root, 'symbols', 'reviews.csv')
    if os.path.isfile(review_path):
        with open(review_path, newline='') as f:
            reviews = {r['address']: r for r in csv.DictReader(f)}
    import family_scan
    in_source = family_scan.source_addresses(root)
    seen_families: set[str] = set()
    seen_members: set[str] = set()
    for family in rows:
        where = f"symbols/families.csv:{family['_line']}"
        name = family['family']
        if not fam.FAMILY_NAME.match(name) or name in seen_families:
            errs.append(f"{where}: family '{name}' must be a unique snake_case name")
            continue
        seen_families.add(name)
        if family['system'] not in systems:
            errs.append(f"{where}: system '{family['system']}' has no game/{family['system']}/ directory")
            continue
        try:
            shape = fam.parse_shape(family['shape'])
        except fam.ShapeError as e:
            errs.append(f'{where}: {e}')
            continue
        if family['returns'] not in RETURN_CLASSES:
            errs.append(f"{where}: returns '{family['returns']}' is not one of {', '.join(sorted(RETURN_CLASSES))}")
            continue
        if not (family['members'].isdigit() and family['bytes'].isdigit()):
            errs.append(f'{where}: members and bytes must be numbers')
            continue
        if len(family['notes']) > MAX_NOTE:
            errs.append(f'{where}: notes longer than {MAX_NOTE} characters')
        rel = f'symbols/families/{name}.csv'
        try:
            members = fam.members(root, name)
        except ValueError as e:
            errs.append(str(e))
            continue
        if not members:
            errs.append(f'{where}: {rel} is missing or empty')
            continue
        check_sorted_unique(members, rel, errs)
        defined = replacement_definitions(root, family['system'])
        try:
            results = fam.read_table(fam.verification_path(root, name), verification.VERIFICATION_HEADER)
        except ValueError as e:
            errs.append(str(e))
            results = []
        current = fam.source_hash(root, family)
        seen_results: set[tuple] = set()
        for r in results:
            key = (r['function'], r['cases'], r['option'])
            if key in seen_results:
                errs.append(f"symbols/families/{name}.verification.csv:{r['_line']}: more than one "
                            f"{r['cases']} row for {r['function']}")
                break
            seen_results.add(key)
        edge_ok = {r['function'] for r in results if r['cases'] == 'edge' and r['option'] == ''
                   and r['source'] == current and r['total'].isdigit() and int(r['total']) > 0 and r['failed'] == '0'
                   and r['passed'] == r['total']}
        recorded_ok = {r['function'] for r in results if r['cases'] == 'recorded' and r['option'] == ''
                       and r['source'] == current and r['total'].isdigit() and int(r['total']) > 0
                       and r['failed'] == '0'}
        if shape.field is not None and name not in family_emit.TEMPLATES:
            errs.append(f'{where}: a parameterized family needs a template in tools/family_emit.py')
        elif shape.field is not None:
            try:
                expected = family_emit.render(root, name)
            except SystemExit as e:
                errs.append(f'{where}: {e}')
            else:
                path = os.path.join(root, 'game', family['system'], f'{name}.cpp')
                current = None
                if os.path.isfile(path):
                    with open(path) as f:
                        current = f.read()
                if current != expected:
                    errs.append(f"{where}: game/{family['system']}/{name}.cpp differs from tools/family_emit.py's output")
        member_names = set()
        total_bytes = 0
        for m in members:
            mw = f"{rel}:{m['_line']}"
            a = m['address']
            if not ADDR.match(a):
                continue
            if a in seen_members:
                errs.append(f'{mw}: {a} is a member of another family')
            seen_members.add(a)
            if a in named:
                errs.append(f'{mw}: {a} has a row in symbols/functions.csv; a function is a member or named, not both')
            if a in hooked:
                errs.append(f'{mw}: {a} is in game/hooks.csv')
            if int(a, 16) in in_source:
                errs.append(f'{mw}: a source file under game/ names {a} (a declared original or an observe '
                            f'hook); a member must not be')
            if export and a not in size_of:
                errs.append(f'{mw}: {a} is not a function in the Ghidra export')
            elif export and m['size'] != size_of[a]:
                errs.append(f"{mw}: size {m['size']} differs from the export ({size_of[a]})")
            if not m['size'].isdigit() or int(m['size']) != shape.size:
                errs.append(f"{mw}: size {m['size']} is not the shape's {shape.size} bytes")
                continue
            total_bytes += int(m['size'])
            if not m['extent'].isdigit() or int(m['extent']) < fam.HOOK_JUMP or int(m['extent']) < int(m['size']):
                errs.append(f"{mw}: extent {m['extent']} must be at least the size and {fam.HOOK_JUMP} (the hook's jump)")
            elif export and a in next_start and int(a, 16) + int(m['extent']) != next_start[a]:
                errs.append(f"{mw}: extent {m['extent']} does not reach the next function in the export")
            elif export and a not in next_start:
                errs.append(f'{mw}: the last function in the export cannot be a member (no known extent)')
            if (m['value'] == '') != (shape.field is None):
                errs.append(f"{mw}: value must be {'empty' if shape.field is None else 'the shape parameter'}")
            elif m['value'] and not (re.fullmatch(r'0x[0-9a-f]+', m['value'])
                                      and int(m['value'], 16) <= FIELD_LIMIT[shape.field]
                                      and (shape.field != 'd8' or int(m['value'], 16) > 0)):
                errs.append(f"{mw}: value {m['value']} is not a valid {{{shape.field}}}")
            if m['status'] not in HOOKED:
                errs.append(f"{mw}: status '{m['status']}' must be one of {'|'.join(HOOKED)}")
            symbol = fam.replacement(family, m['value'])
            if symbol not in defined:
                errs.append(f'{mw}: no extern "C" definition of {symbol} under game/{family["system"]}/')
            elif defined[symbol] not in RETURN_CLASSES[family['returns']]:
                errs.append(f"{mw}: {symbol} returns '{defined[symbol]}', the family declares {family['returns']}")
            member = fam.member_name(family, a)
            member_names.add(member)
            if m['status'] in ('edge-verified', 'verified') and member not in edge_ok:
                errs.append(f'{mw}: {member} is {m["status"]} without a current, passing edge result in '
                            f'symbols/families/{name}.verification.csv (tools/verify.py family --record)')
            if m['status'] == 'verified':
                review = reviews.get(a)
                if member not in recorded_ok:
                    errs.append(f'{mw}: {member} is verified without a current, passing recorded result')
                if not review or review.get('verdict') != 'approved':
                    errs.append(f'{mw}: {member} is verified without an approved review in symbols/reviews.csv')
        if (len(members), total_bytes) != (int(family['members']), int(family['bytes'])):
            errs.append(f"{where}: {rel} has {len(members)} members ({total_bytes} bytes); the row registers "
                        f"{family['members']} ({family['bytes']} bytes)")
        for r in results:
            if r['function'] not in member_names:
                errs.append(f"symbols/families/{name}.verification.csv:{r['_line']}: {r['function']} is not a member")
    known = {f'{name}.csv' for name in seen_families} | {f'{name}.verification.csv' for name in seen_families}
    directory = os.path.join(root, fam.MEMBERS_DIR)
    if os.path.isdir(directory):
        for fn in sorted(os.listdir(directory)):
            if fn not in known:
                errs.append(f'symbols/families/{fn}: no family of that name in symbols/families.csv')
    return errs


def replacement_return_type(root: str, replacement: str) -> str | None:
    """The return type of `extern "C" TYPE replacement(` under game/ ('pointer' for any pointer),
    or None when no such definition is found."""
    pattern = re.compile(r'extern "C"\s+([\w ]+?)\s*(\*?)\s*\b' + re.escape(replacement) + r'\s*\(')
    for dp, _, fns in os.walk(os.path.join(root, 'game')):
        for fn in fns:
            if fn.endswith(('.c', '.cc', '.cpp')):
                with open(os.path.join(dp, fn), errors='replace') as f:
                    m = pattern.search(f.read())
                if m:
                    return 'pointer' if m.group(2) else ' '.join(m.group(1).split()).replace('const ', '')
    return None


def defining_files(root: str, name: str) -> list[str]:
    """Files under game/ that define the replacement bb_<name>."""
    out = []
    pattern = re.compile(rf'\bbb_{re.escape(name)}\s*\(')
    for dp, _, fns in os.walk(os.path.join(root, 'game')):
        for fn in fns:
            if fn.endswith(('.c', '.cc', '.cpp')):
                path = os.path.join(dp, fn)
                with open(path, errors='replace') as f:
                    if pattern.search(f.read()):
                        out.append(os.path.relpath(path, root).replace(os.sep, '/'))
    return out


def check_reviews(root: str, funcs: list[dict]) -> list[str]:
    """A verified function needs an approved independent review (symbols/reviews.csv), unless its
    code is still grandfathered (tools/readable_allowlist.txt, STYLE.md)."""
    errs = []
    allow_path = os.path.join(root, 'tools', 'readable_allowlist.txt')
    allow = set()
    if os.path.isfile(allow_path):
        with open(allow_path) as f:
            allow = {ln.strip() for ln in f if ln.strip() and not ln.lstrip().startswith('#')}
    path = os.path.join(root, 'symbols', 'reviews.csv')
    reviews: dict[str, dict] = {}
    if os.path.isfile(path):
        with open(path, newline='') as f:
            rd = csv.reader(f)
            head = next(rd, None)
            if head != REVIEW_HEADER:
                return [f"symbols/reviews.csv: header must be {','.join(REVIEW_HEADER)}"]
            for n, row in enumerate(rd, 2):
                if len(row) != len(REVIEW_HEADER):
                    errs.append(f'symbols/reviews.csv:{n}: expected {len(REVIEW_HEADER)} fields')
                    continue
                r = dict(zip(REVIEW_HEADER, row))
                if r['verdict'] not in ('approved', 'changes requested'):
                    errs.append(f"symbols/reviews.csv:{n}: verdict must be 'approved' or 'changes requested'")
                if not r['reviewer'].strip():
                    errs.append(f'symbols/reviews.csv:{n}: reviewer is empty')
                reviews[r['address']] = r       # the latest row for an address counts
    for r in funcs:
        if r['status'] != 'verified':
            continue
        files = defining_files(root, r['name'])
        if files and all(f in allow for f in files):
            continue
        rv = reviews.get(r['address'])
        if not rv or rv['verdict'] != 'approved':
            errs.append(f"symbols/functions.csv:{r['_line']}: {r['name']} is verified without an approved review "
                        f"in symbols/reviews.csv (CONTRIBUTING.md, \"Definition of done\")")
    return errs


if __name__ == '__main__':
    sys.exit(main())
