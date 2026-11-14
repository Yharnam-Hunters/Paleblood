#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the item dialogs' list copies in game/menu/gaitem_dialog_lists.cpp, for
tools/verify.py run.

usage: gaitem_dialog_lists_cases.py OUT_DIR text_pair|label

- text_pair: slots 0, 1 and the last in use; a slot at and past the in-use count and an empty
  vector (reported, still copied); a null slot (reported or not, nothing copied); the vector at
  addresses 1, 3 and 7 past an 8-byte boundary (slots from the rounded-up address), and a slot
  between the end of the slots in use counted from the vector's own address and from the
  rounded one (not reported); texts inline
  and on the heap, owns bytes 0, 1 and 0xff, different allocators.
- label: counts 0 and 1 (fits); 2 (full: reported, and the third slot's word lands on the count
  itself); 5 (reported, far past); all ones (passes the check, copied one slot before the
  vector); the vector misaligned by 1 and 6; kind bytes 0 and 0xff.
The narrow (0x02b27840) and wide (0x02a279a0) string assigns and the fatal report are scripted
stubs; each assign is compared with its arguments and the target string as it stands then (begun
empty on the source's allocator). All entries and text are invented.
"""
import json
import os
import struct
import sys

ADDRESSES = {'text_pair': '0x01ed7950', 'label': '0x01f2c200'}
FATAL_ERROR, NARROW_ASSIGN, WIDE_ASSIGN = '0x024b55b0', '0x02b27840', '0x02a279a0'
STRING_SIZE, PAIR_SIZE, LABEL_SIZE = 0x38, 0x70, 0x48
PAIR_COUNT, LABEL_COUNT = 0x708, 0x98
ALLOCATOR = 0x00007a0000002000   # only copied, never called


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff)


def string_bytes(text, owns, allocator, wide, heap=False):
    data = bytearray(b'\x71' * STRING_SIZE)
    data[0:8] = u64(0x5252525252525252)
    encoded = (text.encode('utf-16-le') + b'\0\0') if wide else (text.encode() + b'\0')
    inline = 16
    if not heap:
        data[8:24] = encoded.ljust(inline, b'\0')
    data[0x18:0x20] = u64(len(text))
    data[0x20:0x28] = u64(max(len(text), 0x20) if heap else (7 if wide else 15))
    data[0x28:0x30] = u64(allocator)
    data[0x30] = owns
    return bytes(data), encoded


def stub(address):
    return {'address': address, 'ret': 0, 'argc': 4, 'argmem': [{'arg': 0, 'size': STRING_SIZE}]}


def case(variant, cid, args, buffers, memory, stubs):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': args,
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': []}


def add_heaps(buffers, memory, name, heaps):
    for n, (at, encoded) in enumerate(heaps):
        buffers[f'{name}_heap{n}'] = {'size': 0x40, 'bytes': encoded.hex()}
        memory.append({'addr': f'buf:{name}+{at}', 'pointer': f'{name}_heap{n}'})


def text_pair_case(cid, slot_index, count, shift=0, null_slot=False, texts=('north', 'gate'), owns=(1, 0),
                   heap=(False, False), slot_offset=None):
    vector = bytearray(b'\x99' * (PAIR_COUNT + 8 + 16))
    vector[shift + PAIR_COUNT:shift + PAIR_COUNT + 8] = u64(count)
    source = bytearray()
    heaps = []
    for k in range(2):
        s, encoded = string_bytes(texts[k], owns[k], ALLOCATOR + 0x100 * k, False, heap[k])
        if heap[k]:
            heaps.append((k * STRING_SIZE + 8, encoded))
        source += s
    buffers = {'vector': {'size': len(vector), 'bytes': vector.hex()},
               'source': {'size': PAIR_SIZE, 'bytes': source.hex()}}
    memory = []
    add_heaps(buffers, memory, 'source', heaps)
    aligned = (shift + 7) & ~7
    if slot_offset is None:
        slot_offset = aligned + PAIR_SIZE * slot_index
    slot = 0 if null_slot else f'buf:vector+{slot_offset}'
    stubs = []
    # A null slot is below every slot's end, so it is reported only when the vector's end is 0,
    # which no buffer is; a real slot is reported at or past the end of the slots in use, which
    # start at the vector's address rounded up to 8.
    if not null_slot and slot_offset >= aligned + PAIR_SIZE * count:
        stubs.append({'address': FATAL_ERROR, 'ret': 0, 'argc': 3})
    if not null_slot:
        stubs += [stub(NARROW_ASSIGN), stub(NARROW_ASSIGN)]
    return case('text_pair', cid, {'rdi': f'buf:vector+{shift}', 'rsi': slot, 'rdx': 'buf:source'},
                buffers, memory, stubs)


def text_pair_cases():
    return [text_pair_case('first_slot', 0, 1), text_pair_case('second_slot', 1, 2),
            text_pair_case('last_in_use', 15, 16), text_pair_case('slot_at_count', 2, 2),
            text_pair_case('slot_past_count', 5, 2), text_pair_case('empty_vector', 0, 0),
            text_pair_case('null_slot', 0, 3, null_slot=True), text_pair_case('null_slot_empty', 0, 0, null_slot=True),
            text_pair_case('misaligned_1', 1, 3, shift=1), text_pair_case('misaligned_3_past', 2, 2, shift=3),
            text_pair_case('misaligned_7', 0, 1, shift=7),
            text_pair_case('misaligned_3_before_rounded_end', 0, 2, shift=3, slot_offset=0xe5),
            text_pair_case('heap_texts', 0, 1, texts=('a long northern gate name', 'and its long caption'),
                           heap=(True, True)),
            text_pair_case('first_heap_second_inline', 1, 2, texts=('sixteen chars!!!', 'x'), heap=(True, False)),
            text_pair_case('owns_ff', 0, 1, owns=(0xff, 1)), text_pair_case('empty_texts', 0, 1, texts=('', ''))]


def label_case(cid, count, shift=0, kind=3, word=0x1234567887654321, owns=1, heap=False, text='Bolt'):
    before = 0x50                        # room for the slot before the vector (count all ones)
    vector = bytearray(b'\x88' * (before + 0x300))
    at = before + shift
    vector[at + LABEL_COUNT:at + LABEL_COUNT + 8] = u64(count)
    source = bytearray(b'\x33' * LABEL_SIZE)
    source[0] = kind
    source[8:16] = u64(word)
    s, encoded = string_bytes(text, owns, ALLOCATOR + 0x400, True, heap)
    source[0x10:0x10 + STRING_SIZE] = s
    buffers = {'vector': {'size': len(vector), 'bytes': vector.hex()},
               'source': {'size': LABEL_SIZE, 'bytes': source.hex()}}
    memory = []
    if heap:
        add_heaps(buffers, memory, 'source', [(0x18, encoded)])
    stubs = []
    if (count + 1) & 0xffffffffffffffff > 2:
        stubs.append({'address': FATAL_ERROR, 'ret': 0, 'argc': 3})
    stubs.append(stub(WIDE_ASSIGN))
    return case('label', cid, {'rdi': f'buf:vector+{at}', 'rsi': 'buf:source'}, buffers, memory, stubs)


def label_cases():
    return [label_case('count_0', 0), label_case('count_1', 1), label_case('count_2_full', 2),
            label_case('count_5_far', 5), label_case('count_all_ones', -1), label_case('misaligned_1', 1, shift=1),
            label_case('misaligned_6_full', 2, shift=6), label_case('kind_0', 0, kind=0),
            label_case('kind_ff_owns_0', 1, kind=0xff, owns=0), label_case('heap_text', 0, heap=True,
                                                                           text='a long label text here')]


def cases(variant):
    return text_pair_cases() if variant == 'text_pair' else label_cases()


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
