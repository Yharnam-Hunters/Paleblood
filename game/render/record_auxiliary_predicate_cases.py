#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored synthetic wire inputs. No expected values or game recordings.

Retained complete original analysis establishes input locations and memory extents.
Cases vary numeric admissions, masking boundaries and caller-owned canaries.
Snapshot comparisons do not prove race/fault behavior or restored temporary writes.
"""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x02abb4f0'
RETURN = 'i64'
INPUTS = [{'args': [0, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [0, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [0, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [0, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [64, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [64, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [64, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [128, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [128, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [128, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 0], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 18], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 19], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 20], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 21], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 22], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 23], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}},
 {'args': [4294967295, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 0}},
 {'args': [4294967295, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 1311768467463790320}},
 {'args': [4294967295, 4294967295], 'words': [], 'returns': 'i64', 'incoming': {'rax': 18446744073709551615}}]

def fixtures():
    registers = ['rdi','rsi','rdx','rcx','r8','r9']
    for index, inputs in enumerate(INPUTS):
        arena = bytearray((0x35+29*i)&255 for i in range(512))
        for offset,value in inputs.get('words',[]):
            struct.pack_into('<I',arena,offset,value&0xffffffff)
        def wire(value):
            return 'buf:arena+0x'+format(int(value[1:]),'x') if isinstance(value,str) and value.startswith('p') else hex(value)
        values=[wire(v) for v in inputs['args']]
        args={register:(values[i] if i<len(values) else '0x0') for i,register in enumerate(registers)}
        args.update({k:wire(v) for k,v in inputs.get('incoming',{}).items()})
        case={'schema':1,'address':ADDRESS,'id':'edge_'+format(index,'04d'),'returns':inputs.get('returns',RETURN),'args':args,'stack':values[6:],'buffers':{'arena':{'size':len(arena),'bytes':arena.hex()}},'memory':([{'addr':'0x057e3be0','pointer':'arena+0x1c0'}] if inputs.get('guard') else []),'imports':[],'stubs':([{'address':'0x02abb4a0','argc':1,'ret':'0x0'}] if inputs.get('report_stub') else [])}
        yield case

def main():
    destination=Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=False)
    cases=list(fixtures())
    for case in cases:
        (destination/(case['id']+'.json')).write_text(json.dumps(case,indent=1)+'\n')
    count=max(6,max(len(x['args']) for x in INPUTS))
    abi={'calling_convention':'sysv_amd64','confidence':'supported','stack_alignment':16,'arguments':[({'register':['rdi','rsi','rdx','rcx','r8','r9'][i]} if i<6 else {'stack_index':i-6})|{'type':'uint64_t native wire position; documented low-bit or borrowed-buffer input','direction':'input'} for i in range(count)],'returns':{'type':{'i8':'uint8_t','i32':'uint32_t','i64':'uint64_t','void':'void'}[RETURN]},'input_regions':['arena'],'writable_regions':['arena'],'execution_limits':{'timeout_seconds':10},'evidence':['Retained complete original instruction contract at '+ADDRESS,'Synthetic inputs only; native helper calls and original constant tables retained'],'scope':'Normally returning mapped byte views; no genuine game recordings, race/fault equivalence, or independent review claimed'}
    abi['arguments'].append({'register':'rax','type':'uint64_t ambient incoming RAX; upper bits retained on selected selector paths','direction':'input'})
    (destination/'abi.json').write_text(json.dumps(abi,indent=1)+'\n')
    print(str(len(cases))+' synthetic cases written')

if __name__=='__main__':
    main()
