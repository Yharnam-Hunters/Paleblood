#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The README's speed history chart: the clinic route's frame rate after each merged speed pull request.

Derived only from docs/speed_history.csv, one row per merged pull request with a measured result on
the standard window (frames 1350-2900 of the warm clinic route boot, tools/ab_bench.py). Each row
holds the "after" (B) side of that pull request's own A/B, copied from its description, with the
source of the number. Nothing is invented or interpolated: a missing 1% low stays empty and the line
skips it. The rows come from different bases, so the line is not a running total; the "before"
average of each row is kept in the table and drawn as a small tick.

The SVG is not committed: .github/workflows/progress-map.yml builds speed.svg with this tool on every
push to main and publishes it on GitHub Pages, like progress.svg. It paints its own dark canvas (as
progress.svg does), so it reads the same on a light or a dark page.

usage: speed_history.py [--root DIR] --out-dir DIR   (writes speed.svg)
"""
from __future__ import annotations

import argparse
import csv
import html
import os
import sys

TABLE = os.path.join('docs', 'speed_history.csv')
OUTPUT = 'speed.svg'

BG, INK, MUTED, EDGE = '#0d1117', '#f0f6fc', '#9198a1', '#30363d'
AVG_COLOR, LOW_COLOR = '#2ea043', '#dfb317'
FONT = "-apple-system, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
WIDTH, HEIGHT = 1000, 360
LEFT, RIGHT, TOP, BOTTOM = 56, 24, 64, 56


def read_rows(root: str) -> list[dict]:
    """The table's rows in file order (the order they merged), with numbers parsed."""
    rows = []
    with open(os.path.join(root, TABLE), newline='') as f:
        for raw in csv.DictReader(f):
            rows.append({
                'date': raw['date'], 'pr': int(raw['pr']), 'sha': raw['merge_sha'],
                'avg': float(raw['avg_fps']),
                'low': float(raw['low1_fps']) if raw['low1_fps'] else None,
                'before': float(raw['before_avg_fps']) if raw['before_avg_fps'] else None,
                'source': raw['source'],
            })
    return rows


def check_rows(rows: list[dict]) -> None:
    """Fail on a row that cannot be a measurement: a repeat, a bad hash, a non-positive rate, or a low above the average."""
    seen = set()
    for r in rows:
        if r['pr'] in seen:
            raise ValueError(f"speed history: PR #{r['pr']} appears twice")
        seen.add(r['pr'])
        if len(r['sha']) != 40 or any(c not in '0123456789abcdef' for c in r['sha']):
            raise ValueError(f"speed history: PR #{r['pr']} has no full merge SHA")
        if not r['source']:
            raise ValueError(f"speed history: PR #{r['pr']} has no source")
        values = [v for v in (r['avg'], r['low'], r['before']) if v is not None]
        if any(v <= 0 for v in values):
            raise ValueError(f"speed history: PR #{r['pr']} has a non-positive rate")
        if r['low'] is not None and r['low'] > r['avg']:
            raise ValueError(f"speed history: PR #{r['pr']} has a 1% low above its average")


def axis_top(rows: list[dict]) -> float:
    """The chart's top, the highest rate rounded up to the next 5 fps."""
    top = max(v for r in rows for v in (r['avg'], r['low'], r['before']) if v is not None)
    return float(int(top / 5) * 5 + 5)


def render(rows: list[dict]) -> str:
    """The chart as SVG text."""
    check_rows(rows)
    top = axis_top(rows)
    plot_w, plot_h = WIDTH - LEFT - RIGHT, HEIGHT - TOP - BOTTOM

    def x_of(i: int) -> float:
        return LEFT + plot_w * (i + 0.5) / len(rows)

    def y_of(v: float) -> float:
        return TOP + plot_h * (1 - v / top)

    first, last = rows[0], rows[-1]
    title = (f"Clinic route frame rate, frames 1350-2900: {first['avg']:.2f} fps after #{first['pr']} "
             f"to {last['avg']:.2f} fps after #{last['pr']}")
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" width="{WIDTH}" height="{HEIGHT}" '
           f'role="img" aria-label="{html.escape(title)}" font-family="{FONT}">',
           f'<title>{html.escape(title)}</title>',
           f'<rect width="{WIDTH}" height="{HEIGHT}" rx="6" fill="{BG}" stroke="{EDGE}"/>',
           f'<text x="{LEFT}" y="26" font-size="16" font-weight="600" fill="{INK}">'
           'Speed history: Iosefka\'s Clinic route</text>',
           f'<text x="{LEFT}" y="46" font-size="12" fill="{MUTED}">average and 1% low fps over frames 1350-2900, '
           'measured after each merged speed pull request (tools/ab_bench.py)</text>']
    for tick in range(0, int(top) + 1, 5):
        y = y_of(float(tick))
        out.append(f'<line x1="{LEFT}" y1="{y:.1f}" x2="{WIDTH - RIGHT}" y2="{y:.1f}" stroke="{EDGE}"/>')
        out.append(f'<text x="{LEFT - 8}" y="{y + 4:.1f}" font-size="11" text-anchor="end" fill="{MUTED}">{tick}</text>')
    for key, color in (('avg', AVG_COLOR), ('low', LOW_COLOR)):
        runs, run = [], []
        for i, r in enumerate(rows):
            if r[key] is None:
                if run:
                    runs.append(run)
                run = []
            else:
                run.append((x_of(i), y_of(r[key])))
        if run:
            runs.append(run)
        for run in runs:
            if len(run) > 1:
                points = ' '.join(f'{x:.1f},{y:.1f}' for x, y in run)
                out.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2"/>')
        for i, r in enumerate(rows):
            if r[key] is not None:
                out.append(f'<circle cx="{x_of(i):.1f}" cy="{y_of(r[key]):.1f}" r="4" fill="{color}">'
                           f'<title>#{r["pr"]} ({r["date"]}): {r[key]:.2f} fps</title></circle>')
    for i, r in enumerate(rows):
        x = x_of(i)
        if r['before'] is not None:
            y = y_of(r['before'])
            out.append(f'<line x1="{x - 7:.1f}" y1="{y:.1f}" x2="{x + 7:.1f}" y2="{y:.1f}" stroke="{MUTED}" '
                       f'stroke-width="2"><title>#{r["pr"]} before: {r["before"]:.2f} fps</title></line>')
        out.append(f'<text x="{x:.1f}" y="{y_of(r["avg"]) - 10:.1f}" font-size="11" text-anchor="middle" '
                   f'fill="{INK}">{r["avg"]:.1f}</text>')
        out.append(f'<text x="{x:.1f}" y="{HEIGHT - BOTTOM + 18}" font-size="11" text-anchor="middle" '
                   f'fill="{INK}">#{r["pr"]}</text>')
        out.append(f'<text x="{x:.1f}" y="{HEIGHT - BOTTOM + 32}" font-size="10" text-anchor="middle" '
                   f'fill="{MUTED}">{r["date"][5:]}</text>')
    legend_y = HEIGHT - 10
    out.append(f'<circle cx="{LEFT + 6}" cy="{legend_y - 4}" r="4" fill="{AVG_COLOR}"/>'
               f'<text x="{LEFT + 16}" y="{legend_y}" font-size="11" fill="{MUTED}">average fps</text>'
               f'<circle cx="{LEFT + 116}" cy="{legend_y - 4}" r="4" fill="{LOW_COLOR}"/>'
               f'<text x="{LEFT + 126}" y="{legend_y}" font-size="11" fill="{MUTED}">1% low fps</text>'
               f'<line x1="{LEFT + 216}" y1="{legend_y - 4}" x2="{LEFT + 230}" y2="{legend_y - 4}" stroke="{MUTED}" '
               f'stroke-width="2"/><text x="{LEFT + 236}" y="{legend_y}" font-size="11" fill="{MUTED}">'
               'the same pull request\'s own baseline (bases differ, so this is not a running total)</text>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument('--out-dir', required=True)
    a = ap.parse_args()
    svg = render(read_rows(a.root))
    os.makedirs(a.out_dir, exist_ok=True)
    with open(os.path.join(a.out_dir, OUTPUT), 'w') as f:
        f.write(svg)
    print(f'speed history: {OUTPUT} ({len(svg.encode())} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
