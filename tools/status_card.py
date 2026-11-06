#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The README status card: docs/assets/status-light.svg and docs/assets/status-dark.svg.

Derived only from files under symbols/, like tools/progress.py, which writes and checks the card
with the README blocks (--update-readme, --check):
- symbols/imports.csv: each import of the executable and who provides it at the last recorded
  boot (the game's own bundled modules, the runtime, a registered stop by name, or nobody);
- symbols/boot.json and symbols/boot_history.csv: the furthest milestone, where the boot stops,
  and how many imports worked at each recorded boot;
- symbols/functions.csv and symbols/ghidra_functions.csv (through progress.compute): the
  decompilation.

No clock is read: "in the last 7 days" counts back from the newest date in the boot history, so
the same files always give the same card.

usage: status_card.py [--root DIR] [--out-dir DIR]   (writes the two SVGs; progress.py also does)
"""
from __future__ import annotations

import argparse
import csv
import datetime
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import progress  # noqa: E402  (the shared numbers: compute(), runtime())

OUT_FILES = {'light': 'docs/assets/status-light.svg', 'dark': 'docs/assets/status-dark.svg'}

# Who provides an import, in drawing order; the colours were checked with the dataviz palette
# validator in both modes (categorical slots blue, aqua, yellow; grey is the "not yet" track).
STATES = ('runtime', 'bundled', 'stops', 'missing')
STATE_LABELS = {'runtime': 'implemented by the runtime', 'bundled': "the game's own modules",
                'stops': 'registered, stops by name', 'missing': 'not yet'}
THEMES = {
    'light': {'surface': '#fcfcfb', 'ink': '#0b0b0b', 'ink2': '#52514e', 'muted': '#898781',
              'grid': '#e1e0d9', 'track': '#e1e0d9', 'badge': '#52514e', 'badge_text': '#ffffff',
              'runtime': '#2a78d6', 'bundled': '#1baf7a', 'stops': '#eda100', 'line': '#2a78d6'},
    'dark': {'surface': '#1a1a19', 'ink': '#f0efec', 'ink2': '#c3c2b7', 'muted': '#898781',
             'grid': '#2c2c2a', 'track': '#3a3a37', 'badge': '#3a3a37', 'badge_text': '#f0efec',
             'runtime': '#3987e5', 'bundled': '#199e70', 'stops': '#c98500', 'line': '#3987e5'},
}
FONT = "-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
WIDTH, HEIGHT = 1000, 620
WEEK = datetime.timedelta(days=7)


# ---------------------------------------------------------------- data

def library_states(root: str) -> dict[str, dict[str, int]]:
    """Per library the executable imports from, how many of its imports are in each state."""
    libraries: dict[str, dict[str, int]] = {}
    with open(os.path.join(root, 'symbols', 'imports.csv'), newline='') as f:
        for row in csv.DictReader(f):
            if row['importer'] != 'eboot':
                continue
            provider = row['provider']
            if provider == 'runtime':
                state = 'runtime'
            elif provider == 'runtime-unresolved':
                state = 'stops'
            elif provider:
                state = 'bundled'
            else:
                state = 'missing'
            counts = libraries.setdefault(row['library'], dict.fromkeys(STATES, 0))
            counts[state] += 1
    return libraries


def boot_history(root: str) -> list[dict]:
    """Each recorded boot: its date and how many imports worked (bundled plus runtime)."""
    with open(os.path.join(root, 'symbols', 'boot_history.csv'), newline='') as f:
        return [{'date': datetime.date.fromisoformat(r['date']),
                 'working': int(r['imports_bundled']) + int(r['imports_implemented']),
                 'milestone': r['milestone'], 'milestone_index': int(r['milestone_index'])}
                for r in csv.DictReader(f)]


def gained_in_week(history: list[dict]) -> int:
    """Imports that started working in the 7 days up to the newest recorded boot."""
    if not history:
        return 0
    newest = history[-1]
    before = [h for h in history if h['date'] <= newest['date'] - WEEK]
    baseline = before[-1] if before else history[0]
    return newest['working'] - baseline['working']


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


def cell_grid(count: int, w: float, h: float) -> tuple[int, float]:
    """Columns and cell size that fit `count` square cells into w by h."""
    size = math.sqrt(w * h / count) if count else 0
    while size > 1:
        columns = max(1, int(w // size))
        if math.ceil(count / columns) * size <= h:
            return columns, size
        size -= 0.25
    return max(1, int(w)), 1.0


def short_name(library: str) -> str:
    return library[len('libSce'):] if library.startswith('libSce') else library


# ---------------------------------------------------------------- drawing

def text(x: float, y: float, s: str, fill: str, size: int = 12, weight: str = 'normal', anchor: str = 'start') -> str:
    return (f'<text x="{x:.1f}" y="{y:.1f}" fill="{fill}" font-size="{size}" font-weight="{weight}" '
            f'text-anchor="{anchor}">{html.escape(s)}</text>')


def badge(x: float, y: float, label: str, value: str, colour: str, t: dict) -> tuple[str, float]:
    """A two-part badge; returns its SVG and width."""
    lw, vw = 12 + 7.2 * len(label), 12 + 7.6 * len(value)
    svg = (f'<rect x="{x:.1f}" y="{y}" width="{lw + vw:.1f}" height="26" rx="4" fill="{t["badge"]}"/>'
           f'<path d="M{x + lw:.1f} {y}h{vw - 4:.1f}a4 4 0 0 1 4 4v18a4 4 0 0 1-4 4h-{vw - 4:.1f}z" fill="{colour}"/>'
           + text(x + 6, y + 17.5, label, t['badge_text'], 13)
           + text(x + lw + 6, y + 17.5, value, '#ffffff' if colour != t['stops'] else '#0b0b0b', 13, 'bold'))
    return svg, lw + vw


def draw_treemap(libraries: dict[str, dict[str, int]], x: float, y: float, w: float, h: float, t: dict) -> list[str]:
    order = sorted(libraries, key=lambda lib: (-sum(libraries[lib].values()), lib))
    rects = squarify([sum(libraries[lib].values()) for lib in order], x, y, w, h)
    parts = []
    for lib, (rx, ry, rw, rh) in zip(order, rects):
        counts = libraries[lib]
        total = sum(counts.values())
        working = counts['runtime'] + counts['bundled']
        inner_x, inner_y, inner_w, inner_h = rx + 2, ry + 2, rw - 4, rh - 4
        label_h = 15 if inner_h > 34 and inner_w > 44 else 0
        tooltip = (f'{lib}: {working} of {total} working ({counts["runtime"]} runtime, {counts["bundled"]} '
                   f'game modules, {counts["stops"]} stop by name, {counts["missing"]} not yet)')
        parts.append(f'<g><title>{html.escape(tooltip)}</title>')
        parts.append(f'<rect x="{inner_x:.1f}" y="{inner_y:.1f}" width="{inner_w:.1f}" height="{inner_h:.1f}" '
                     f'rx="3" fill="none" stroke="{t["grid"]}"/>')
        if label_h:
            name = short_name(lib)
            room = int((inner_w - 8) / 6.6)
            caption = f'{name} {working}/{total}'
            parts.append(text(inner_x + 4, inner_y + 11.5, caption if len(caption) <= room else name[:max(room, 1)],
                              t['ink2'], 10))
        columns, size = cell_grid(total, inner_w - 4, inner_h - 4 - label_h)
        k = 0
        for state in STATES:
            for _ in range(counts[state]):
                cx = inner_x + 2 + (k % columns) * size
                cy = inner_y + 2 + label_h + (k // columns) * size
                fill = t['track'] if state == 'missing' else t[state]
                gap = 1 if size >= 4 else 0
                parts.append(f'<rect x="{cx:.1f}" y="{cy:.1f}" width="{size - gap:.1f}" height="{size - gap:.1f}" '
                             f'rx="{0.5 if gap else 0}" fill="{fill}"/>')
                k += 1
        parts.append('</g>')
    return parts


def draw_history(history: list[dict], total: int, x: float, y: float, w: float, h: float, t: dict) -> list[str]:
    if len(history) < 2:
        return [text(x, y + 14, 'Not enough recorded boots yet', t['muted'], 11)]
    first, last = history[0]['date'], history[-1]['date']
    span = max((last - first).days, 1)
    px = lambda d: x + 30 + (w - 34) * (d - first).days / span  # noqa: E731
    py = lambda v: y + h - 16 - (h - 24) * v / total  # noqa: E731
    parts = []
    for v in sorted({*range(0, total, 200), total}):
        parts.append(f'<line x1="{x + 30}" x2="{x + w - 4}" y1="{py(v):.1f}" y2="{py(v):.1f}" stroke="{t["grid"]}"/>')
        parts.append(text(x + 26, py(v) + 4, str(v), t['muted'], 10, anchor='end'))
    points = ' '.join(f'{px(r["date"]):.1f},{py(r["working"]):.1f}' for r in history)
    parts.append(f'<polyline points="{points}" fill="none" stroke="{t["line"]}" stroke-width="2" '
                 f'stroke-linejoin="round" stroke-linecap="round"/>')
    for r in history:
        parts.append(f'<circle cx="{px(r["date"]):.1f}" cy="{py(r["working"]):.1f}" r="3" fill="{t["line"]}" '
                     f'stroke="{t["surface"]}" stroke-width="1.5"><title>{r["date"]}: {r["working"]} working, '
                     f'{html.escape(r["milestone"])}</title></circle>')
    parts.append(text(x + 30, y + h, first.isoformat(), t['muted'], 10))
    parts.append(text(x + w - 4, y + h, last.isoformat(), t['muted'], 10, anchor='end'))
    return parts


def draw_bars(rows: list[tuple[str, int]], whole: int, x: float, y: float, w: float, t: dict) -> list[str]:
    parts = []
    for i, (label, value) in enumerate(rows):
        by = y + i * 30
        fill = max(4.0, (w - 150) * value / whole) if value else 0
        parts.append(text(x, by + 12, label, t['ink2'], 12))
        parts.append(f'<rect x="{x + 110}" y="{by + 2}" width="{w - 150}" height="12" rx="2" fill="{t["track"]}"/>')
        if fill:
            parts.append(f'<rect x="{x + 110}" y="{by + 2}" width="{fill:.1f}" height="12" rx="2" fill="{t["runtime"]}"/>')
        parts.append(text(x + w, by + 12, f'{value:,}', t['ink'], 12, anchor='end'))
    return parts


def render(root: str, theme: str) -> str:
    t = THEMES[theme]
    data = progress.compute(root)
    rt = data.get('runtime') or {}
    libraries = library_states(root)
    history = boot_history(root)
    total_imports = sum(sum(c.values()) for c in libraries.values())
    working = sum(c['runtime'] + c['bundled'] for c in libraries.values())
    replaced, edge, verified = (data[k]['functions'] for k in ('replaced', 'edge_verified', 'verified'))
    target = data['total']['functions']

    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" '
         f'viewBox="0 0 {WIDTH} {HEIGHT}" font-family="{FONT}" role="img" '
         f'aria-label="Paleblood status: {working} of {total_imports} imports working">',
         f'<rect width="{WIDTH}" height="{HEIGHT}" rx="8" fill="{t["surface"]}"/>']

    bx = 16
    for label, value, colour in (
            ('imports working', f'{working}/{total_imports} ({100 * working / total_imports:.1f}%)', t['runtime']),
            ('boot', f'{rt.get("milestone", "none")} ({rt.get("milestone_index", 0)}/{rt.get("milestones", 0)})',
             t['bundled']),
            ('in the last 7 days', f'+{gained_in_week(history)} imports', t['runtime']),
            ('functions decompiled', f'{replaced:,} ({verified} verified)',
             t['bundled'])):
        svg, width = badge(bx, 14, label, value, colour, t)
        p.append(svg)
        bx += width + 8

    p.append(text(16, 70, f'System imports by library: {working} of {total_imports} working', t['ink'], 14, 'bold'))
    p += draw_treemap(libraries, 16, 80, 590, 450, t)
    lx = 16
    for state in STATES:
        n = sum(c[state] for c in libraries.values())
        fill = t['track'] if state == 'missing' else t[state]
        p.append(f'<rect x="{lx}" y="542" width="10" height="10" rx="2" fill="{fill}"/>')
        caption = f'{STATE_LABELS[state]} ({n})'
        p.append(text(lx + 14, 551, caption, t['ink2'], 11))
        lx += 22 + 6.2 * len(caption)

    p.append(text(630, 70, 'Imports working at each recorded boot', t['ink'], 14, 'bold'))
    p += draw_history(history, total_imports, 630, 82, 354, 170, t)

    p.append(text(630, 290, 'Boot', t['ink'], 14, 'bold'))
    p.append(text(630, 312, f'Furthest milestone: {rt.get("milestone", "none")}', t['ink2'], 12))
    stop = progress.boot_stop(rt) if rt else 'not recorded'
    words, line, lines = stop.replace('`', '').split(), '', []
    for word in words:
        if len(line) + len(word) + 1 > 52:
            lines.append(line)
            line = word
        else:
            line = f'{line} {word}'.strip()
    lines.append(line)
    for i, piece in enumerate(lines[:3]):
        p.append(text(630, 334 + 17 * i, ('Stops at: ' if i == 0 else '') + piece, t['ink2'], 12))

    p.append(text(630, 410, f'Decompilation (of {target:,} functions)', t['ink'], 14, 'bold'))
    p += draw_bars([('replaced', replaced), ('edge-verified', edge), ('verified', verified)], target, 630, 422, 354, t)

    p.append(text(16, 590, 'Counted from symbols/ at the last recorded boot. Percentages are of the imports this '
                  'game uses, not of every PS4 system function.', t['muted'], 11))
    p.append(text(16, 606, 'Generated by tools/status_card.py through tools/progress.py --update-readme. Do not edit.',
                  t['muted'], 11))
    p.append('</svg>')
    return '\n'.join(p) + '\n'


def render_all(root: str) -> dict[str, str]:
    """Relative path -> SVG text, for both themes."""
    return {path: render(root, theme) for theme, path in OUT_FILES.items()}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=progress.DEFAULT_ROOT)
    a = ap.parse_args()
    for path, svg in render_all(a.root).items():
        full = os.path.join(a.root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'w') as f:
            f.write(svg)
        print(f'status card: {path} written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
