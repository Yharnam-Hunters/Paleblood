#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic entry-manager lookup fixtures; these are not gameplay recordings.

Every declared object has canaries, and lookup must leave all bytes unchanged. Invalid
table/handle pointers on rejected paths make premature dereferences observable failures.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x01314320'
MANAGER_OFFSET = 8
HANDLE_OFFSET = 8
TABLE_FIELD = 0x18
CAPACITY_FIELD = 0xac
ENABLED_FIELD = 0x108
MANAGER_BYTES = 0x120
MASK64 = (1 << 64) - 1
UNALIGNED_GUEST_TABLE = 0x058b0bb9


def word(value):
    """Encode one complete 64-bit handle or opaque entry value."""
    return struct.pack('<Q', value & MASK64).hex()


def preset(address, value):
    """Initialize a pointer slot with a relocatable fixture reference or literal bits."""
    if isinstance(value, str) and value.startswith('buf:'):
        return {'addr': address, 'pointer': value[4:]}
    return {'addr': address, 'bytes': word(value)}


def case(cid, handle=1, enabled=1, capacity=4, entry=None, handle_pointer=None,
         table_pointer=None, handle_offset=HANDLE_OFFSET):
    """Construct a read-only lookup with guarded manager, handle, table and entry storage."""
    manager = 'buf:manager'
    table = 'buf:table'
    fixture = {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
        'args': {'rdi': f'{manager}+{MANAGER_OFFSET:#x}',
                 'rsi': handle_pointer if handle_pointer is not None
                 else f'buf:handle+{handle_offset:#x}'},
        'buffers': {
            'manager': {'size': MANAGER_BYTES, 'bytes': 'a5' * MANAGER_BYTES},
            'handle': {'size': 32, 'bytes': 'b6' * 32},
            'table': {'size': 56, 'bytes': 'c7' * 56},
            'entry0': {'size': 24, 'bytes': 'd0' * 24},
            'entry1': {'size': 24, 'bytes': 'd1' * 24},
            'entry2': {'size': 24, 'bytes': 'd2' * 24},
            'entry3': {'size': 24, 'bytes': 'd3' * 24},
        },
        'memory': [
            preset(f'{manager}+{MANAGER_OFFSET + TABLE_FIELD:#x}',
                   table if table_pointer is None else table_pointer),
            {'addr': f'{manager}+{MANAGER_OFFSET + CAPACITY_FIELD:#x}',
             'bytes': struct.pack('<I', capacity).hex()},
            {'addr': f'{manager}+{MANAGER_OFFSET + ENABLED_FIELD:#x}',
             'bytes': bytes([enabled]).hex()},
            {'addr': f'buf:handle+{handle_offset:#x}', 'bytes': word(handle)},
        ],
        'imports': [],
    }
    for index in range(4):
        value = entry if index == (handle & 0xffffffff) and entry is not None else f'buf:entry{index}'
        fixture['memory'].append(preset(f'buf:table+{index * 8:#x}', value))
    return fixture


CASES = [
    case('first_entry', handle=0),
    case('middle_entry', handle=1),
    case('last_entry', handle=3),
    case('disabled_skips_null_handle_and_table', enabled=0, handle_pointer='0x0', table_pointer=0),
    case('enabled_nonboolean_byte', enabled=2),
    case('enabled_high_bit_byte', enabled=0x80),
    case('zero_capacity_skips_null_table', handle=0, capacity=0, table_pointer=0),
    case('index_equals_capacity_skips_null_table', handle=4, table_pointer=0),
    case('index_above_capacity_skips_null_table', handle=5, table_pointer=0),
    case('unsigned_index_high_bit_rejected', handle=0x80000000, table_pointer=0),
    case('unsigned_capacity_high_bit', handle=1, capacity=0x80000001),
    case('full_handle_sentinel_skips_null_table', handle=MASK64, table_pointer=0),
    case('upper_word_all_ones_is_not_full_sentinel', handle=0xffffffff00000001),
    case('handle_sign_bit_does_not_change_index', handle=0x8000000000000002),
    case('missing_entry_full_sentinel', entry=MASK64),
    case('null_entry_returned', entry=0),
    case('entry_low_word_all_ones_is_not_full_sentinel', entry=0x00000000ffffffff),
    case('opaque_entry_upper_bits_preserved', entry=0x1234567887654321),
    case('entry_aliases_handle_storage', entry='buf:handle', handle_offset=0),
    case('unaligned_handle_storage', handle_offset=9),
]

# The handle occupies one table slot but selects another; lookup must not rewrite either.
aliased = case('handle_aliases_another_table_slot', handle_pointer='buf:table')
aliased['memory'].append(preset('buf:table', 1))
CASES.append(aliased)

# The table shares the guarded manager buffer's prefix and otherwise unknown fields.
aliased = case('table_aliases_manager_storage', table_pointer='buf:manager')
for index in range(4):
    aliased['memory'].append(preset(f'buf:manager+{index * 8:#x}', f'buf:entry{index}'))
CASES.append(aliased)

# The handle's upper word is also the manager's capacity; its low word still selects slot1.
aliased = case('handle_aliases_capacity_upper_word', handle_pointer='buf:manager+0xb0')
aliased['memory'].append(preset('buf:manager+0xb0', (4 << 32) | 1))
CASES.append(aliased)

# One load sees the enabled byte; the later handle load sees all eight bytes at that address.
aliased = case('handle_aliases_enabled_byte', handle_pointer='buf:manager+0x110')
aliased['memory'].append(preset('buf:manager+0x110', 0xfedcba9800000001))
CASES.append(aliased)

# Guest-address pointer presets permit an unaligned table without pointer-offset presets.
# This isolated fixture owns the canary region; the resolver itself has no global reads.
unaligned = case('unaligned_table_in_guest_memory')
unaligned['memory'][0] = {'addr': 'buf:manager+0x20', 'guest': hex(UNALIGNED_GUEST_TABLE)}
unaligned['memory'].append({'addr': hex(UNALIGNED_GUEST_TABLE - 9), 'bytes': 'e8' * 56})
for index in range(4):
    unaligned['memory'].append(preset(hex(UNALIGNED_GUEST_TABLE + index * 8), f'buf:entry{index}'))
CASES.append(unaligned)


def main():
    """Write the constructed fixture set into the caller-selected private directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
