#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure owner views and GP input words; no product/return/output oracle."""
import json
from pathlib import Path
import struct
import sys

ABI = {'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16, 'returns': {'type': 'uint64_t'}, 'execution_limits': {'timeout_seconds': 5}, 'return_carrier': 'Existing frontend uint64_t maps to fixture i64; all64 RAX bits retained', 'scope': 'Mechanical GP/memory wire only; original API/prototype/field meanings, runtime reachability and computed callers unproved', 'observation_limits': 'Caller-clobbered GP/FP equivalence not claimed; native reads, same-value and restored temporary writes uninstrumented', 'error_limits': '875 prior errors:281 explicit points and594 unattributed LSDA owners remain', 'evidence': ['Complete15B canonical body and raw unwind; no loads/calls/FP', 'First low32 RSI store+0x18, then low32 EDX*ECX*4 store+0x1c; XOR EAX defines full64 zero', 'One bounded caller sets four GP inputs and consumes EAX;3765B FDE versus3764 saved addresses/one-byte gap'], 'arguments': [{'register': 'rdi', 'type': 'void* writable owner byte view of exactly0x20 bytes; unaligned allowed', 'direction': 'output'}, {'register': 'rsi', 'type': 'uint64_t word_input; low32 consumed', 'direction': 'input'}, {'register': 'rdx', 'type': 'uint64_t factor0; low32 consumed', 'direction': 'input'}, {'register': 'rcx', 'type': 'uint64_t factor1; low32 consumed', 'direction': 'input'}, {'register': 'r8', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}, {'register': 'r9', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}], 'source_formal_slots': ['rdi', 'rsi', 'rdx', 'rcx'], 'input_regions': ['arena', 'ignored'], 'writable_regions': ['arena'], 'memory_contract': 'Owner+0x18 and+0x1c each exact4-byte stores in that order; owner0x20 extent valid writable, no owner reads', 'failure_scope': 'No null/invalid-memory recovery; normally returning valid owner views only', 'store_order': ['owner+0x18/4', 'owner+0x1c/4'], 'ordering_limit': 'Distinct valid fields: snapshot comparison cannot alone prove store order or detect overwritten/same-value temporary stores'}


def case(cid, word, factor0, factor1, owner_offset=16, arena_size=96, incoming=None, pattern=None, initial_words=()):
    if not 0 <= owner_offset <= owner_offset + 0x20 <= arena_size:
        raise ValueError('owner needs its complete mapped writable0x20-byte extent')
    if any(not 0 <= value <= 0xffffffffffffffff for value in (word,factor0,factor1)):
        raise ValueError('incoming GP value outside64 bits')
    arena=bytearray(((0x6d+29*index)&255) if pattern is None else pattern for index in range(arena_size))
    for offset,value in initial_words:
        if not 0 <= offset <= offset+4 <= arena_size:
            raise ValueError('initial input word outside supplied arena')
        arena[offset:offset+4]=struct.pack('<I',value)
    args={'rdi':f'buf:arena+0x{owner_offset:x}','rsi':hex(word),'rdx':hex(factor0),'rcx':hex(factor1),'r8':'0x0','r9':'0x0'}
    if set(incoming or {}) & {'rdi','rsi','rdx','rcx'}:
        raise ValueError('ignored seeds cannot replace formal inputs')
    args.update(incoming or {})
    return {'schema':1,'address':'0x011c4510','id':cid,'returns':'i64','args':args,
            'buffers':{'arena':{'size':arena_size,'bytes':arena.hex()},
                       'ignored':{'size':64,'bytes':bytes((0x27+7*index)&255 for index in range(64)).hex()}},
            'memory':[],'imports':[],'stubs':[]}


INPUT_WORDS = [
    ('ordinary',0x11223344,7,9),
    ('all_zero_words',0,0,0),
    ('first_word_all_bits',0xffffffff,7,9),
    ('factor0_zero',0x89abcdef,0,9),
    ('factor1_zero',0x89abcdef,7,0),
    ('unit_factors',0x01234567,1,1),
    ('all_low_words_all_bits',0xffffffff,0xffffffff,0xffffffff),
    ('factor0_bit31',0x76543210,0x80000000,3),
    ('factor1_bit31',0x76543210,3,0x80000000),
    ('factor0_bit30',0x76543210,0x40000000,1),
    ('factor1_bit30',0x76543210,1,0x40000000),
    ('wrapped_pair',0x10203040,0x7fffffff,0xfffffffe),
    ('mixed_low_words',0xa1b2c3d4,0x12345678,0x9abcdef0),
    ('upper_word_first_only',0xfedcba9811223344,7,9),
    ('upper_word_factor0_only',0x11223344,0x1234567800000007,9),
    ('upper_word_factor1_only',0x11223344,7,0x8000000000000009),
    ('all_upper_words',0x0123456789abcdef,0x12345678fedcba98,0x8765432101234567),
    ('all_full_words_all_bits',0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff),
]
CASES=[case(cid,word,factor0,factor1) for cid,word,factor0,factor1 in INPUT_WORDS]
for offset in [0,1,3,7,17,31]:
    CASES.append(case(f'owner_offset_{offset:02x}',0x11223344,7,9,owner_offset=offset))
CASES.extend([
    case('exact_owner_extent',0xa1b2c3d4,0x12345678,0x9abcdef0,owner_offset=0,arena_size=0x20),
    case('last_owner_extent',0xa1b2c3d4,0x12345678,0x9abcdef0,owner_offset=64),
    case('unaligned_last_owner_extent',0xa1b2c3d4,0x12345678,0x9abcdef0,owner_offset=65,arena_size=97),
    case('zero_canaries',0x11223344,7,9,pattern=0),
    case('all_bits_canaries',0x11223344,7,9,pattern=255),
    case('changed_owner_initial_fields',0x11223344,7,9,initial_words=[(40,0xdeadbeef),(44,0x10203040)]),
    case('ignored_gp_all_bits',0x11223344,7,9,incoming={'r8':'0xffffffffffffffff','r9':'0x8000000000000000'}),
    case('ignored_gp_mapped_canaries',0x11223344,7,9,incoming={'r8':'buf:ignored+0x1','r9':'buf:arena+0x3'}),
])


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR',file=sys.stderr)
        return 2
    destination=Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=False)
    for fixture in CASES:
        (destination/(fixture['id']+'.json')).write_text(json.dumps(fixture,indent=1)+'\n')
    (destination/'abi.json').write_text(json.dumps(ABI,indent=1)+'\n')
    print(f'{len(CASES)} authored input states written to {destination}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
