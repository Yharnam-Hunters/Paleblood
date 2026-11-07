#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise component arrangements, single-code rejection and aliased outputs."""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x014717a0'
RETURN = 'i8'
INPUTS = [{'args': ['p33', 'p33'], 'words': [(33, 16433153)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 16433155)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 16433158)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 16433171)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 16433211)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 16433407)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917057)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917059)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917062)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917075)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917115)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 15917311)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924609)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924611)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924614)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924627)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924667)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 9924863)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053377)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053379)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053382)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053395)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053435)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 14053631)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16385)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16387)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16390)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16403)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16443)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16639)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131073)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131075)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131078)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131091)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131131)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 131327)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048577)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048579)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048582)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048595)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048635)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1048831)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388609)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388611)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388614)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388627)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388667)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8388863)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180225)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180227)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180230)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180243)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180283)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 180479)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502145)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502147)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502150)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502163)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502203)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 10502399)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151553)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151555)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151558)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151571)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151611)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 151807)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409089)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409091)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409094)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409107)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409147)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 8409343)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753089)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753091)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753094)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753107)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753147)], 'returns': 'i8'},
 {'args': ['p33', 'p33'], 'words': [(33, 1753343)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763137)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763139)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763142)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763155)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763195)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 12763391)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1236993)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1236995)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1236998)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1237011)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1237051)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1237247)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577025)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577027)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577030)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577043)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577083)], 'returns': 'i8'},
 {'args': ['p33', 0], 'words': [(33, 8577279)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 1)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 3)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 6)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 19)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 59)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 255)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773121)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773123)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773126)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773139)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773179)], 'returns': 'i8'},
 {'args': ['p33', 'p128'], 'words': [(33, 16773375)], 'returns': 'i8'}]

# Each tuple lists exactly the comparisons required by one ordered arrangement.
# Inputs 1/3/6 select the single/pair/triple rules; 255 selects the four-code fallback.
# The remaining lanes use 0, which matches none of the required codes 4 through 7.
ARRANGEMENT_RULES = (
    ('single', 1, (((0, 4),), ((1, 4),), ((2, 4),), ((3, 4),))),
    ('pair', 3, (((0, 4), (1, 5)), ((0, 4), (3, 5)),
                 ((1, 4), (0, 5)), ((3, 4), (0, 5)))),
    ('triple', 6, (((0, 4), (1, 5), (2, 6)), ((0, 4), (1, 5), (3, 6)),
                   ((2, 4), (1, 5), (0, 6)), ((3, 4), (1, 5), (0, 6)))),
    ('quad', 255, (((0, 4), (1, 5), (2, 6), (3, 7)),
                   ((2, 4), (1, 5), (0, 6), (3, 7)),
                   ((3, 4), (2, 5), (1, 6), (0, 7)),
                   ((3, 4), (0, 5), (1, 6), (2, 7)))),
)


def isolated_mismatch_inputs():
    for family, encoding, arrangements in ARRANGEMENT_RULES:
        for arrangement, required in enumerate(arrangements):
            for wrong_lane, _ in required:
                lanes = [0, 0, 0, 0]
                for lane, code in required:
                    lanes[lane] = code
                lanes[wrong_lane] = 0
                packed = encoding
                for lane, code in enumerate(lanes):
                    packed |= code << (12 + lane * 3)
                yield {'id': f'{family}_arrangement_{arrangement}_wrong_{"abcd"[wrong_lane]}',
                       'args': ['p33', 'p128'], 'words': [(33, packed)]}


INPUTS.extend(isolated_mismatch_inputs())

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
        case={'schema':1,'address':ADDRESS,'id':inputs.get('id','edge_'+format(index,'04d')),'returns':inputs.get('returns',RETURN),'args':args,'stack':values[6:],'buffers':{'arena':{'size':len(arena),'bytes':arena.hex()}},'memory':([{'addr':'0x057e3be0','pointer':'arena+0x1c0'}] if inputs.get('guard') else []),'imports':[],'stubs':([{'address':'0x02abb4a0','argc':1,'ret':'0x0'}] if inputs.get('report_stub') else [])}
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
