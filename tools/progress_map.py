#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The decompilation progress map and badges that the README shows.

Derived only from committed files under symbols/ (no game data):
- symbols/ghidra_functions.csv: every function of the executable and its size (the denominator);
- symbols/functions.csv and the function families (symbols/families.csv, symbols/families/): the
  status of each replaced function (replaced, edge-verified, verified), through progress.compute;
- symbols/source_files.csv: the library each function was compiled from (tools/source_files.py);
- symbols/verification.csv, symbols/matching.csv and the families' tables: the evidence behind
  each verified function, and the date each function was first tested ("in the last 7 days" counts
  back from the newest such date, never from the clock, so the same files give the same bytes).
The libraries' display names and groups come from game/progress_map.csv (tools/ stays game-agnostic).

The map is a treemap grouped by library, its area proportional to bytes, so the size of what is
left reads honestly; each block is a grid of cells, each cell the same number of that block's
bytes. Verified, then replaced, bytes fill a block's cells from the top left.

These files are not committed: .github/workflows/progress-map.yml builds them on every push to
main and publishes them on GitHub Pages, so the README never shows a stale map.

usage: progress_map.py [--root DIR] --out-dir DIR   (writes progress.svg and badge-*.svg)
"""
from __future__ import annotations

import argparse
import bisect
import csv
import datetime
import html
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import families as fam  # noqa: E402
import progress  # noqa: E402  (the shared numbers)

MAP = 'progress.svg'
BADGES = ('badge-verified.svg', 'badge-replaced.svg', 'badge-code.svg', 'badge-week.svg')
WEEK = datetime.timedelta(days=7)

# Colours: GitHub's dark canvas and its success green, the shields amber, a neutral grey track.
BG, INK, MUTED, EDGE = '#0d1117', '#f0f6fc', '#9198a1', '#30363d'
STATUS_FILL = {'verified': '#2ea043', 'replaced': '#dfb317', 'open': '#6e7681'}
STATUS_LABEL = {'verified': 'verified', 'replaced': 'replaced, not yet verified', 'open': 'not replaced yet'}
FONT = "-apple-system, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
WIDTH = 1000
MARGIN = 12
MAP_HEIGHT = 560
PITCH = 5  # one cell: 4 px and a 1 px gap

# How the map names and groups the libraries of symbols/source_files.csv: game/progress_map.csv
# (library, group, group_short, name, short). Groups are drawn in the order they first appear there;
# a library it does not list goes into OTHER_GROUP under a title-cased form of its id, and code no
# library claims (the empty id) into its own group.
NAMES_TABLE = os.path.join('game', 'progress_map.csv')
OTHER_GROUP = ('Libraries', 'Libraries')
UNATTRIBUTED = {'library': '', 'group': 'Code not attributed to a library yet', 'group_short': 'Unattributed',
                'name': 'Code not attributed to a library yet', 'short': 'Unattributed'}


# ---------------------------------------------------------------- data

def read_csv(path: str) -> list[dict]:
    if not os.path.isfile(path):
        return []
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


class Names:
    """Display names and groups for libraries (game/progress_map.csv)."""

    def __init__(self, root: str):
        self.rows = {r['library']: r for r in read_csv(os.path.join(root, NAMES_TABLE))}

    def row(self, lib: str) -> dict:
        if lib in self.rows:
            return self.rows[lib]
        if not lib:
            return UNATTRIBUTED
        title = ' '.join(part[:1].upper() + part[1:] for part in re.split(r'[-_ ]+', lib) if part)
        return {'library': lib, 'group': OTHER_GROUP[0], 'group_short': OTHER_GROUP[1], 'name': title,
                'short': title}

    def name(self, lib: str) -> str:
        return self.row(lib)['name']

    def short(self, lib: str) -> str:
        return self.row(lib)['short']

    def groups(self, libs: list[str]) -> list[tuple[str, str, list[str]]]:
        """(group title, short title, its libraries) in table order, then the groups of libraries
        the table does not list."""
        order: dict[str, tuple[str, list[str]]] = {}
        for r in [*self.rows.values(), *(self.row(lib) for lib in libs)]:
            order.setdefault(r['group'], (r['group_short'], []))
        for lib in libs:
            order[self.row(lib)['group']][1].append(lib)
        return [(g, short, members) for g, (short, members) in order.items() if members]


class Libraries:
    """Address -> library, from the runs of symbols/source_files.csv (first and last function)."""

    def __init__(self, root: str):
        runs = sorted((int(r['address'], 16), int(r['last'], 16), r['library'])
                      for r in read_csv(os.path.join(root, 'symbols', 'source_files.csv')))
        self.starts = [r[0] for r in runs]
        self.runs = runs

    def of(self, address: int) -> str:
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and address <= self.runs[i][1]:
            return self.runs[i][2]
        return ''


def status_rows(root: str) -> list[dict]:
    """Every tracked function (symbols/functions.csv and family members), as progress counts them."""
    return progress.read_csv(os.path.join(root, 'symbols', 'functions.csv')) + fam.function_rows(root)


def library_totals(root: str, rows: list[dict]) -> dict[str, dict]:
    """Per library: functions and bytes in all, replaced (any level) and verified."""
    libs = Libraries(root)
    out: dict[str, dict] = {}

    def entry(lib: str) -> dict:
        return out.setdefault(lib, {'functions': 0, 'bytes': 0, 'replaced': 0, 'replaced_bytes': 0,
                                    'verified': 0, 'verified_bytes': 0})
    for r in read_csv(os.path.join(root, 'symbols', 'ghidra_functions.csv')):
        e = entry(libs.of(int(r['address'], 16)))
        e['functions'] += 1
        e['bytes'] += int(r['size'])
    for r in rows:
        if r['status'] not in progress.IMPLEMENTED:
            continue
        e = entry(libs.of(int(r['address'], 16)))
        e['replaced'] += 1
        e['replaced_bytes'] += int(r['size'])
        if r['status'] == 'verified':
            e['verified'] += 1
            e['verified_bytes'] += int(r['size'])
    return out


def evidence(root: str, rows: list[dict]) -> dict[str, int]:
    """Verified functions by the evidence they carry (a function can carry several kinds)."""
    recorded = {r['function'] for r in read_csv(os.path.join(root, 'symbols', 'verification.csv'))
                if r['cases'] in ('recorded', 'option-recorded') and int(r['total']) > 0
                and r['passed'] == r['total']}
    exact = {r['address'] for r in read_csv(os.path.join(root, 'symbols', 'matching.csv'))
             if r['byte_exact'] == 'yes'}
    for f in fam.families(root):
        exact |= {r['address'] for r in read_csv(os.path.join(root, 'symbols', 'families',
                                                              f"{f['family']}.matching.csv"))
                  if r['byte_exact'] == 'yes'}
    verified = [r for r in rows if r['status'] == 'verified']
    return {'inputs recorded in the game': sum(r['name'] in recorded for r in verified),
            'byte-exact matching source': sum(r['address'] in exact for r in verified),
            'in a function family': sum(bool(r.get('family')) for r in verified)}


def first_tested(root: str) -> dict[str, datetime.date]:
    """Function name -> the date of its first verification record (symbols/verification.csv and
    the families' tables): when it was first replaced and tested."""
    paths = [os.path.join(root, 'symbols', 'verification.csv')]
    paths += [os.path.join(root, 'symbols', 'families', f"{f['family']}.verification.csv")
              for f in fam.families(root)]
    out: dict[str, datetime.date] = {}
    for path in paths:
        for r in read_csv(path):
            try:
                day = datetime.date.fromisoformat(r['date'])
            except ValueError:
                continue
            if r['function'] not in out or day < out[r['function']]:
                out[r['function']] = day
    return out


def replaced_in_week(rows: list[dict], tested: dict[str, datetime.date]) -> int:
    """Replaced functions first tested in the 7 days up to the newest test date in the tables."""
    days = [tested[r['name']] for r in rows if r['status'] in progress.IMPLEMENTED and r['name'] in tested]
    if not days:
        return 0
    newest = max(tested.values())
    return sum(d > newest - WEEK for d in days)


# ---------------------------------------------------------------- formatting

def mb(n: int) -> str:
    if n >= 1_000_000:
        return f'{n / 1e6:.1f} MB'
    if n >= 1000:
        return f'{n / 1e3:.1f} KB'
    return f'{n} B'


def pct_text(part: int, whole: int) -> str:
    return f'{100 * part / whole:.2f}%' if whole else 'n/a'


def esc(s: str) -> str:
    return html.escape(s, quote=True)


def text_width(s: str, size: float) -> float:
    """A conservative width estimate for a sans-serif face (no font metrics at build time)."""
    narrow, wide = set('ilj.,:;|!\'() '), set('mwMW%@')
    units = sum(0.32 if c in narrow else 0.86 if c in wide else 0.62 for c in s)
    return units * size


def fit(candidates: list[str], size: float, room: float) -> str:
    """The first candidate that fits in `room` pixels; else the last one, cut short with an ellipsis."""
    for s in candidates:
        if text_width(s, size) <= room:
            return s
    s = candidates[-1]
    while s and text_width(s + '…', size) > room:
        s = s[:-1]
    return s.rstrip() + '…' if s else ''


def wrap(text: str, size: float, room: float) -> list[str]:
    """Words of `text` in lines no wider than `room` pixels (a longer single word gets a line of its own)."""
    lines: list[str] = []
    for word in text.split():
        if lines and text_width(f'{lines[-1]} {word}', size) <= room:
            lines[-1] += f' {word}'
        else:
            lines.append(word)
    return lines


def label(x: float, y: float, s: str | list[str], size: float = 12, weight: str = 'normal', fill: str = INK,
          anchor: str = 'start', halo: bool = False, room: float | None = None) -> str:
    """A text element; with `room`, the first of the candidate strings `s` that fits that width."""
    candidates = [s] if isinstance(s, str) else s
    s = fit(candidates, size, room) if room is not None else candidates[0]
    stroke = f' stroke="{BG}" stroke-width="3" stroke-linejoin="round" paint-order="stroke"' if halo else ''
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
            f'text-anchor="{anchor}"{stroke}>{esc(s)}</text>')


# ---------------------------------------------------------------- layout

def squarify(sizes: list[float], x: float, y: float, w: float, h: float) -> list[tuple]:
    """Squarified treemap (Bruls, Huizing and van Wijk): one rectangle per size, in order."""
    total = sum(sizes)
    scale = w * h / total if total else 0
    areas = [s * scale for s in sizes]
    out: list[tuple] = []

    def worst(row: list[float], side: float) -> float:
        s = sum(row)
        return max(max(side * side * a / (s * s), s * s / (side * side * a)) for a in row)

    i = 0
    while i < len(areas):
        side = min(w, h)
        row = [areas[i]]
        i += 1
        while i < len(areas) and worst(row + [areas[i]], side) <= worst(row, side):
            row.append(areas[i])
            i += 1
        strip = sum(row) / side
        offset = 0.0
        for a in row:
            length = a / strip
            out.append((x, y + offset, strip, length) if w >= h else (x + offset, y, length, strip))
            offset += length
        if w >= h:
            x, w = x + strip, w - strip
        else:
            y, h = y + strip, h - strip
    return out


def group_regions(sizes: list[int], x: float, y: float, w: float, h: float, header: float,
                  gap: float) -> list[tuple]:
    """Regions for the groups: the first a column on the left, the rest stacked on the right. Each
    region keeps `header` pixels on top for its title; the areas below the titles are in proportion
    to the sizes."""
    if len(sizes) == 1:
        return [(x, y, w, h)]
    first, rest = sizes[0], sum(sizes[1:])
    stacked = len(sizes) - 1
    # first_w * (h - header) : rest_w * (h - stacked*header - gaps) = first : rest
    right_h = h - stacked * header - (stacked - 1) * gap
    ratio = first * right_h / (rest * (h - header))
    first_w = (w - gap) * ratio / (1 + ratio)
    right_w = w - gap - first_w
    out = [(x, y, first_w, h)]
    top = y
    for s in sizes[1:]:
        inner = right_h * s / rest
        out.append((x + first_w + gap, top, right_w, inner + header))
        top += inner + header + gap
    return out


def snap(v: float) -> float:
    return -(-v // PITCH) * PITCH


def cell_counts(lib: dict, cells: int) -> tuple[int, int]:
    """Cells for verified bytes and for replaced (not verified) bytes in a block of `cells` cells:
    rounded to the nearest cell, but any replaced bytes show at least one."""
    if not cells or not lib['bytes']:
        return 0, 0
    per = lib['bytes'] / cells

    def n(b: int) -> int:
        return min(cells, max(1, int(b / per + 0.5))) if b else 0
    verified = n(lib['verified_bytes'])
    replaced = max(verified, n(lib['replaced_bytes']))
    return verified, replaced - verified


def cell_runs(start: int, count: int, columns: int) -> list[tuple[int, int, int, int]]:
    """Cells start .. start+count-1 of a row-major grid as at most three rectangles
    (column, row, width, height in cells): a partial first row, full rows, a partial last row."""
    out = []
    i, end = start, start + count
    if count <= 0:
        return out
    if i % columns:
        take = min(end - i, columns - i % columns)
        out.append((i % columns, i // columns, take, 1))
        i += take
    if end - i >= columns:
        rows = (end - i) // columns
        out.append((0, i // columns, columns, rows))
        i += rows * columns
    if end > i:
        out.append((0, i // columns, end - i, 1))
    return out


# ---------------------------------------------------------------- drawing

def draw_block(names: Names, key: str, lib: dict, x: float, y: float, w: float, h: float) -> list[str]:
    name = names.name(key)
    tip = (f"{name}: {mb(lib['bytes'])} in {lib['functions']:,} functions. Replaced {lib['replaced']:,} functions, "
           f"{mb(lib['replaced_bytes'])} ({pct_text(lib['replaced_bytes'], lib['bytes'])} of its bytes); verified "
           f"{lib['verified']:,}, {mb(lib['verified_bytes'])} ({pct_text(lib['verified_bytes'], lib['bytes'])}).")
    room = w - 8
    short = names.short(key)
    detail = f"{pct_text(lib['verified_bytes'], lib['bytes'])} verified, {mb(lib['bytes'])}"
    captions = []  # (text, size, weight): the block's name and numbers above its cells, when they fit
    for title, size in ((name, 13), (short, 13), (short, 10)):
        if text_width(title, size) <= room and h >= 3 * size:
            captions.append((title, size, 'bold'))
            if h >= 120 and text_width(detail, 11) <= room:
                captions.append((detail, 11, 'normal'))
            break
    strip = sum(size + 3 for _, size, _ in captions) + (3 if captions else 0)
    cx, cy = snap(x + 1), snap(y + 1 + strip)
    columns, rows = max(0, int((x + w - 1 - cx) // PITCH)), max(0, int((y + h - 1 - cy) // PITCH))
    cells = columns * rows
    parts = [f'<g><title>{esc(tip)}</title>',
             f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" fill="{BG}" stroke="{EDGE}"/>']
    if cells:
        parts.append(f'<rect x="{cx}" y="{cy}" width="{columns * PITCH}" height="{rows * PITCH}" fill="url(#open)"/>')
        verified, replaced = cell_counts(lib, cells)
        for status, start, count in (('verified', 0, verified), ('replaced', verified, replaced)):
            for col, row, cw, ch in cell_runs(start, count, columns):
                parts.append(f'<rect x="{cx + col * PITCH}" y="{cy + row * PITCH}" width="{cw * PITCH}" '
                             f'height="{ch * PITCH}" fill="url(#{status})"/>')
    ty = y + 2
    for caption, size, weight in captions:
        ty += size + 3
        parts.append(label(x + 4, ty - 2, caption, size, weight, fill=INK if weight == 'bold' else MUTED))
    parts.append('</g>')
    return parts


def draw_map(names: Names, libs: dict[str, dict], x: float, y: float, w: float, h: float) -> list[str]:
    present = sorted((k for k in libs if libs[k]['bytes']), key=lambda k: (-libs[k]['bytes'], k))
    groups = names.groups(present)
    sizes = [sum(libs[k]['bytes'] for k in keys) for _, _, keys in groups]
    parts = []
    header = 22
    for (title, short, keys), (gx, gy, gw, gh) in zip(groups, group_regions(sizes, x, y, w, h, header, 10)):
        whole = sum(libs[k]['bytes'] for k in keys)
        ver = sum(libs[k]['verified_bytes'] for k in keys)
        numbers = f'{pct_text(ver, whole)} verified ({mb(whole)})'
        parts.append(f'<g><title>{esc(f"{title}: {numbers}")}</title>')
        parts.append(label(gx, gy + 15, [f'{title}: {numbers}', f'{title}: {pct_text(ver, whole)} ({mb(whole)})',
                                         f'{short}: {pct_text(ver, whole)} ({mb(whole)})', short],
                           14, 'bold', room=gw))
        parts.append('</g>')
        rects = squarify([libs[k]['bytes'] for k in keys], gx, gy + header, gw, gh - header)
        for key, (bx, by, bw, bh) in zip(keys, rects):
            parts += draw_block(names, key, libs[key], bx, by, bw, bh)
    return parts


def draw_bars(title: str, rows: list[tuple], x: float, y: float, w: float) -> tuple[list[str], float]:
    """Horizontal bars: rows of (label, [(value, status)...], whole, value text)."""
    parts = [label(x, y + 15, title, 14, 'bold', room=w)]
    label_w, value_w = 0.0, 0.0
    for name, _, _, value in rows:
        label_w = max(label_w, text_width(name, 12))
        value_w = max(value_w, text_width(value, 12))
    label_w = min(label_w, 0.4 * w)
    bar_x, bar_w = x + label_w + 10, w - label_w - value_w - 22
    top = y + 26
    for i, (name, segments, whole, value) in enumerate(rows):
        by = top + i * 20
        parts.append(label(x, by + 11, name, 12, fill=INK, room=label_w))
        parts.append(f'<rect x="{bar_x:.1f}" y="{by + 2}" width="{bar_w:.1f}" height="11" fill="{STATUS_FILL["open"]}"/>')
        left = 0.0
        for v, status in segments:
            seg = bar_w * v / whole if whole else 0
            if v and seg < 1:
                seg = 1.0
            if seg:
                parts.append(f'<rect x="{bar_x + left:.1f}" y="{by + 2}" width="{seg:.1f}" height="11" '
                             f'fill="{STATUS_FILL[status]}"/>')
            left += seg
        parts.append(label(x + w, by + 11, value, 12, fill=MUTED, anchor='end'))
    return parts, 26 + 20 * len(rows)


def render_map(root: str, data: dict, rows: list[dict]) -> str:
    libs = library_totals(root, rows)
    names = Names(root)
    t, r, v = data['total'], data['replaced'], data['verified']
    parts = []
    y = 26
    room = WIDTH - 2 * MARGIN
    parts.append(label(MARGIN, y, f"Functions verified: {pct_text(v['functions'], t['functions'])} "
                       f"({v['functions']:,}/{t['functions']:,})", 20, 'bold', room=room))
    y += 22
    replaced = f"Replaced: {pct_text(r['functions'], t['functions'])} ({r['functions']:,} functions"
    parts.append(label(MARGIN, y, [f"{replaced}, {data['families']['replaced']['functions']:,} of them in "
                                   f"{data['families']['count']} function families of tiny identical functions)",
                                   replaced + ')'], 13, fill=MUTED, room=room))
    y += 18
    parts.append(label(MARGIN, y, f"Code replaced: {pct_text(r['bytes'], t['bytes'])} of {mb(t['bytes'])} "
                       f"({mb(r['bytes'])}); verified {pct_text(v['bytes'], t['bytes'])} ({mb(v['bytes'])})",
                       13, fill=MUTED, room=room))
    y += 14
    parts += draw_map(names, libs, MARGIN, y, WIDTH - 2 * MARGIN, MAP_HEIGHT)
    y += MAP_HEIGHT + 20
    lx = MARGIN
    for status in ('verified', 'replaced', 'open'):
        parts.append(f'<rect x="{lx}" y="{y - 10}" width="11" height="11" fill="{STATUS_FILL[status]}"/>')
        parts.append(label(lx + 16, y, STATUS_LABEL[status], 12))
        lx += 16 + text_width(STATUS_LABEL[status], 12) + 18
    parts.append(label(WIDTH - MARGIN, y, ['Area is code bytes; each cell is an equal share of its block. '
                                           'Any replaced bytes show at least one cell.',
                                           'Area is code bytes; each cell is an equal share of its block.',
                                           'Area is code bytes.'],
                       11, fill=MUTED, anchor='end', room=WIDTH - MARGIN - lx))
    y += 18

    col_w = (WIDTH - 2 * MARGIN - 30) / 2
    order = sorted(libs, key=lambda k: (-libs[k]['bytes'], k))
    lib_rows = [(names.short(k), [(libs[k]['verified'], 'verified'),
                                          (libs[k]['replaced'] - libs[k]['verified'], 'replaced')],
                 libs[k]['functions'], f"{libs[k]['replaced']:,}/{libs[k]['functions']:,}") for k in order]
    left, lh = draw_bars('Functions replaced by library', lib_rows, MARGIN, y, col_w)
    ev = evidence(root, rows)
    ev_rows = [(name, [(n, 'verified')], v['functions'], f"{n:,}/{v['functions']:,}") for name, n in ev.items()]
    right, rh = draw_bars('Verified functions by evidence', ev_rows, MARGIN + col_w + 30, y, col_w)
    parts += left + right
    note_y = y + rh + 22
    for line in wrap('A function counts as verified when it passes on its edge cases and on inputs recorded in the '
                     'game, or matches the original bytes exactly, and an independent review has approved it.',
                     11, col_w):
        parts.append(label(MARGIN + col_w + 30, note_y, line, 11, fill=MUTED, room=col_w))
        note_y += 15
    y += max(lh, note_y - 15 - y) + 16
    parts.append(label(MARGIN, y, ['Counted from symbols/ on main (no game data). Generated by tools/progress_map.py; '
                                   'published by .github/workflows/progress-map.yml.',
                                   'Counted from symbols/ on main. Generated by tools/progress_map.py.'],
                       11, fill=MUTED, room=room))
    height = int(y + 12)
    defs = ''.join(f'<pattern id="{s}" width="{PITCH}" height="{PITCH}" patternUnits="userSpaceOnUse">'
                   f'<rect width="{PITCH - 1}" height="{PITCH - 1}" fill="{STATUS_FILL[s]}"/></pattern>'
                   for s in ('open', 'replaced', 'verified'))
    head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" viewBox="0 0 {WIDTH} {height}" '
            f'font-family="{esc(FONT)}" role="img" aria-label="Paleblood decompilation: '
            f"{v['functions']:,} of {t['functions']:,} functions verified, "
            f"{pct_text(r['bytes'], t['bytes'])} of the code replaced\">")
    return '\n'.join([head, f'<defs>{defs}</defs>', f'<rect width="{WIDTH}" height="{height}" fill="{BG}"/>',
                      *parts, '</svg>']) + '\n'


def badge(name: str, value: str, colour: str) -> str:
    """A shields-style flat badge."""
    lw, vw = int(text_width(name, 11) + 12), int(text_width(value, 11) + 12)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{lw + vw}" height="20" role="img" '
            f'aria-label="{esc(name)}: {esc(value)}"><title>{esc(name)}: {esc(value)}</title>'
            f'<rect width="{lw}" height="20" fill="#555"/><rect x="{lw}" width="{vw}" height="20" fill="{colour}"/>'
            f'<g fill="#fff" font-family="Verdana,DejaVu Sans,sans-serif" font-size="11" text-anchor="middle">'
            f'<text x="{lw / 2:.1f}" y="14">{esc(name)}</text><text x="{lw + vw / 2:.1f}" y="14">{esc(value)}</text>'
            '</g></svg>\n')


def render_all(root: str) -> dict[str, str]:
    """File name -> SVG text; nothing for a tree without the Ghidra export."""
    if not os.path.isfile(os.path.join(root, 'symbols', 'ghidra_functions.csv')):
        return {}
    data = progress.compute(root)
    rows = status_rows(root)
    t, r, v = data['total'], data['replaced'], data['verified']
    week = replaced_in_week(rows, first_tested(root))
    return {
        MAP: render_map(root, data, rows),
        'badge-verified.svg': badge('functions verified', f"{pct_text(v['functions'], t['functions'])} "
                                    f"({v['functions']:,})", STATUS_FILL['verified']),
        'badge-replaced.svg': badge('functions replaced', f"{pct_text(r['functions'], t['functions'])} "
                                    f"({r['functions']:,})", STATUS_FILL['replaced']),
        'badge-code.svg': badge('code replaced', f"{pct_text(r['bytes'], t['bytes'])} of {mb(t['bytes'])}",
                                STATUS_FILL['replaced']),
        'badge-week.svg': badge('replaced in 7 days', f'+{week:,} functions', '#2f81f7'),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=progress.DEFAULT_ROOT)
    ap.add_argument('--out-dir', required=True)
    a = ap.parse_args()
    out = render_all(a.root)
    if not out:
        print('progress map: symbols/ghidra_functions.csv is missing', file=sys.stderr)
        return 1
    os.makedirs(a.out_dir, exist_ok=True)
    for name, svg in out.items():
        with open(os.path.join(a.out_dir, name), 'w') as f:
            f.write(svg)
        print(f'progress map: {name} ({len(svg.encode())} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
