#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate FMOD output port-state callback cases for the real poll and its boundary."""
import json
from pathlib import Path
import sys

from update_output_port_state_cases import cases as port_state_cases


CALLBACK = '0x0111b230'
POLL = '0x0111bb10'
CONTEXT_OFFSET = 0x38


def boundary(label, context, result):
    return {'schema': 1, 'address': CALLBACK, 'id': label, 'returns': 'i32',
            'args': {'rdi': context, 'rsi': '0x1234567800000040'},
            'buffers': {'owner': {'size': 192, 'bytes': 'a5' * 192}},
            'stubs': [{'address': POLL, 'argc': 1, 'ret': hex(result)}]}


def cases():
    """Run each existing port-state case through the callback and exercise its argument contract."""
    rows = []
    for poll in port_state_cases():
        owner = poll['args']['rdi']
        name, _, relative = owner.partition('+')
        assert name == 'buf:owner'
        base = int(relative, 0) if relative else 0
        poll['args']['rdi'] = f'buf:owner+{base + CONTEXT_OFFSET}'
        poll['address'] = CALLBACK
        poll['id'] = 'poll-' + poll['id']
        rows.append(poll)
    for name, context in [('owned', 'buf:owner+72'), ('null', 0), ('below-bias', 1),
                          ('bias', 56), ('high', '0xabcd123400000058'),
                          ('upper', '0xffffffffffffffff')]:
        for result in (0, 1, 0x100, 0x80000000, 0x100000000, 0xfedcba9876543210):
            rows.append(boundary(f'{name}-{result:x}', context, result))
    once = boundary('poll-once', 'buf:owner+64', 1)
    once['stubs'][0]['writes'] = [{'arg': 0, 'bytes': '10203040'}]
    once['stubs'].append({'address': POLL, 'argc': 1, 'ret': 2,
                          'writes': [{'arg': 0, 'bytes': '8090a0b0'}]})
    rows.append(once)
    return rows


def main():
    """Write the synthetic fixtures to the selected directory."""
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (output / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
