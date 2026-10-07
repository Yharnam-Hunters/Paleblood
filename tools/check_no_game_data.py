#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject game data, binaries, assets, raw decompiler dumps and images or video of the game.

usage: check_no_game_data.py (--staged | --tracked) [--root DIR]
  --staged   check the git index (pre-commit)
  --tracked  check every tracked file in the working tree (CI)

No screenshots, clips or images of the game, ever: image and video files (by extension or by
their contents) are refused unless they are under docs/assets/ and listed in
docs/assets/ALLOWLIST with the tool that produced them (tool output only, such as a terminal
recording of tools/verify.py). The allowlist itself may list nothing outside docs/assets/.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BAD_EXT = {
    '.elf', '.self', '.sprx', '.prx', '.bin', '.pkg', '.sfo', '.pfs', '.dump', '.rif',
    '.dds', '.gnf', '.at9', '.trp', '.fsb', '.bnd', '.dcx', '.tpf', '.flver', '.bdt', '.bhd',
    '.gzf', '.gpr',
}
MAGICS = {
    b'\x7fELF': 'ELF',
    b'\x4f\x15\x3d\x1d': 'PS4 SELF',
    b'\x7fCNT': 'PS4 PKG',
    b'\x00PSF': 'param.sfo',
    b'BND3': 'BND3 archive',
    b'BND4': 'BND4 archive',
    b'BHD5': 'BHD5 archive',
    b'DCX\x00': 'DCX compressed',
    b'TPF\x00': 'TPF textures',
    b'FSB5': 'FMOD bank',
    b'DDS ': 'DDS texture',
}
MEDIA_EXT = {
    '.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.tga', '.tif', '.tiff', '.apng', '.avif', '.heic',
    '.svg', '.ico', '.mp4', '.m4v', '.mov', '.webm', '.mkv', '.avi', '.wmv', '.flv',
}
MEDIA_MAGICS = (
    (0, b'\x89PNG', 'PNG image'), (0, b'GIF8', 'GIF image'), (0, b'\xff\xd8\xff', 'JPEG image'),
    (4, b'ftyp', 'MP4/QuickTime video'), (0, b'\x1a\x45\xdf\xa3', 'Matroska/WebM video'),
    (8, b'WEBP', 'WebP image'), (8, b'AVI ', 'AVI video'),
)
ASSETS_DIR = 'docs/assets/'
ALLOWLIST = 'docs/assets/ALLOWLIST'
DUMP_LINE = re.compile(rb'\b(undefined[1-8]?|(FUN|DAT|LAB|PTR|SUB|UNK)_[0-9a-fA-F]{6,})\b')
DUMP_THRESHOLD = 20
DUMP_EXEMPT = ('README.md', 'QUIRKS.md', 'CONTRIBUTING.md', 'docs/', 'tools/', 'test/')
MAX_SIZE = 2 * 1024 * 1024
SIZE_EXCEPTIONS = {'symbols/ghidra_functions.csv': 16 * 1024 * 1024}


def git(root: str, *args: str) -> bytes:
    return subprocess.run(['git', '-C', root, *args], check=True, capture_output=True).stdout


def target_hashes(root: str) -> set[str]:
    path = os.path.join(root, 'target.sha256')
    if not os.path.isfile(path):
        return set()
    with open(path) as f:
        return {ln.split()[0] for ln in f if ln.strip()}


def read_allowlist(text: str) -> tuple[set[str], list[str]]:
    """docs/assets/ALLOWLIST: one `path  # tool that produced it` per line."""
    allowed, errors = set(), []
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        path, _, note = line.partition('#')
        path = path.strip()
        if not path.startswith(ASSETS_DIR) or '..' in path.split('/'):
            errors.append(f'{ALLOWLIST}:{n}: {path} is outside {ASSETS_DIR}')
        elif not note.strip():
            errors.append(f'{ALLOWLIST}:{n}: {path} needs `# <tool that produced it>`')
        else:
            allowed.add(path)
    return allowed, errors


def is_media(path: str, data: bytes) -> str | None:
    ext = os.path.splitext(path)[1].lower()
    if ext in MEDIA_EXT:
        return f'extension {ext}'
    for off, magic, what in MEDIA_MAGICS:
        if what and data[off:off + len(magic)] == magic:
            return what
    head = data[:512].lstrip().lower()
    if head.startswith(b'<svg') or (head.startswith(b'<?xml') and b'<svg' in data[:4096].lower()):
        return 'SVG image'
    return None


def scan(path: str, data: bytes, hashes: set[str], allowed: set[str] = frozenset()) -> list[str]:
    out = []
    ext = os.path.splitext(path)[1].lower()
    media = is_media(path, data)
    if media and path not in allowed:
        out.append(f'{path}: {media}: no screenshots, clips or images of the game; only tool output under '
                   f'{ASSETS_DIR}, listed in {ALLOWLIST}')
    if ext in BAD_EXT:
        out.append(f'{path}: extension {ext} is never committed')
    for magic, what in MAGICS.items():
        if data.startswith(magic):
            out.append(f'{path}: starts with {what} magic')
    if hashlib.sha256(data).hexdigest() in hashes:
        out.append(f'{path}: matches a hash in target.sha256')
    limit = SIZE_EXCEPTIONS.get(path, MAX_SIZE)
    if len(data) > limit:
        out.append(f'{path}: {len(data)} bytes exceeds the {limit} byte limit')
    if b'\x00' not in data[:8192] and not path.startswith(DUMP_EXEMPT) and path not in DUMP_EXEMPT:
        n = sum(1 for ln in data.split(b'\n') if DUMP_LINE.search(ln))
        if n >= DUMP_THRESHOLD:
            out.append(f'{path}: {n} lines look like a raw decompiler dump')
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--staged', action='store_true')
    g.add_argument('--tracked', action='store_true')
    ap.add_argument('--root', default=DEFAULT_ROOT)
    a = ap.parse_args()

    hashes = target_hashes(a.root)
    if a.staged:
        names = git(a.root, 'diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z')
        try:
            allow_text = git(a.root, 'show', f':{ALLOWLIST}').decode()
        except subprocess.CalledProcessError:
            allow_text = ''
    else:
        names = git(a.root, 'ls-files', '-z')
        full = os.path.join(a.root, ALLOWLIST)
        allow_text = open(full).read() if os.path.isfile(full) else ''
    allowed, bad = read_allowlist(allow_text)
    gitlinks = {ln.split(b'\t', 1)[1].decode() for ln in git(a.root, 'ls-files', '-s', '-z').split(b'\0')
                if ln.startswith(b'160000 ')}
    for raw in names.split(b'\0'):
        if not raw:
            continue
        path = raw.decode()
        if path in gitlinks:
            continue  # a submodule: its own repository, checked there
        if a.staged:
            data = git(a.root, 'show', f':{path}')
        else:
            full = os.path.join(a.root, path)
            if not os.path.isfile(full) or os.path.islink(full):
                continue
            with open(full, 'rb') as f:
                data = f.read()
        bad += scan(path, data, hashes, allowed)
    for b in bad:
        print(f'game-data: {b}', file=sys.stderr)
    if bad:
        print('game-data: no game files, ever (CONTRIBUTING.md)', file=sys.stderr)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
