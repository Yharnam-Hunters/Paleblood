#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the wide path token scanner (game/loading/path_token.cpp), for tools/verify.py run.

usage: path_token_cases.py OUT_DIR

Each case lays out a cursor (position, delimiters, characters left) over synthetic UTF-16 text
and a delimiter set in their own buffers. wcschr is a scripted import: the generator walks the
text as the scanner does and scripts each call's result (the delimiter's address in the set, the
set's terminator for a 0 character, or null).
"""
import json
import os
import struct
import sys

ADDRESS = '0x02a2aca0'


def utf16(text):
    return text.encode('utf-16-le').hex()


def scripted_scan(text, start, delimiters, remaining):
    """The wcschr calls the scanner makes, in order, as (character, index in the set or None)."""
    calls = []

    def find(ch):
        if ch == '\0':
            index = len(delimiters)
        else:
            index = delimiters.index(ch) if ch in delimiters else None
        calls.append((ch, index))
        return index is not None

    at = start
    while remaining and find(text[at]):
        at += 1
        remaining -= 1
    if text[at] == '\0' or not remaining:
        return calls
    while remaining:
        if find(text[at]):
            break
        at += 1
        remaining -= 1
    return calls


def case(cid, text, delimiters=':', start=0, remaining=None, null_position=False, null_delimiters=False):
    text_bytes = text + '\0' * 8   # what follows the counted range is readable
    if remaining is None:
        remaining = len(text)
    cursor = struct.pack('<QQQ', 0, 0, remaining).hex()
    memory = []
    if not null_position:
        memory.append({'addr': 'buf:cursor+0', 'pointer': f'text+{2 * start}'})
    if not null_delimiters:
        memory.append({'addr': 'buf:cursor+8', 'pointer': 'delimiters+0'})
    imports = []
    if not null_position and not null_delimiters:
        for ch, index in scripted_scan(text_bytes, start, delimiters, remaining):
            imports.append({'name': 'wcschr', 'argc': 2,
                            'ret': f'buf:delimiters+{2 * index}' if index is not None else 0})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i128',
            'args': {'rdi': 'buf:cursor'},
            'buffers': {'cursor': {'size': 24, 'bytes': cursor},
                        'text': {'size': 2 * len(text_bytes), 'bytes': utf16(text_bytes)},
                        'delimiters': {'size': 2 * len(delimiters) + 2, 'bytes': utf16(delimiters + '\0')}},
            'memory': memory, 'imports': imports}


def cases():
    return [
        case('null_position', 'data:/x', null_position=True),
        case('null_delimiters', 'data:/x', null_delimiters=True),
        case('nothing_left', 'abc', remaining=0),
        case('mount_name', 'data:/param/x.bin'),
        case('leading_separators', '::abc:def'),
        case('only_separators', '::::'),
        case('separators_then_nul_counted', ':\0x', remaining=3),
        case('limit_inside_token', 'abcdef', remaining=3),
        case('limit_at_delimiter', 'abc:def', remaining=3),
        case('nul_inside_range', 'ab\0cd', remaining=5),
        case('empty_text', ''),
        case('empty_text_counted', '', remaining=4),
        case('several_delimiters', '/ :a b/c', delimiters=' /:'),
        case('empty_delimiter_set', 'abc:def', delimiters=''),
        case('mid_text', 'title:/sound/x.fsb', start=7),
        case('single_character', 'x'),
        case('delimiter_last', 'name:', remaining=5),
        case('separator_last_limited', 'ab::', start=2, remaining=2),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
