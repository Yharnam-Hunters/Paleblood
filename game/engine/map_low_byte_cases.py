#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure low-byte/upper-word inputs; no expected return or output model."""
import json
from pathlib import Path
import sys

ABI = {'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16, 'returns': {'type': 'uint64_t'}, 'execution_limits': {'timeout_seconds': 5}, 'return_carrier': 'Existing frontend uint64_t maps to fixture i64; all64 RAX bits retained', 'scope': 'Mechanical GP/memory wire only; original API/prototype/field meanings, runtime reachability and computed callers unproved', 'observation_limits': 'Caller-clobbered GP/FP equivalence not claimed; native reads, same-value and restored temporary writes uninstrumented', 'error_limits': '875 prior errors:281 explicit points and594 unattributed LSDA owners remain', 'evidence': ['Complete18B canonical body; all33 selected targets, raw unwind and own first14 branch bound', 'Only DIL consumed; MOVSX EAX,DIL then EAX architectural zero-extension defines all64 result bits', 'Parent accepted mechanical wire; no known code caller/original prototype proof'], 'arguments': [{'register': 'rdi', 'type': 'uint64_t value; low8 only consumed', 'direction': 'input'}, {'register': 'rsi', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}, {'register': 'rdx', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}, {'register': 'rcx', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}, {'register': 'r8', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}, {'register': 'r9', 'type': 'uint64_t ignored incoming GP seed; not a source formal', 'direction': 'input'}], 'source_formal_slots': ['rdi'], 'input_regions': ['probe'], 'writable_regions': [], 'memory_contract': 'No memory reads or writes; supplied canary buffer remains unchanged', 'return_contract': 'Selected byte sign-extended to32, then zero-extended to64; no full64 signed extension', 'hook_limit': 'Own branch source+7 to target+13 lies inside first14 bytes; future hook handling required'}


def case(cid, value, incoming=None, pattern=None):
    if not 0 <= value <= 0xffffffffffffffff:
        raise ValueError('incoming GP value outside64 bits')
    args = {register:'0x0' for register in ('rdi','rsi','rdx','rcx','r8','r9')}
    args['rdi'] = hex(value)
    if 'rdi' in (incoming or {}):
        raise ValueError('ignored seeds cannot replace the formal input')
    args.update(incoming or {})
    canary = bytes(((0x53 + 19 * index) & 255) if pattern is None else pattern for index in range(64))
    return {'schema':1,'address':'0x011c48f0','id':cid,'returns':'i64','args':args,
            'buffers':{'probe':{'size':64,'bytes':canary.hex()}},'memory':[],'imports':[],'stubs':[]}


CASES = [case(f'byte_{value:02x}',value) for value in range(256)]
UPPER_SEEDS = [0x100,0x1234567800000000,0xffffffffffffff00,0x8000000000000000]
LOW_BYTES = [0x00,0x40,0x41,0x5a,0x5b,0x7f,0x80,0xff]
for index, upper in enumerate(UPPER_SEEDS):
    CASES.extend(case(f'upper_seed{index}_byte_{value:02x}',upper | value) for value in LOW_BYTES)
CASES.extend([
    case('ignored_gp_all_bits',0x41,{'rsi':'0xffffffffffffffff','rdx':'0x1234567887654321','rcx':'0x8000000000000000','r8':'0xfeedface','r9':'0x7fffffffffffffff'}),
    case('ignored_gp_mapped_canaries',0x80,{'rsi':'buf:probe+0x1','rdx':'buf:probe+0x9','rcx':'buf:probe+0x11','r8':'buf:probe+0x19','r9':'buf:probe+0x21'}),
    case('ignored_gp_zero_canary',0xff,{'rsi':'0x1','rdx':'0xffffffffffffffff','rcx':'0x2','r8':'0x8000000000000000','r9':'0x12345678ffffffff'},pattern=0),
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
