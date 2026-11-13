#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The progress page published on GitHub Pages next to the progress map (progress.html, progress.json).

Derived only from committed files (no game data), the same ones the map reads:
- symbols/ghidra_functions.csv, symbols/functions.csv and the function families, through
  progress.compute and progress_map: totals, replaced and verified functions and bytes;
- symbols/source_files.csv: the library and source file each run of functions was compiled from;
- symbols/verification.csv, symbols/matching.csv, symbols/reviews.csv: the evidence behind verified functions;
- symbols/imports.csv and symbols/boot.json: which system imports the runtime or a bundled module provides;
- game/progress_map.csv: the libraries' display names.

Optionally, --open-prs FILE adds what open pull requests would add. That file comes from
--fetch-open-prs, which reads the function tables of each open pull request's head through the
GitHub API (gh, read-only token); the tables are parsed as CSV, never executed. The page labels
those numbers as not merged. Everything else is a pure function of the tree, so the same tree gives
the same bytes.

These files are not committed: .github/workflows/progress-map.yml builds them with the map on
every push to main and publishes them.

usage: progress_page.py [--root DIR] --out-dir DIR [--open-prs FILE] [--source-sha SHA]
                        [--repo-url URL] [--wiki-url URL]
       progress_page.py --fetch-open-prs FILE --repo OWNER/NAME   (needs gh and a token)
"""
from __future__ import annotations

import argparse
import bisect
import csv
import io
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import progress  # noqa: E402  (the shared numbers)
import progress_map as pm  # noqa: E402  (libraries, names, evidence)

PAGE, DATA = 'progress.html', 'progress.json'
TOP_FILES = 25
# Status rank: an open pull request "adds" a function when it raises the function's rank.
RANK = {'replaced': 1, 'edge-verified': 2, 'verified': 3}
TABLE_RE = re.compile(r'^symbols/(functions|families/[A-Za-z0-9_]+)\.csv$')

CSS = """
body { max-width: 1000px; margin: 0 auto; padding: 16px; background: #fff; color: #1f2328;
       font: 15px/1.45 -apple-system, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif; }
a { color: #0969da; }
h1 { margin: 8px 0 4px; }
h2 { margin: 28px 0 8px; border-bottom: 1px solid #d0d7de; padding-bottom: 4px; }
h3 { margin: 18px 0 6px; font-size: 1em; }
img { max-width: 100%; height: auto; }
.badges img { margin-right: 4px; vertical-align: middle; }
p.note, figcaption, footer { color: #59636e; font-size: 0.9em; }
table { border-collapse: collapse; width: 100%; }
th, td { border: 1px solid #d0d7de; padding: 2px 6px; vertical-align: middle; font-size: 14px; }
td:first-child { overflow-wrap: break-word; }
th { background: #f6f8fa; text-align: left; font-weight: 600; }
td.n, th.n { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
td.bar { width: 24%; min-width: 60px; }
tr.total td { font-weight: 600; background: #f6f8fa; }
.bar span { display: flex; height: 13px; background: #8c959f; }
.bar i { display: block; height: 100%; }
i.v { background: #2da44e; } i.r { background: #d4a72c; } i.p { background: #a5d6a7; }
.key i { display: inline-block; width: 11px; height: 11px; margin: 0 4px 0 10px; vertical-align: -1px; }
.key i.o { background: #8c959f; }
.grid { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: 24px; }
.grid > div { min-width: 0; overflow-x: auto; }
details { margin-top: 8px; } summary { cursor: pointer; color: #0969da; }
footer { margin-top: 36px; border-top: 1px solid #d0d7de; padding-top: 8px; }
@media (max-width: 760px) { .grid { grid-template-columns: 1fr; } td.bar { min-width: 50px; } }
""".strip()


# ---------------------------------------------------------------- data

class SourceFiles:
    """Address -> (library, source file) from the runs of symbols/source_files.csv."""

    def __init__(self, root: str):
        self.runs = sorted((int(r['address'], 16), int(r['last'], 16), r['library'], r['file'])
                           for r in pm.read_csv(os.path.join(root, 'symbols', 'source_files.csv')))
        self.starts = [r[0] for r in self.runs]

    def of(self, address: int) -> tuple[str, str]:
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and address <= self.runs[i][1]:
            return self.runs[i][2], self.runs[i][3]
        return '', ''


def by_source_file(root: str, rows: list[dict]) -> list[dict]:
    """Per named source file: its library, functions and bytes, replaced and verified functions.
    Sorted by replaced, then verified, then size."""
    files = SourceFiles(root)
    out: dict[str, dict] = {}

    def entry(address: str) -> dict | None:
        lib, name = files.of(int(address, 16))
        if not name:
            return None
        return out.setdefault(name, {'file': name, 'library': lib, 'functions': 0, 'bytes': 0,
                                     'replaced': 0, 'verified': 0})
    for r in pm.read_csv(os.path.join(root, 'symbols', 'ghidra_functions.csv')):
        e = entry(r['address'])
        if e:
            e['functions'] += 1
            e['bytes'] += int(r['size'])
    for r in rows:
        if r['status'] in progress.IMPLEMENTED and (e := entry(r['address'])):
            e['replaced'] += 1
            e['verified'] += r['status'] == 'verified'
    return sorted(out.values(), key=lambda e: (-e['replaced'], -e['verified'], -e['bytes'], e['file']))


def reviewed(root: str, rows: list[dict]) -> int:
    """Verified functions with an approving independent review recorded."""
    approved = {r['address'] for r in pm.read_csv(os.path.join(root, 'symbols', 'reviews.csv'))
                if r['verdict'] == 'approved'}
    for f in pm.fam.families(root):
        path = os.path.join(root, 'symbols', 'families', f"{f['family']}.reviews.csv")
        if any(r.get('verdict') == 'approved' for r in pm.read_csv(path)):
            approved |= {r['address'] for r in rows if r.get('family') == f['family']}
    return sum(r['address'] in approved for r in rows if r['status'] == 'verified')


def imports(root: str) -> dict:
    """symbols/imports.csv by library: the executable's imports (what symbols/boot.json counts),
    and every importer's."""
    rows = pm.read_csv(os.path.join(root, 'symbols', 'imports.csv'))

    def tally(selected: list[dict], key: str) -> list[dict]:
        out: dict[str, dict] = {}
        for r in selected:
            e = out.setdefault(r[key], {'name': r[key], 'imports': 0, 'runtime': 0, 'module': 0, 'missing': 0})
            e['imports'] += 1
            if r['provider'] == 'runtime':
                e['runtime'] += 1
            elif r['provider'] and not r['provider'].startswith('runtime'):
                e['module'] += 1
            else:
                e['missing'] += 1
        return sorted(out.values(), key=lambda e: (-e['imports'], e['name'].lower()))
    executable = [r for r in rows if r['importer'] == 'eboot']
    return {'executable': tally(executable, 'library'), 'importers': tally(rows, 'importer')}


def open_pr_gains(root: str, prs: list[dict], rows: list[dict]) -> dict:
    """What open pull requests would add on top of main: per pull request and, without counting a
    function twice, per library. A function counts when a pull request raises its status."""
    on_main = {r['address']: RANK.get(r['status'], 0) for r in rows}
    libs = pm.Libraries(root)
    best: dict[str, int] = {}
    per_pr = []
    for pr in sorted(prs, key=lambda p: p['number']):
        gains = {}
        for r in pr.get('functions', []):
            rank = RANK.get(r.get('status', ''), 0)
            if rank > on_main.get(r['address'], 0):
                gains[r['address']] = max(rank, gains.get(r['address'], 0))
        for a, rank in gains.items():
            best[a] = max(rank, best.get(a, 0))
        per_pr.append({'number': pr['number'], 'title': pr['title'], 'author': pr.get('author', ''),
                       'draft': bool(pr.get('draft')), 'error': pr.get('error') or '',
                       'replaced': sum(a not in on_main or on_main[a] == 0 for a in gains),
                       'verified': sum(rank == RANK['verified'] for rank in gains.values())})
    by_lib: dict[str, dict] = {}
    for a, rank in best.items():
        e = by_lib.setdefault(libs.of(int(a, 16)), {'replaced': 0, 'verified': 0})
        e['replaced'] += on_main.get(a, 0) == 0
        e['verified'] += rank == RANK['verified']
    return {'prs': per_pr, 'libraries': by_lib,
            'replaced': sum(e['replaced'] for e in by_lib.values()),
            'verified': sum(e['verified'] for e in by_lib.values())}


def compute(root: str, prs: list[dict] | None = None, source_sha: str = '') -> dict:
    """Everything the page shows, as the JSON published beside it."""
    data = progress.compute(root)
    rows = pm.status_rows(root)
    names = pm.Names(root)
    libs = pm.library_totals(root, rows)
    order = sorted(libs, key=lambda k: (-libs[k]['bytes'], k))
    out = {
        'schema': 1,
        'source_sha': source_sha,
        'total': data['total'],
        'replaced': data['replaced'],
        'edge_verified': data['edge_verified'],
        'verified': data['verified'],
        'libraries': [{'library': k, 'name': names.name(k), 'short': names.short(k), **libs[k]} for k in order],
        'systems': data['systems'],
        'source_files': by_source_file(root, rows),
        'evidence': {**pm.evidence(root, rows), 'approved by an independent review': reviewed(root, rows)},
        'families': data['families'],
        'runtime': data['runtime'],
        'imports': imports(root),
    }
    if prs is not None:
        out['open_prs'] = open_pr_gains(root, prs, rows)
    return out


# ---------------------------------------------------------------- open pull requests (network)

def gh(*args: str) -> str:
    return subprocess.run(['gh', *args], check=True, capture_output=True, text=True).stdout


def table_rows(text: str) -> list[dict]:
    """address, size, status of each row of a function table (functions.csv or a family's members)."""
    out = []
    for r in csv.DictReader(io.StringIO(text)):
        a, status = (r.get('address') or '').strip(), (r.get('status') or '').strip()
        if re.fullmatch(r'0x[0-9a-fA-F]+', a) and status in RANK:
            out.append({'address': a.lower(), 'status': status})
    return out


def fetch_open_prs(repo: str) -> list[dict]:
    """Open pull requests and the function tables at their heads (data only)."""
    listing = json.loads(gh('pr', 'list', '-R', repo, '--state', 'open', '--limit', '200',
                            '--json', 'number,title,author,isDraft,files'))
    out = []
    for pr in sorted(listing, key=lambda p: p['number']):
        tables = sorted(f['path'] for f in pr.get('files') or [] if TABLE_RE.match(f['path']))
        entry = {'number': pr['number'], 'title': pr['title'], 'author': (pr.get('author') or {}).get('login', ''),
                 'draft': pr['isDraft'], 'functions': [], 'error': ''}
        for path in tables:
            try:
                text = gh('api', '-H', 'Accept: application/vnd.github.raw',
                          f'repos/{repo}/contents/{path}?ref=refs/pull/{pr["number"]}/head')
            except subprocess.CalledProcessError as e:
                entry['error'] = f'{path}: could not be read ({e.returncode})'
                continue
            entry['functions'] += table_rows(text)
        if tables or entry['error']:
            out.append(entry)
    return out


# ---------------------------------------------------------------- markup

def esc(s: object) -> str:
    return pm.esc(str(s))


def num(n: int) -> str:
    return f'<td class="n">{n:,}</td>'


def pct_cell(part: int, whole: int) -> str:
    return f'<td class="n">{pm.pct_text(part, whole)}</td>'


def bar(whole: int, *segments: tuple[int, str]) -> str:
    """A bar cell: segments of (value, class) laid left to right over a grey track; any non-zero
    segment stays visible."""
    parts = []
    for value, cls in segments:
        if value and whole:
            parts.append(f'<i class="{cls}" style="width:{max(0.4, 100 * value / whole):.2f}%"></i>')
    title = ', '.join(f'{v:,}' for v, _ in segments) + f' of {whole:,}'
    return f'<td class="bar" title="{esc(title)}"><span>{"".join(parts)}</span></td>'


def table(head: list[str], body: list[str], numeric_from: int = 1) -> str:
    cells = ''.join(f'<th class="n">{esc(h)}</th>' if i >= numeric_from and h else f'<th>{esc(h)}</th>'
                    for i, h in enumerate(head))
    return '<table>\n<tr>' + cells + '</tr>\n' + '\n'.join(body) + '\n</table>'


def key(*items: tuple[str, str]) -> str:
    return '<p class="note key">' + ''.join(f'<i class="{c}"></i>{esc(t)}' for c, t in items) + '</p>'


def library_tables(d: dict) -> str:
    libs = d['libraries']
    t, r, v = d['total'], d['replaced'], d['verified']

    def rows(unit: str) -> list[str]:
        suffix = '' if unit == 'functions' else '_bytes'
        whole = 'functions' if unit == 'functions' else 'bytes'
        fmt = (lambda n: num(n)) if unit == 'functions' else (lambda n: f'<td class="n">{pm.mb(n)}</td>')
        out = [f'<tr><td title="{esc(e["name"])}">{esc(e["short"])}</td>{fmt(e[whole])}{fmt(e["replaced" + suffix])}{fmt(e["verified" + suffix])}'
               f'{pct_cell(e["verified" + suffix], e[whole])}'
               f'{bar(e[whole], (e["verified" + suffix], "v"), (e["replaced" + suffix] - e["verified" + suffix], "r"))}</tr>'
               for e in libs]
        tw, tr, tv = (t['functions'], r['functions'], v['functions']) if unit == 'functions' else \
            (t['bytes'], r['bytes'], v['bytes'])
        out.append(f'<tr class="total"><td>All</td>{fmt(tw)}{fmt(tr)}{fmt(tv)}{pct_cell(tv, tw)}'
                   f'{bar(tw, (tv, "v"), (tr - tv, "r"))}</tr>')
        return out
    return ('<h2 id="libraries">Decompilation by library</h2>\n'
            + key(('v', 'verified'), ('r', 'replaced, not yet verified'), ('o', 'not replaced yet'))
            + '<div class="grid">\n<div><h3>Functions</h3>\n'
            + table(['Library', 'Functions', 'Replaced', 'Verified', '% verified', ''], rows('functions'))
            + '</div>\n<div><h3>Bytes of code</h3>\n'
            + table(['Library', 'Bytes', 'Replaced', 'Verified', '% verified', ''], rows('bytes'))
            + '</div>\n</div>\n'
            + '<p class="note">The library of each function comes from the source files it was compiled from '
              '(symbols/source_files.csv); code no library claims yet is counted on its own row.</p>')


def source_tables(d: dict) -> str:
    systems = sorted(d['systems'].items(), key=lambda kv: (-kv[1]['replaced'], kv[0]))
    sys_rows = [f'<tr><td>{esc(k)}</td>{num(s["replaced"])}{num(s["edge_verified"])}{num(s["verified"])}'
                f'{bar(s["replaced"], (s["verified"], "v"), (s["edge_verified"] - s["verified"], "p"))}</tr>'
                for k, s in systems]

    def file_row(e: dict) -> str:
        return (f'<tr><td>{esc(e["file"])}</td>{num(e["functions"])}{num(e["replaced"])}{num(e["verified"])}'
                f'{bar(e["functions"], (e["verified"], "v"), (e["replaced"] - e["verified"], "r"))}</tr>')
    files = d['source_files']
    head = ['Source file', 'Functions', 'Replaced', 'Verified', '']
    top = table(head, [file_row(e) for e in files[:TOP_FILES]])
    rest = ''
    if len(files) > TOP_FILES:
        rest = (f'<details><summary>The other {len(files) - TOP_FILES:,} source files</summary>\n'
                + table(head, [file_row(e) for e in files[TOP_FILES:]]) + '\n</details>\n')
    fams = d['families']['families']
    fam_rows = [f'<tr><td title="{esc(f["shape"])}">{esc(k)}</td>{num(f["members"])}'
                f'{num(f["verified"])}{bar(f["members"], (f["verified"], "v"), (f["edge_verified"] - f["verified"], "p"))}</tr>'
                for k, f in sorted(fams.items(), key=lambda kv: (-kv[1]['members'], kv[0]))]
    return ('<h2 id="systems">By game system and source file</h2>\n'
            + key(('v', 'verified'), ('p', 'edge-verified only'), ('o', 'replaced only'))
            + '<div class="grid">\n<div><h3>Replaced functions by system</h3>\n'
            + table(['System', 'Replaced', 'Edge-verified', 'Verified', ''], sys_rows)
            + '<p class="note">Systems are the game\'s subsystems a replaced function belongs to '
              '(symbols/functions.csv); "leaf" holds the function families.</p>\n</div>\n'
            + f'<div><h3 id="families">Function families ({len(fams)})</h3>\n'
            + table(['Family', 'Members', 'Verified', ''], fam_rows)
            + '<p class="note">A family is many tiny identical functions that share one replacement '
              '(symbols/families.csv); they move the function count far more than the byte count.</p>\n'
              '</div>\n</div>\n'
            + '<h3 id="source-files">By source file (most replaced first)</h3>\n'
            + key(('v', 'verified'), ('r', 'replaced, not yet verified'), ('o', 'not replaced yet'))
            + top + '\n' + rest
            + f'<p class="note">{len(files):,} source files are named so far; their functions are placed by '
              'address in symbols/source_files.csv.</p>')


def verification_tables(d: dict) -> str:
    v = d['verified']['functions']
    ev = [f'<tr><td>{esc(k[:1].upper() + k[1:])}</td>{num(n)}{num(v)}{bar(v, (n, "v"))}</tr>'
          for k, n in d['evidence'].items()]
    summary = []
    t = d['total']
    for label, k in (('Replaced (builds and runs in place of the original)', 'replaced'),
                     ('Edge-verified (passes generated edge cases)', 'edge_verified'),
                     ('Verified', 'verified')):
        summary.append(f'<tr><td>{esc(label)}</td>{num(d[k]["functions"])}{num(t["functions"])}'
                       f'{bar(t["functions"], (d[k]["functions"], "v"))}</tr>')
    return ('<h2 id="evidence">Verification</h2>\n<div class="grid">\n<div><h3>Functions by level</h3>\n'
            + table(['Measure', 'Count', 'Of', ''], summary)
            + '<p class="note">Each level includes the next: replaced, then edge-verified, then verified.</p>\n'
              '</div>\n<div><h3>Verified functions by evidence</h3>\n'
            + table(['Measure', 'Count', 'Of', ''], ev)
            + '<p class="note">A function counts as verified when it passes on its edge cases and on inputs '
              'recorded in the game, or matches the original bytes exactly, and an independent review has '
              'approved it. A function can carry several kinds of evidence.</p>\n</div>\n</div>')


def runtime_tables(d: dict) -> str:
    rt = d['runtime']

    def rows(entries: list[dict]) -> list[str]:
        out = [f'<tr><td>{esc(e["name"] or "(unnamed)")}</td>{num(e["imports"])}{num(e["runtime"])}{num(e["module"])}'
               f'{pct_cell(e["runtime"] + e["module"], e["imports"])}'
               f'{bar(e["imports"], (e["runtime"], "v"), (e["module"], "p"))}</tr>' for e in entries]
        tot = {k: sum(e[k] for e in entries) for k in ('imports', 'runtime', 'module')}
        out.append(f'<tr class="total"><td>All</td>{num(tot["imports"])}{num(tot["runtime"])}{num(tot["module"])}'
                   f'{pct_cell(tot["runtime"] + tot["module"], tot["imports"])}'
                   f'{bar(tot["imports"], (tot["runtime"], "v"), (tot["module"], "p"))}</tr>')
        return out
    imp = d['imports']
    if not imp['executable']:
        return '<h2 id="runtime">Runtime: system imports</h2>\n<p>No import table committed yet.</p>'
    status = ''
    if rt:
        status = (f'<p>The last recorded boot reached milestone <b>{esc(rt["milestone"])}</b> '
                  f'({rt["milestone_index"]} of {rt["milestones"]}); outcome: {esc(rt["outcome"])}. '
                  f'Of the executable\'s {rt["imports_total"]:,} imports, the runtime implements '
                  f'{rt["imports_implemented"]:,} and the bundled modules provide {rt["imports_bundled"]:,}: '
                  f'{progress.fmt_pct(rt["imports_pct"])} are provided.</p>\n')
    head = ['Library', 'Imports', 'Runtime', 'Module', '% provided', '']
    return ('<h2 id="runtime">Runtime: system imports by library</h2>\n' + status
            + key(('v', 'implemented by the runtime'), ('p', 'provided by a bundled module'), ('o', 'not provided'))
            + '<h3>The executable\'s imports</h3>\n'
            + table(head, rows(imp['executable']))
            + '\n<h3>By importer (the executable and its bundled modules)</h3>\n'
            + table(['Importer', 'Imports', 'Runtime', 'Module', '% provided', ''], rows(imp['importers']))
            + '<p class="note">From symbols/imports.csv and symbols/boot.json, written by tools/boot.py from a run '
              'of the boot harness. An import counts as implemented when the runtime binds it; how complete '
              'each implementation is shows in the boot milestones, not here.</p>')


def open_pr_section(d: dict, repo_url: str) -> str:
    if 'open_prs' not in d:
        return ('<h2 id="open-prs">Potential progress with open pull requests</h2>\n'
                '<p class="note">Not computed for this build (no open pull request data).</p>')
    op = d['open_prs']
    lib_rows = []
    for e in d['libraries']:
        g = op['libraries'].get(e['library'], {'replaced': 0, 'verified': 0})
        if not g['replaced'] and not g['verified']:
            continue
        lib_rows.append(f'<tr><td>{esc(e["name"])}</td>{num(e["replaced"])}{num(e["replaced"] + g["replaced"])}'
                        f'{num(e["verified"])}{num(e["verified"] + g["verified"])}{num(e["functions"])}'
                        f'{bar(e["functions"], (e["replaced"], "v"), (g["replaced"], "p"))}</tr>')

    def pr_link(n: int) -> str:
        return f'<a href="{esc(repo_url)}/pull/{n}">#{n}</a>' if repo_url else f'#{n}'
    pr_rows = [f'<tr><td>{pr_link(p["number"])}</td><td>{esc(p["title"])}{" (draft)" if p["draft"] else ""}'
               f'{" <em>" + esc(p["error"]) + "</em>" if p["error"] else ""}</td><td>{esc(p["author"])}</td>'
               f'{num(p["replaced"])}{num(p["verified"])}</tr>' for p in op['prs'] if p['replaced'] or p['verified'] or p['error']]
    body = ('<h2 id="open-prs">Potential progress with open pull requests</h2>\n'
            f'<p><b>Not merged.</b> Open pull requests would add {op["replaced"]:,} replaced and '
            f'{op["verified"]:,} verified functions on top of main (a function several pull requests touch '
            'counts once). Read from the function tables at each pull request\'s head when this page was built; '
            'review can still change or reject them.</p>\n')
    if lib_rows:
        body += (key(('v', 'replaced on main'), ('p', 'added by open pull requests'), ('o', 'not replaced'))
                 + table(['Library', 'Replaced', 'With open PRs', 'Verified', 'With open PRs', 'Functions', ''], lib_rows))
    if pr_rows:
        body += (f'\n<details><summary>{len(pr_rows)} pull requests that change the function tables</summary>\n'
                 + table(['Pull request', 'Title', 'Author', 'Replaced', 'Verified'], pr_rows, numeric_from=3)
                 + '\n</details>')
    return body


def render_html(d: dict, repo_url: str = '', wiki_url: str = '') -> str:
    def link(path: str) -> str:
        return f'{repo_url}/blob/main/{path}' if repo_url else path
    t, r, v = d['total'], d['replaced'], d['verified']
    built = f' This build: main <code>{esc(d["source_sha"][:12])}</code>.' if d['source_sha'] else ''
    footer_links = [f'<a href="{esc(link("STATUS.md"))}">STATUS.md</a>']
    if wiki_url:
        footer_links.append(f'<a href="{esc(wiki_url)}">the documentation site</a>')
    if repo_url:
        footer_links.append(f'<a href="{esc(repo_url)}">the repository</a>')
    badges = ''.join(f'<img src="{b}" alt="{esc(b[6:-4])}">' for b in pm.BADGES)
    return '\n'.join([
        '<!DOCTYPE html>',
        '<html lang="en">',
        '<head>',
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        '<title>Paleblood progress</title>',
        f'<style>\n{CSS}\n</style>',
        '</head>',
        '<body>',
        '<h1>Progress</h1>',
        f'<p>Generated by <a href="{esc(link("tools/progress_page.py"))}">tools/progress_page.py</a> after every '
        f'push to <code>main</code>, daily, and when the Pages workflow is run manually.{built} '
        'Raw numbers: <a href="progress.json">progress.json</a>.</p>',
        f'<p class="badges">{badges}</p>',
        f'<p><b>{pm.pct_text(v["functions"], t["functions"])}</b> of the executable\'s {t["functions"]:,} functions '
        f'are verified ({v["functions"]:,}) and {pm.pct_text(r["functions"], t["functions"])} replaced '
        f'({r["functions"]:,}); {pm.pct_text(r["bytes"], t["bytes"])} of its {pm.mb(t["bytes"])} of code is replaced.</p>',
        '<figure style="margin:0"><a href="progress.svg"><img src="progress.svg" alt="Progress map: the executable\'s '
        'code by library, its area in bytes; green where verified, amber where replaced, grey where not yet '
        'replaced"></a></figure>',
        library_tables(d),
        verification_tables(d),
        source_tables(d),
        runtime_tables(d),
        open_pr_section(d, repo_url),
        '<footer>',
        '<p>Counted from the committed tables under symbols/ (no game data). Paleblood is a fan project and is '
        'not affiliated with or endorsed by the game\'s publisher or developer. The page layout follows the '
        'progress page of the AnyPS5 project. More: ' + ' · '.join(footer_links) + '.</p>',
        '</footer>',
        '</body>',
        '</html>',
    ]) + '\n'


def render_all(root: str, prs: list[dict] | None = None, source_sha: str = '', repo_url: str = '',
               wiki_url: str = '') -> dict[str, str]:
    """File name -> text; nothing for a tree without the Ghidra export."""
    if not os.path.isfile(os.path.join(root, 'symbols', 'ghidra_functions.csv')):
        return {}
    d = compute(root, prs, source_sha)
    return {PAGE: render_html(d, repo_url.rstrip('/'), wiki_url),
            DATA: json.dumps(d, indent=1, sort_keys=True) + '\n'}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=progress.DEFAULT_ROOT)
    ap.add_argument('--out-dir')
    ap.add_argument('--open-prs', help='JSON from --fetch-open-prs')
    ap.add_argument('--source-sha', default='')
    ap.add_argument('--repo-url', default='')
    ap.add_argument('--wiki-url', default='')
    ap.add_argument('--fetch-open-prs', metavar='FILE', help='write the open pull requests\' function tables')
    ap.add_argument('--repo', help='OWNER/NAME for --fetch-open-prs')
    a = ap.parse_args()
    if a.fetch_open_prs:
        if not a.repo:
            ap.error('--fetch-open-prs needs --repo')
        prs = fetch_open_prs(a.repo)
        with open(a.fetch_open_prs, 'w') as f:
            json.dump(prs, f, indent=1, sort_keys=True)
        print(f'progress page: {len(prs)} open pull requests change the function tables')
        return 0
    if not a.out_dir:
        ap.error('--out-dir is required')
    prs = None
    if a.open_prs:
        with open(a.open_prs) as f:
            prs = json.load(f)
    out = render_all(a.root, prs, a.source_sha, a.repo_url, a.wiki_url)
    if not out:
        print('progress page: symbols/ghidra_functions.csv is missing', file=sys.stderr)
        return 1
    os.makedirs(a.out_dir, exist_ok=True)
    for name, text in out.items():
        with open(os.path.join(a.out_dir, name), 'w') as f:
            f.write(text)
        print(f'progress page: {name} ({len(text.encode())} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
