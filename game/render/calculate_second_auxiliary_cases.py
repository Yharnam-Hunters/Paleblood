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
ADDRESS = '0x0149e570'
RETURN = 'i64'
INPUTS = [{'args': ['p33', 'p44', 'p48', 'p52', 0, 1, 0, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 1, 1, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 1, 4294967295, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 65, 0, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 65, 4294967295, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 4294967295, 0, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 4294967295, 1, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 0, 4294967295, 4294967295, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 1, 1, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 1, 4294967295, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 65, 0, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 65, 1, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 4294967295, 0, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 4294967295, 1, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 1, 4294967295, 4294967295, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 1, 0, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 1, 4294967295, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 65, 0, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 65, 1, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 65, 4294967295, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 4294967295, 1, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 63, 4294967295, 4294967295, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 1, 0, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 1, 1, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 65, 0, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 65, 1, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 65, 4294967295, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 4294967295, 0, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 64, 4294967295, 4294967295, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 1, 0, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 1, 1, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 1, 4294967295, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 65, 1, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 65, 4294967295, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 4294967295, 0, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 65, 4294967295, 1, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 1, 0, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 1, 1, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 1, 4294967295, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 65, 0, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 65, 4294967295, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 4294967295, 0, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 4294967295, 1, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 511, 4294967295, 4294967295, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 1, 1, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 1, 4294967295, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 65, 0, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 65, 1, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 4294967295, 0, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 4294967295, 1, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 512, 4294967295, 4294967295, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 1, 0, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 1, 4294967295, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 65, 0, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 65, 1, 256], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 65, 4294967295, 255], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 4294967295, 1, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p44', 'p48', 'p52', 4294967295, 4294967295, 4294967295, 1], 'words': [], 'returns': 'i64'},
 {'args': [0, 0, 0, 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 0, 0, 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 0, 'p47', 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 0, 'p47', 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 'p40', 0, 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 'p40', 0, 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 'p40', 'p47', 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': [0, 'p40', 'p47', 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 0, 0, 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 0, 0, 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 0, 'p47', 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 0, 'p47', 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p40', 0, 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p40', 0, 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p40', 'p47', 0, 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p40', 'p47', 'p54', 65, 513, 3, 0], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p33', 'p34', 'p35', 65, 513, 3, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p34', 'p35', 'p36', 65, 513, 3, 1], 'words': [], 'returns': 'i64'},
 {'args': ['p33', 'p35', 'p36', 'p37', 65, 513, 3, 1], 'words': [], 'returns': 'i64'}]

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
    (destination/'abi.json').write_text(json.dumps(abi,indent=1)+'\n')
    print(str(len(cases))+' synthetic cases written')

if __name__=='__main__':
    main()
