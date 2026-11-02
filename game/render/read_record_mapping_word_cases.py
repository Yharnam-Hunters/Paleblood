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
ADDRESS = '0x01486cf0'
RETURN = 'i64'
INPUTS = [{'args': ['p33'], 'words': [(49, 2902491137)]},
 {'args': ['p33'], 'words': [(49, 2902493185)]},
 {'args': ['p33'], 'words': [(49, 2902495233)]},
 {'args': ['p33'], 'words': [(49, 2902497281)]},
 {'args': ['p33'], 'words': [(49, 2902491393)]},
 {'args': ['p33'], 'words': [(49, 2902493441)]},
 {'args': ['p33'], 'words': [(49, 2902495489)]},
 {'args': ['p33'], 'words': [(49, 2902497537)]},
 {'args': ['p33'], 'words': [(49, 2902492673)]},
 {'args': ['p33'], 'words': [(49, 2902494721)]},
 {'args': ['p33'], 'words': [(49, 2902496769)]},
 {'args': ['p33'], 'words': [(49, 2902498817)]},
 {'args': ['p33'], 'words': [(49, 2902492929)]},
 {'args': ['p33'], 'words': [(49, 2902494977)]},
 {'args': ['p33'], 'words': [(49, 2902497025)]},
 {'args': ['p33'], 'words': [(49, 2902499073)]},
 {'args': ['p33'], 'words': [(49, 2902491141)]},
 {'args': ['p33'], 'words': [(49, 2902493189)]},
 {'args': ['p33'], 'words': [(49, 2902495237)]},
 {'args': ['p33'], 'words': [(49, 2902497285)]},
 {'args': ['p33'], 'words': [(49, 2902491397)]},
 {'args': ['p33'], 'words': [(49, 2902493445)]},
 {'args': ['p33'], 'words': [(49, 2902495493)]},
 {'args': ['p33'], 'words': [(49, 2902497541)]},
 {'args': ['p33'], 'words': [(49, 2902492677)]},
 {'args': ['p33'], 'words': [(49, 2902494725)]},
 {'args': ['p33'], 'words': [(49, 2902496773)]},
 {'args': ['p33'], 'words': [(49, 2902498821)]},
 {'args': ['p33'], 'words': [(49, 2902492933)]},
 {'args': ['p33'], 'words': [(49, 2902494981)]},
 {'args': ['p33'], 'words': [(49, 2902497029)]},
 {'args': ['p33'], 'words': [(49, 2902499077)]},
 {'args': ['p33'], 'words': [(49, 2902491157)]},
 {'args': ['p33'], 'words': [(49, 2902493205)]},
 {'args': ['p33'], 'words': [(49, 2902495253)]},
 {'args': ['p33'], 'words': [(49, 2902497301)]},
 {'args': ['p33'], 'words': [(49, 2902491413)]},
 {'args': ['p33'], 'words': [(49, 2902493461)]},
 {'args': ['p33'], 'words': [(49, 2902495509)]},
 {'args': ['p33'], 'words': [(49, 2902497557)]},
 {'args': ['p33'], 'words': [(49, 2902492693)]},
 {'args': ['p33'], 'words': [(49, 2902494741)]},
 {'args': ['p33'], 'words': [(49, 2902496789)]},
 {'args': ['p33'], 'words': [(49, 2902498837)]},
 {'args': ['p33'], 'words': [(49, 2902492949)]},
 {'args': ['p33'], 'words': [(49, 2902494997)]},
 {'args': ['p33'], 'words': [(49, 2902497045)]},
 {'args': ['p33'], 'words': [(49, 2902499093)]},
 {'args': ['p33'], 'words': [(49, 2902491165)]},
 {'args': ['p33'], 'words': [(49, 2902493213)]},
 {'args': ['p33'], 'words': [(49, 2902495261)]},
 {'args': ['p33'], 'words': [(49, 2902497309)]},
 {'args': ['p33'], 'words': [(49, 2902491421)]},
 {'args': ['p33'], 'words': [(49, 2902493469)]},
 {'args': ['p33'], 'words': [(49, 2902495517)]},
 {'args': ['p33'], 'words': [(49, 2902497565)]},
 {'args': ['p33'], 'words': [(49, 2902492701)]},
 {'args': ['p33'], 'words': [(49, 2902494749)]},
 {'args': ['p33'], 'words': [(49, 2902496797)]},
 {'args': ['p33'], 'words': [(49, 2902498845)]},
 {'args': ['p33'], 'words': [(49, 2902492957)]},
 {'args': ['p33'], 'words': [(49, 2902495005)]},
 {'args': ['p33'], 'words': [(49, 2902497053)]},
 {'args': ['p33'], 'words': [(49, 2902499101)]},
 {'args': ['p33'], 'words': [(49, 2902491197)]},
 {'args': ['p33'], 'words': [(49, 2902493245)]},
 {'args': ['p33'], 'words': [(49, 2902495293)]},
 {'args': ['p33'], 'words': [(49, 2902497341)]},
 {'args': ['p33'], 'words': [(49, 2902491453)]},
 {'args': ['p33'], 'words': [(49, 2902493501)]},
 {'args': ['p33'], 'words': [(49, 2902495549)]},
 {'args': ['p33'], 'words': [(49, 2902497597)]},
 {'args': ['p33'], 'words': [(49, 2902492733)]},
 {'args': ['p33'], 'words': [(49, 2902494781)]},
 {'args': ['p33'], 'words': [(49, 2902496829)]},
 {'args': ['p33'], 'words': [(49, 2902498877)]},
 {'args': ['p33'], 'words': [(49, 2902492989)]},
 {'args': ['p33'], 'words': [(49, 2902495037)]},
 {'args': ['p33'], 'words': [(49, 2902497085)]},
 {'args': ['p33'], 'words': [(49, 2902499133)]},
 {'args': ['p33'], 'words': [(49, 2902491261)]},
 {'args': ['p33'], 'words': [(49, 2902493309)]},
 {'args': ['p33'], 'words': [(49, 2902495357)]},
 {'args': ['p33'], 'words': [(49, 2902497405)]},
 {'args': ['p33'], 'words': [(49, 2902491517)]},
 {'args': ['p33'], 'words': [(49, 2902493565)]},
 {'args': ['p33'], 'words': [(49, 2902495613)]},
 {'args': ['p33'], 'words': [(49, 2902497661)]},
 {'args': ['p33'], 'words': [(49, 2902492797)]},
 {'args': ['p33'], 'words': [(49, 2902494845)]},
 {'args': ['p33'], 'words': [(49, 2902496893)]},
 {'args': ['p33'], 'words': [(49, 2902498941)]},
 {'args': ['p33'], 'words': [(49, 2902493053)]},
 {'args': ['p33'], 'words': [(49, 2902495101)]},
 {'args': ['p33'], 'words': [(49, 2902497149)]},
 {'args': ['p33'], 'words': [(49, 2902499197)]}]

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
