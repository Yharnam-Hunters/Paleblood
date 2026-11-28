#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Readability lint for game/ (STYLE.md): fails on transcription patterns.

usage: check_readable.py [--root DIR] [--base REF | --base-list FILE] [FILE...]

Checks every C and C++ file under game/ (or the files given) for:
  - calls by address: rt::fn<, a cast of an address literal, an address literal in an expression
  - offset access: get<...>(p, N) / put<...>(p, N) / ptr_at(p, N) with a number, p[0x..] indexing
  - recording code: rec. , capture JSON ("schema", "stubs", "argmem"), rt_capture_*
  - SSE intrinsics (_mm_*) outside game/engine/
  - offset arithmetic in disguise: a byte-pointer cast (char / unsigned char / std::byte / uint8_t *) of an
    object plus a nonzero literal or a constant declared in the same file, directly or through a variable the cast
    was stored in (whole-object memcpy and byte arrays are fine; `+ sizeof(...)`/`offsetof(...)` and
    `*size*` constants and variables are not flagged)
  - magic numbers: numeric literals other than 0 and 1 outside a named-constant declaration
    (constexpr / const / enum / static_assert / alignas / padding members / RT_ORIGINAL /
    RT_GLOBAL lines)
Comments and string literals are ignored, except that capture JSON in strings counts.

Files written before the rule are listed in tools/readable_allowlist.txt and skipped. The list may
only shrink: an entry that is no longer needed (the file passes, or is gone) is an error, and with
--base REF (CI: the target branch) or --base-list FILE (pre-commit: HEAD's list) an entry that is
not in that list is an error.

The offset-arithmetic check has its own shrink-only list, tools/offset_arithmetic_allowlist.txt. The
whole tree is also checked for one declaration per game address: an address declared by more than
one RT_GLOBAL / RT_ORIGINAL (the same global or function declared again in another file) fails;
declare it once in a header and include it. Addresses that were already duplicated when the check was
added are in game/duplicate_declarations_allowlist.txt, which may only shrink as well.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWLIST = 'tools/readable_allowlist.txt'
OFFSET_ALLOWLIST = 'tools/offset_arithmetic_allowlist.txt'
DUPLICATE_ALLOWLIST = 'game/duplicate_declarations_allowlist.txt'
SOURCES = ('.c', '.cc', '.cpp', '.h', '.hpp')

NUMBER = re.compile(r'(?<![\w.])(0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\d*\.\d+(?:[eE][-+]?\d+)?[fF]?|\d+[uUlL]*)(?![\w.])')
ALLOWED_NUMBERS = {'0', '1', '0u', '1u', '0.0f', '1.0f', '0.0', '1.0', '0.f', '1.f', '0ull', '1ull', '0ul', '1ul'}
DECLARATION = re.compile(r'^\s*(?:(?:static|inline|extern)\s+)*(?:constexpr\b|const\s+\w[\w:<>, *&]*\s+\w+\s*(?:\[[^\]]*\])?\s*=)'
                         r'|^\s*static_assert\b|^\s*enum\b|\balignas\s*\(|^\s*RT_(?:ORIGINAL|GLOBAL)\s*\('
                         r'|^\s*(?:uint8_t|char|unsigned char|std::byte|void\s*\*)\s*unknown_\w+\s*\[|^\s*#')
RULES = (
    ('call by address', re.compile(r'\brt::fn\s*<')),
    ('call by address', re.compile(r'reinterpret_cast\s*<[^>]*\(\s*\*\s*\)[^>]*>\s*\(\s*0[xX][0-9a-fA-F]+')),
    ('call by address', re.compile(r'\(\s*\w[\w\s*]*\(\s*\*\s*\)\s*\([^)]*\)\s*\)\s*0[xX][0-9a-fA-F]+')),
    ('offset access', re.compile(r'\b(?:get|put)\s*<[^>]+>\s*\([^;]*,\s*(?:0[xX][0-9a-fA-F]+|\d+)')),
    ('offset access', re.compile(r'\bptr_at\s*\([^;]*,\s*(?:0[xX][0-9a-fA-F]+|\d+)')),
    ('offset access', re.compile(r'\w\s*\[\s*0[xX][0-9a-fA-F]+\s*\]')),
    ('recording code', re.compile(r'\brec\.')),
    ('recording code', re.compile(r'\brt_capture_\w+')),
)
JSON_IN_STRING = re.compile(r'\\"(?:schema|stubs|argmem|argc|buffers|memory|writes)\\"')
INTRINSIC = re.compile(r'\b_mm(?:256|512)?_\w+')
PADDING = re.compile(r'^\s*(?:uint8_t|char|unsigned char|std::byte|void\s*\*)\s*unknown_\w+\s*\[[^\]]*\]\s*;')


def strip(text: str) -> tuple[str, str]:
    """(code without comments and strings, the string literals' contents), same line structure."""
    code, strings = [], []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            code.append(''.join(ch for ch in text[i:j] if ch == '\n'))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            strings.append(text[i + 1:j])
            code.append(c + c + ''.join(ch for ch in text[i:j] if ch == '\n'))
            i = j + 1
        else:
            code.append(c)
            i += 1
    return ''.join(code), '\n'.join(strings)


def problems(path: str, text: str) -> list[str]:
    rel = path
    in_engine = rel.replace(os.sep, '/').startswith('game/engine/')
    code, strings = strip(text)
    out = []
    if JSON_IN_STRING.search(strings) or JSON_IN_STRING.search(text):
        out.append(f'{rel}: recording code (capture JSON in a string)')
    in_declaration = False      # a named-constant declaration continues until its ';'
    for number, line in enumerate(code.splitlines(), 1):
        for what, rx in RULES:
            if rx.search(line) and not (what == 'offset access' and PADDING.search(line)):
                out.append(f'{rel}:{number}: {what}: {line.strip()[:100]}')
        if not in_engine and INTRINSIC.search(line):
            out.append(f'{rel}:{number}: SSE intrinsic outside game/engine/: {line.strip()[:100]}')
        starts = bool(DECLARATION.search(line))
        if starts or in_declaration:
            in_declaration = (in_declaration or starts) and ';' not in line and not line.lstrip().startswith('#')
            continue
        for m in NUMBER.finditer(line):
            if m.group(1).lower() not in ALLOWED_NUMBERS:
                out.append(f'{rel}:{number}: magic number {m.group(1)} (name it: constexpr, enum or a struct field): '
                           f'{line.strip()[:100]}')
                break
    return out

BYTE_TYPE = r'(?:unsigned\s+char|signed\s+char|char|std::byte|byte|uint8_t|int8_t|u8)'
BYTE_CAST = re.compile(r'(?:(?:reinterpret|static)_cast\s*<\s*(?:const\s+)?(?:volatile\s+)?' + BYTE_TYPE + r'\s*\*\s*(?:const\s*)?>\s*\('
                       r'|\(\s*(?:const\s+)?(?:volatile\s+)?' + BYTE_TYPE + r'\s*\*\s*(?:const\s*)?\)\s*\(?)')
BYTE_VARIABLE = re.compile(r'(?:const\s+)?' + BYTE_TYPE + r'\s*\*\s*(?:const\s+)?(\w+)\s*=\s*(?:(?:reinterpret|static)_cast\s*<[^<>;]*\*\s*>\s*\('
                           r'|\(\s*(?:const\s+)?' + BYTE_TYPE + r'\s*\*\s*\)\s*)')
AUTO_BYTE_VARIABLE = re.compile(r'\bauto\s*(?:const\s*)?\*\s*(?:const\s+)?(\w+)\s*=\s*' + BYTE_CAST.pattern)
CONSTANT_DECLARATION = re.compile(r'\b(?:constexpr|const|enum)\b[^;=(]*?\b(\w+)\s*=\s*(?:0[xX][0-9a-fA-F]+|\d+)')
TEMPLATE_CONSTANT = re.compile(r'\btemplate\s*<[^>;]*?\b(?:size_t|ptrdiff_t|u?int\w*|unsigned(?:\s+int)?)\s+(\w+)')
PLUS_OFFSET = re.compile(r'\s*\+\s*(0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*)\b(?!\s*[(:])')


def after_parenthesis(code: str, i: int) -> int:
    """Index just past the ')' closing the '(' that opened just before code[i]; -1 if unbalanced."""
    depth = 1
    while i < len(code):
        depth += (code[i] == '(') - (code[i] == ')')
        i += 1
        if depth == 0:
            return i
    return -1


def offset_operand(code: str, end: int, constants: set[str]) -> str | None:
    """The offset after `+` at code[end:] when it is a nonzero literal or a constant declared in the same file."""
    m = PLUS_OFFSET.match(code, end)
    if not m:
        return None
    token = m.group(1)
    if token[0].isdigit():
        return None if token.lower().rstrip('ul') in ('0', '1', '0x0', '0x1') else token
    # a *_size constant is a header or record length (payload that follows it), like sizeof
    return token if token in constants and 'size' not in token.lower() else None


def offset_arithmetic(code: str) -> list[tuple[int, str]]:
    """(line, text) for each byte-pointer cast or byte-pointer variable plus a literal or offset constant."""
    constants = set(CONSTANT_DECLARATION.findall(code)) | set(TEMPLATE_CONSTANT.findall(code))
    hits = []
    for m in BYTE_CAST.finditer(code):
        if m.group(0).endswith('('):                # a cast applies to a parenthesised group...
            end = after_parenthesis(code, m.end())
        else:                                       # ...or, for a C cast, to the next operand
            o = re.compile(r'[\w:.>-]+').match(code, m.end())
            end = o.end() if o else -1
        if end > 0 and offset_operand(code, end, constants):
            hits.append((code.count('\n', 0, m.start()) + 1, code[m.start():end + 24].split('\n')[0]))
    for v in [*BYTE_VARIABLE.finditer(code), *AUTO_BYTE_VARIABLE.finditer(code)]:
        use = re.compile(r'(?<![\w.>])' + re.escape(v.group(1)) + r'(?=\s*\+)')
        for u in use.finditer(code, v.end()):
            if offset_operand(code, u.end(), constants):
                hits.append((code.count('\n', 0, u.start()) + 1, code[u.start():u.start() + 60].split('\n')[0]))
    return sorted(set(hits))


def offset_problems(path: str, text: str) -> list[str]:
    code, _ = strip(text)
    return [f'{path}:{line}: offset in disguise: byte-pointer arithmetic with a constant offset '
            f'(describe the object as a struct with named fields instead; STYLE.md "Structs, not offsets"): '
            f'{snippet.strip()[:100]}' for line, snippet in offset_arithmetic(code)]


DECLARATION_OF_ADDRESS = re.compile(r'^[ \t]*RT_(?:GLOBAL|ORIGINAL)\s*\(\s*(0[xX][0-9a-fA-F]+)\s*,\s*(\w+)', re.M)


def duplicate_declarations(files: dict[str, str]) -> dict[int, list[tuple[str, int, str]]]:
    """address -> every (file, line, name) that declares it with RT_GLOBAL or RT_ORIGINAL, when more than one."""
    seen: dict[int, list[tuple[str, int, str]]] = {}
    for rel, text in files.items():
        code, _ = strip(text)
        for m in DECLARATION_OF_ADDRESS.finditer(code):
            seen.setdefault(int(m.group(1), 16), []).append((rel, code.count('\n', 0, m.start()) + 1, m.group(2)))
    return {a: d for a, d in seen.items() if len(d) > 1}


def duplicate_problems(root: str, every: list[str], changed: list[str], allowed: list[str], whole_tree: bool) -> list[str]:
    """One declaration per game address. Reads all of game/ (a duplicate needs both sides); with
    changed files, reports only duplicates that involve one of them."""
    files = {}
    for rel in every:
        with open(os.path.join(root, rel), errors='replace') as f:
            files[rel] = f.read()
    duplicated = duplicate_declarations(files)
    allowed_addresses = {int(x, 16) for x in allowed}
    touched = {f.replace(os.sep, '/') for f in changed}
    out = []
    for address in sorted(duplicated):
        sites = duplicated[address]
        if address in allowed_addresses or not (whole_tree or touched & {s[0] for s in sites}):
            continue
        where = ', '.join(f'{rel}:{line} ({name})' for rel, line, name in sites)
        out.append(f'{sites[0][0]}:{sites[0][1]}: address 0x{address:08x} is declared more than once: {where}; '
                   f'declare it once in a header and include it (STYLE.md "Layout")')
    if whole_tree:
        for address in sorted(allowed_addresses - set(duplicated)):
            out.append(f'{DUPLICATE_ALLOWLIST}: 0x{address:08x} is declared once now; remove it from the list')
    return out


def read_allowlist(text: str) -> list[str]:
    lines = (ln.split('  #')[0].strip() for ln in text.splitlines() if not ln.lstrip().startswith('#'))
    return [ln for ln in lines if ln]


def game_files(root: str) -> list[str]:
    out = []
    for dp, _, fns in os.walk(os.path.join(root, 'game')):
        for fn in fns:
            if fn.endswith(SOURCES):
                out.append(os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, '/'))
    return sorted(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--base', help='git ref whose allowlist this one may not grow beyond')
    ap.add_argument('--base-list', help='a file with the allowlist this one may not grow beyond')
    ap.add_argument('--base-dir', help='a directory holding the base versions of every allowlist, at their repository paths')
    ap.add_argument('files', nargs='*')
    a = ap.parse_args()
    root = a.root
    with open(os.path.join(root, ALLOWLIST)) as f:
        allow = read_allowlist(f.read())
    errors = []
    if len(set(allow)) != len(allow):
        errors.append(f'{ALLOWLIST}: duplicate entries')

    def base_of(list_path: str, explicit: str | None = None) -> str | None:
        """The same list as the base the change started from (None when there is none to compare)."""
        if a.base:
            r = subprocess.run(['git', '-C', root, 'show', f'{a.base}:{list_path}'], capture_output=True, text=True)
            return r.stdout if r.returncode == 0 else None
        for candidate in (explicit, a.base_dir and os.path.join(a.base_dir, list_path)):
            if candidate and os.path.isfile(candidate):
                with open(candidate) as f:
                    return f.read()
        return None

    def read_list(list_path: str) -> list[str]:
        if not os.path.isfile(os.path.join(root, list_path)):
            return []
        with open(os.path.join(root, list_path)) as f:
            return read_allowlist(f.read())

    def check_shrinks(list_path: str, entries: list[str], base: str | None, what: str) -> None:
        if base is not None:
            for g in sorted(set(entries) - set(read_allowlist(base))):
                errors.append(f'{list_path}: {g} was added; the list may only shrink ({what})')

    check_shrinks(ALLOWLIST, allow, base_of(ALLOWLIST, a.base_list), 'make the file readable instead')
    offset_allow = read_list(OFFSET_ALLOWLIST)
    duplicate_allow = read_list(DUPLICATE_ALLOWLIST)
    check_shrinks(OFFSET_ALLOWLIST, offset_allow, base_of(OFFSET_ALLOWLIST), 'describe the object as a struct instead')
    check_shrinks(DUPLICATE_ALLOWLIST, duplicate_allow, base_of(DUPLICATE_ALLOWLIST), 'declare the address once in a header')
    every = game_files(root)
    targets = [f.replace(os.sep, '/') for f in a.files] if a.files else every
    for rel in targets:
        if not rel.startswith('game/') or not rel.endswith(SOURCES):
            continue
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            continue
        with open(path, errors='replace') as f:
            text = f.read()
        if rel not in allow:
            errors += problems(rel, text)
        if rel not in offset_allow:
            errors += offset_problems(rel, text)
    if not a.files:
        for rel in allow:
            path = os.path.join(root, rel)
            if not os.path.isfile(path):
                errors.append(f'{ALLOWLIST}: {rel} no longer exists; remove it from the list')
                continue
            with open(path, errors='replace') as f:
                if not problems(rel, f.read()):
                    errors.append(f'{ALLOWLIST}: {rel} passes now; remove it from the list')
        for rel in offset_allow:
            path = os.path.join(root, rel)
            if not os.path.isfile(path):
                errors.append(f'{OFFSET_ALLOWLIST}: {rel} no longer exists; remove it from the list')
                continue
            with open(path, errors='replace') as f:
                if not offset_problems(rel, f.read()):
                    errors.append(f'{OFFSET_ALLOWLIST}: {rel} has no offset arithmetic now; remove it from the list')
    errors += duplicate_problems(root, every, a.files, duplicate_allow, whole_tree=not a.files)
    for e in errors:
        print(f'readable: {e}', file=sys.stderr)
    if errors:
        print(f'readable: {len(errors)} problem(s); see STYLE.md', file=sys.stderr)
        return 1
    remaining = sum(1 for r in allow)
    print(f'readable: ok ({remaining} grandfathered file(s) left in {ALLOWLIST})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
