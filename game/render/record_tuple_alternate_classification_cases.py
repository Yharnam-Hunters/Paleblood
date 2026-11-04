#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise tuple classification, threshold and parameter choices.

Synthetic inputs compare both implementations without expected answers.
"""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x0149ea70'
RETURN = 'i32'
INPUTS = [{'args': [0], 'words': []},
 {'args': [1], 'words': []},
 {'args': [2], 'words': []},
 {'args': [3], 'words': []},
 {'args': [4], 'words': []},
 {'args': [7], 'words': []},
 {'args': [8], 'words': []},
 {'args': [15], 'words': []},
 {'args': [16], 'words': []},
 {'args': [31], 'words': []},
 {'args': [32], 'words': []},
 {'args': [63], 'words': []},
 {'args': [64], 'words': []},
 {'args': [127], 'words': []},
 {'args': [128], 'words': []},
 {'args': [255], 'words': []},
 {'args': [256], 'words': []},
 {'args': [2147483647], 'words': []},
 {'args': [2147483648], 'words': []},
 {'args': [4294967295], 'words': []}]

# Focused review boundaries complement the original authored cases.
EXTRA_INPUTS = [{'id': 'classification_05', 'args': [5], 'words': []},
 {'id': 'classification_06', 'args': [6], 'words': []},
 {'id': 'classification_09', 'args': [9], 'words': []},
 {'id': 'classification_10', 'args': [10], 'words': []},
 {'id': 'classification_11', 'args': [11], 'words': []},
 {'id': 'classification_12', 'args': [12], 'words': []},
 {'id': 'classification_13', 'args': [13], 'words': []},
 {'id': 'classification_14', 'args': [14], 'words': []}]

def fixtures():
    registers = ['rdi','rsi','rdx','rcx','r8','r9']
    for index, inputs in enumerate(INPUTS + EXTRA_INPUTS):
        arena = bytearray((0x35+29*i)&255 for i in range(512))
        for offset,value in inputs.get('words',[]):
            struct.pack_into('<I',arena,offset,value&0xffffffff)
        def wire(value):
            return 'buf:arena+0x'+format(int(value[1:]),'x') if isinstance(value,str) and value.startswith('p') else hex(value)
        values=[wire(v) for v in inputs['args']]
        args={register:(values[i] if i<len(values) else '0x0') for i,register in enumerate(registers)}
        args.update({k:wire(v) for k,v in inputs.get('incoming',{}).items()})
        case={'schema':1,'address':ADDRESS,'id':inputs.get('id', 'edge_'+format(index,'04d')),'returns':inputs.get('returns',RETURN),'args':args,'stack':values[6:],'buffers':{'arena':{'size':len(arena),'bytes':arena.hex()}},'memory':[],'imports':[],'stubs':[]}
        yield case

def main():
    destination=Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=False)
    cases=list(fixtures())
    for case in cases:
        (destination/(case['id']+'.json')).write_text(json.dumps(case,indent=1)+'\n')
    count=max(6,max(len(x['args']) for x in INPUTS))
    abi={'calling_convention':'sysv_amd64','confidence':'supported','stack_alignment':16,'arguments':[({'register':['rdi','rsi','rdx','rcx','r8','r9'][i]} if i<6 else {'stack_index':i-6})|{'type':'uint64_t argument; see the function declaration for its width','direction':'input'} for i in range(count)],'returns':{'type':{'i8':'uint8_t','i32':'uint32_t','i64':'uint64_t','void':'void'}[RETURN]},'input_regions':['arena'],'writable_regions':['arena'],'execution_limits':{'timeout_seconds':10},'evidence':['Original function at '+ADDRESS,'Synthetic inputs only; native helper calls and original constant tables retained'],'scope':'Normally returning mapped byte views; no genuine game recordings, race/fault equivalence, or independent review claimed'}
    (destination/'abi.json').write_text(json.dumps(abi,indent=1)+'\n')
    print(str(len(cases))+' synthetic cases written')

if __name__=='__main__':
    main()
