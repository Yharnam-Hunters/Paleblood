#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The runtime track's claimable issues: one per system call group, from symbols/imports.csv.

usage: runtime_issues.py [--group KEY] [--json]

Each group is a set of PS4 system libraries, in the runtime roadmap's order. The issue body lists
every function of those libraries the executable imports, with what is implemented, so a
contributor can claim a group (or part of one). Imports of the executable and of the game's own
modules count once per library and symbol; one is done when a module or the runtime provides it.
Every number comes from symbols/imports.csv (written by tools/boot.py). --json prints
[{key, title, body}]; otherwise the bodies as text.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# key, title, libraries, what the group is about. Libraries the game ships as its own modules
# (libc, libSceFios2) are mapped from those modules first, so they are not listed here.
GROUPS = (
    ('kernel', 'Kernel, threads and memory',
     ('libkernel', 'libScePosix', 'libSceSysmodule', 'libSceLibcInternal'),
     'Processes, threads and synchronisation, memory (direct and flexible memory, mapping), time, '
     'events and equeues, module loading, and the system C library the game\'s own modules call '
     '(libSceLibcInternal).'),
    ('files', 'Files and saves',
     ('libSceSaveData', 'libSceSaveDataDialog', 'libSceAppContent', 'libScePlayGo', 'libSceDiscMap'),
     'Save data, app content and PlayGo: the game reads its data through the bundled libSceFios2, '
     'which calls libkernel\'s file functions (kernel group).'),
    ('input', 'Input and users',
     ('libScePad', 'libSceMouse', 'libSceUserService', 'libSceImeDialog'),
     'Controllers, mouse, the signed-in user and the on-screen keyboard.'),
    ('audio', 'Audio',
     ('libSceAudioOut', 'libSceAudioIn', 'libSceAjm', 'libSceVoice', 'libSceAvPlayer'),
     'Audio output and input, audio decoding (Ajm), voice chat and video playback.'),
    ('video', 'Video out',
     ('libSceVideoOut',),
     'Display buffers, flips and flip events.'),
    ('gnm', 'GNM (GPU commands)',
     ('libSceGnmDriver',),
     'Command buffer submission and GPU state. Graphics are rebuilt at the engine level, so this '
     'group is about what the engine expects back, not about emulating the GPU.'),
    ('system', 'System services and dialogs',
     ('libSceSystemService', 'libSceRtc', 'libSceCommonDialog', 'libSceMsgDialog', 'libSceNpProfileDialog'),
     'System parameters and events, the real-time clock and system dialogs.'),
    ('network', 'Network and online services (offline)',
     ('libSceNet', 'libSceNetCtl', 'libSceHttp', 'libSceSsl', 'libSceNpCommon', 'libSceNpManager',
      'libSceNpMatching2', 'libSceNpWebApi', 'libSceNpScore', 'libSceNpSignaling', 'libSceNpUtility',
      'libSceNpTrophy', 'libSceNpAuth', 'libSceNpCommerce'),
     'Networking and PSN. The runtime is offline: these report "not connected" or "signed out" the '
     'way the system does, so the game takes its offline paths.'),
)
BUNDLED = ('libc', 'libSceFios2')


def read_imports(root: str) -> list[dict]:
    """One row per (library, symbol): kind, implemented ("yes" when a module or the runtime
    provides it), and who imports it. Plain-named references with no library ("?") are left out."""
    path = os.path.join(root, 'symbols', 'imports.csv')
    if not os.path.isfile(path):
        raise SystemExit('runtime_issues: no symbols/imports.csv; run tools/boot.py')
    rows: dict[tuple, dict] = {}
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            if r['library'] == '?':
                continue
            row = rows.setdefault((r['library'], r['symbol']), {'library': r['library'], 'symbol': r['symbol'],
                                                                'kind': r['kind'], 'implemented': 'no',
                                                                'importers': set()})
            row['importers'].add(r['importer'])
            if r['provider']:
                row['implemented'] = 'yes'
    return [rows[k] for k in sorted(rows)]


def check_coverage(rows: list[dict]) -> list[str]:
    """Libraries in imports.csv that no group (and no bundled module) covers."""
    known = {lib for g in GROUPS for lib in g[2]} | set(BUNDLED)
    return sorted({r['library'] for r in rows} - known)


def issue(group: tuple, rows: list[dict]) -> dict:
    key, title, libs, about = group
    mine = [r for r in rows if r['library'] in libs]
    done = sum(r['implemented'] == 'yes' for r in mine)
    lines = [f'Runtime track, system call group **{title}** ({done} of {len(mine)} imported functions '
             f'implemented). {about}', '',
             'Claim the group, or one library of it, by commenting here. Implement in '
             f'`runtime/syslib/{key}.c` with `RT_SYSLIB` entries (`runtime/include/runtime/syslib.h`); clean room: '
             'public documentation, your own reverse engineering and observed behaviour only, no code copied '
             'from or modelled line by line on another emulator (CONTRIBUTING.md). Behaviour you do not know '
             'fails loudly with the function\'s name. `tools/boot.py` shows how far the boot gets.', '']
    for lib in libs:
        fns = [r for r in mine if r['library'] == lib]
        if not fns:
            continue
        lines.append(f"### {lib} ({sum(r['implemented'] == 'yes' for r in fns)} of {len(fns)})")
        lines.append('')
        for r in fns:
            mark = 'x' if r['implemented'] == 'yes' else ' '
            kind = ' (data)' if r['kind'] == 'data' else ''
            by = '' if r['importers'] == {'eboot'} else ' (used by ' + ', '.join(sorted(r['importers'])) + ')'
            lines.append(f"- [{mark}] `{r['symbol']}`{kind}{by}")
        lines.append('')
    lines.append('Generated by `tools/runtime_issues.py` from `symbols/imports.csv`.')
    return {'key': key, 'title': f'Runtime: {title}', 'body': '\n'.join(lines) + '\n'}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--group')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    rows = read_imports(ROOT)
    missing = check_coverage(rows)
    if missing:
        print(f"runtime_issues: libraries in no group: {', '.join(missing)}", file=sys.stderr)
        return 1
    out = [issue(g, rows) for g in GROUPS if not a.group or g[0] == a.group]
    if a.json:
        json.dump(out, sys.stdout, indent=1)
        print()
    else:
        for i in out:
            print(f"## {i['title']}\n\n{i['body']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
