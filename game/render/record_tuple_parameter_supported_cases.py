#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise tuple classification, threshold and parameter choices.

Synthetic inputs compare both implementations without expected answers.
"""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x0149e900'
RETURN = 'i64'
INPUTS = [{'args': [0], 'words': [], 'returns': 'i64'},
 {'args': [1], 'words': [], 'returns': 'i64'},
 {'args': [2], 'words': [], 'returns': 'i64'},
 {'args': [3], 'words': [], 'returns': 'i64'},
 {'args': [4], 'words': [], 'returns': 'i64'},
 {'args': [7], 'words': [], 'returns': 'i64'},
 {'args': [8], 'words': [], 'returns': 'i64'},
 {'args': [15], 'words': [], 'returns': 'i64'},
 {'args': [16], 'words': [], 'returns': 'i64'},
 {'args': [31], 'words': [], 'returns': 'i64'},
 {'args': [32], 'words': [], 'returns': 'i64'},
 {'args': [63], 'words': [], 'returns': 'i64'},
 {'args': [64], 'words': [], 'returns': 'i64'},
 {'args': [127], 'words': [], 'returns': 'i64'},
 {'args': [128], 'words': [], 'returns': 'i64'},
 {'args': [255], 'words': [], 'returns': 'i64'},
 {'args': [256], 'words': [], 'returns': 'i64'},
 {'args': [2147483647], 'words': [], 'returns': 'i64'},
 {'args': [2147483648], 'words': [], 'returns': 'i64'},
 {'args': [4294967295], 'words': [], 'returns': 'i64'},
 {'args': [256], 'words': [], 'returns': 'i64'},
 {'args': [512], 'words': [], 'returns': 'i64'},
 {'args': [1024], 'words': [], 'returns': 'i64'},
 {'args': [2048], 'words': [], 'returns': 'i64'},
 {'args': [4096], 'words': [], 'returns': 'i64'},
 {'args': [8192], 'words': [], 'returns': 'i64'},
 {'args': [16384], 'words': [], 'returns': 'i64'},
 {'args': [32768], 'words': [], 'returns': 'i64'},
 {'args': [65536], 'words': [], 'returns': 'i64'},
 {'args': [131072], 'words': [], 'returns': 'i64'},
 {'args': [262144], 'words': [], 'returns': 'i64'},
 {'args': [524288], 'words': [], 'returns': 'i64'},
 {'args': [1048576], 'words': [], 'returns': 'i64'},
 {'args': [2097152], 'words': [], 'returns': 'i64'},
 {'args': [4194304], 'words': [], 'returns': 'i64'},
 {'args': [8388608], 'words': [], 'returns': 'i64'},
 {'args': [16777216], 'words': [], 'returns': 'i64'},
 {'args': [33554432], 'words': [], 'returns': 'i64'},
 {'args': [67108864], 'words': [], 'returns': 'i64'},
 {'args': [134217728], 'words': [], 'returns': 'i64'},
 {'args': [268435456], 'words': [], 'returns': 'i64'},
 {'args': [536870912], 'words': [], 'returns': 'i64'},
 {'args': [1073741824], 'words': [], 'returns': 'i64'},
 {'args': [2147483648], 'words': [], 'returns': 'i64'},
 {'args': [65537], 'words': [], 'returns': 'i64'},
 {'args': [65539], 'words': [], 'returns': 'i64'},
 {'args': [305419896], 'words': [], 'returns': 'i64'}]

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
