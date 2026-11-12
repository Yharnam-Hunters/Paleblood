#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the robust heap's allocations (game/engine/robust_heap_allocate.cpp), for
tools/verify.py run.

usage: robust_heap_cases.py OUT_DIR [allocate|allocate_top|aligned|aligned_top|free]

Each case lays out a heap in three buffers: the heap's state (`heap`), its 31 small-list sentinels
(`bins`) and a chain of blocks (`arena`), used and free, ending in a used block. Free blocks are
filed as the heap files them: usable sizes 0x18..0x1f8 in their list (in the order given, so the
last one listed is the list's last), larger ones in a balanced red-black tree (bit 0 of the parent
word set = black, as the engine's tree code has it), smaller ones nowhere. The tree's removal and
insertion fix-up run as the original code on both sides.
"""
import json
import os
import struct
import sys

ALLOCATE, ALLOCATE_TOP = '0x02486810', '0x02487380'
ALIGNED, ALIGNED_TOP = '0x02486cd0', '0x02487650'
FREE, FATAL_ERROR = '0x02487b50', '0x024b55b0'
LARGEST_SMALL = 0x1f8
ARENA_START = 0x10


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Layout:
    """blocks: (usable, 'used' | 'free'); every block's total size is usable + 8 (a multiple of 16)."""

    def __init__(self, blocks, available=0x100000, count=7, all_black=False):
        self.all_black = all_black
        self.memory = []
        self.offsets = []
        at = ARENA_START
        for usable, _ in blocks:
            assert (usable + 8) % 16 == 0 and usable >= 8, usable
            self.offsets.append(at)
            at += usable + 8
        end = at
        self.arena_size = end + 0x40
        self.blocks = blocks
        self.end = end
        free = [i for i, (_, kind) in enumerate(blocks) if kind == 'free']
        for i in free:
            assert i == 0 or blocks[i - 1][1] == 'used', 'free blocks are never adjacent'
        # Headers; the final used block points past the end.
        for i, (usable, kind) in enumerate(blocks):
            off = self.offsets[i]
            nxt = self.offsets[i + 1] if i + 1 < len(blocks) else end
            prev_used = i == 0 or blocks[i - 1][1] == 'used'
            flags = (1 if kind == 'used' else 0) | (2 if prev_used else 0)
            self.ptr(f'arena+{off + 8}', f'arena+{nxt + flags}')
            if not prev_used:
                self.ptr(f'arena+{off}', f'arena+{self.offsets[i - 1]}')
        self.ptr(f'arena+{end + 8}', f'arena+{end + 0x20 + 3}')
        self.ptr(f'arena+{end}', f'arena+{self.offsets[-1]}')
        # Small lists.
        bins = {}
        large = []
        for i in free:
            usable = blocks[i][0]
            if 0x18 <= usable <= LARGEST_SMALL:
                bins.setdefault((usable - 9) // 16, []).append(self.offsets[i] + 16)
            elif usable > LARGEST_SMALL:
                large.append((usable, self.offsets[i] + 16))
        for k in range(31):
            nodes = bins.get(k, [])
            chain = [('bins', 16 * k)] + [('arena', n) for n in nodes]
            for j, (name, off) in enumerate(chain):
                after, before = chain[(j + 1) % len(chain)], chain[j - 1]
                self.ptr(f'buf:{name}+{off}', f'{after[0]}+{after[1]}')
                self.ptr(f'buf:{name}+{off + 8}', f'{before[0]}+{before[1]}')
        # Large tree.
        large.sort()
        root = self.tree(large, None, 0, self.tree_depth(len(large)))
        self.memory += [{'addr': 'buf:heap+8', 'bytes': u64(available)},
                        {'addr': 'buf:heap+16', 'bytes': u64(count)},
                        {'addr': 'buf:heap+24', 'pointer': 'bins'}]
        if root is not None:
            self.ptr('buf:heap+32', f'arena+{root}')

    @staticmethod
    def tree_depth(n):
        d = 0
        while (1 << d) - 1 < n:
            d += 1
        return d

    def tree(self, nodes, parent, depth, total_depth):
        if not nodes:
            return None
        mid = (len(nodes) - 1) // 2
        size, node = nodes[mid]
        perfect = len(self.blocks) and (1 << total_depth) - 1 == sum(1 for u, k in self.blocks if k == 'free' and u > LARGEST_SMALL)
        red = depth == total_depth - 1 and not perfect and depth > 0 and not self.all_black
        left = self.tree(nodes[:mid], node, depth + 1, total_depth)
        right = self.tree(nodes[mid + 1:], node, depth + 1, total_depth)
        for field, child in ((0, left), (8, right)):
            if child is None:
                self.memory.append({'addr': f'buf:arena+{node + field}', 'bytes': u64(0)})
            else:
                self.ptr(f'buf:arena+{node + field}', f'arena+{child}')
        colour = 0 if red else 1
        if parent is None:
            self.memory.append({'addr': f'buf:arena+{node + 16}', 'bytes': u64(colour)})
        else:
            self.ptr(f'buf:arena+{node + 16}', f'arena+{parent + colour}')
        self.memory.append({'addr': f'buf:arena+{node + 24}', 'bytes': u64(size)})
        return node

    def ptr(self, where, target):
        where = where if where.startswith('buf:') else f'buf:{where}'
        self.memory.append({'addr': where, 'pointer': target})


def case(address, cid, request, blocks, available=0x100000, count=7, all_black=False, alignment=None,
         heap_range=None, stubs=()):
    layout = Layout(blocks, available, count, all_black)
    args = {'rdi': 'buf:heap', 'rsi': request}
    if heap_range is not None:
        low, high = heap_range
        layout.memory += [{'addr': 'buf:heap+40', 'pointer': f'arena+{low}'},
                          {'addr': 'buf:heap+48', 'pointer': f'arena+{layout.end if high is None else high}'}]
    if alignment is not None:
        args['rdx'] = alignment
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i64',
            'args': args,
            'buffers': {'heap': {'size': 0x38}, 'bins': {'size': 0x1f0}, 'arena': {'size': layout.arena_size}},
            'memory': layout.memory, 'stubs': list(stubs), 'imports': []}


U, F = 'used', 'free'


def allocate_cases():
    a = ALLOCATE
    return [
        case(a, 'small_own_list', 0x20, [(0x28, F), (0x18, U), (0x28, F), (0x38, U)]),
        case(a, 'small_own_list_single', 0x18, [(0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_minimum_request', 0, [(0x18, F), (0x18, U)]),
        case(a, 'small_request_0x17', 0x17, [(0x18, F), (0x18, U)]),
        case(a, 'small_larger_list_split', 0x28, [(0x88, F), (0x18, U)]),
        case(a, 'small_larger_list_fragment', 0x18, [(0x28, F), (0x18, U)]),
        case(a, 'small_larger_list_last_bin', 0x18, [(0x1f8, F), (0x18, U), (0x1e8, F), (0x18, U)]),
        case(a, 'small_largest_request', LARGEST_SMALL, [(0x1f8, F), (0x18, U)]),
        case(a, 'small_from_tree_exact', 0x1f8, [(0x18, U), (0x208, F), (0x18, U)]),  # usable 0x1f8 != 0x208
        case(a, 'small_from_tree_split_to_list', 0x100, [(0x208, F), (0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'small_from_tree_in_place_leaf', 0x18,
             [(0x408, F), (0x18, U), (0x508, F), (0x18, U), (0x608, F), (0x18, U)]),
        case(a, 'small_from_tree_in_place_root', 0x18, [(0x408, F), (0x18, U), (0x508, F), (0x18, U)]),
        # Not a valid red-black colouring (a black right child under the smallest node): shows that
        # the moved node's child keeps its own colour bit.
        case(a, 'small_from_tree_in_place_black_child', 0x18, [(0x408, F), (0x18, U), (0x508, F), (0x18, U)],
             all_black=True),
        case(a, 'small_from_tree_in_place_alone', 0x30, [(0x18, U), (0x808, F), (0x18, U)]),
        case(a, 'small_from_tree_margin_edge', 0x18, [(0x218, F), (0x18, U)]),
        case(a, 'small_from_tree_margin_past', 0x18, [(0x228, F), (0x18, U)]),
        case(a, 'small_nothing_free', 0x40, [(0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_empty_heap', 0x40, [(0x18, U)]),
        case(a, 'not_enough_available', 0x40, [(0x88, F), (0x18, U)], available=0x3f),
        case(a, 'available_exactly_request', 0x40, [(0x88, F), (0x18, U)], available=0x40),
        case(a, 'large_exact_middle', 0x300, [(0x208, F), (0x18, U), (0x308, F), (0x18, U), (0x408, F), (0x18, U)]),
        case(a, 'large_exact_root_alone', 0x308, [(0x308, F), (0x18, U)]),
        case(a, 'large_right_turn_rest_to_tree', 0x380,
             [(0x208, F), (0x18, U), (0x308, F), (0x18, U), (0x908, F), (0x18, U)]),
        case(a, 'large_right_turn_rest_to_list', 0x380,
             [(0x208, F), (0x18, U), (0x308, F), (0x18, U), (0x4a8, F), (0x18, U)]),
        case(a, 'large_right_turn_fragment', 0x2f8,
             [(0x208, F), (0x18, U), (0x218, F), (0x18, U), (0x318, F), (0x18, U)]),
        case(a, 'large_left_only_in_place', 0x200, [(0x808, F), (0x18, U), (0x908, F), (0x18, U), (0xa08, F), (0x18, U)]),
        case(a, 'large_left_only_split', 0x200, [(0x308, F), (0x18, U), (0x908, F), (0x18, U), (0xa08, F), (0x18, U)]),
        case(a, 'large_none_fits', 0x900, [(0x208, F), (0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'large_empty_tree', 0x200, [(0x88, F), (0x18, U)]),
        case(a, 'large_request_0x1f9', 0x1f9, [(0x208, F), (0x18, U)]),
        case(a, 'large_duplicates', 0x300, [(0x308, F), (0x18, U), (0x308, F), (0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'large_seven_nodes', 0x500,
             [x for s in (0x208, 0x308, 0x408, 0x508, 0x608, 0x708, 0x808) for x in ((s, F), (0x18, U))]),
        case(a, 'large_eight_nodes_red_leaf', 0x4f0,
             [x for s in (0x208, 0x308, 0x408, 0x508, 0x608, 0x708, 0x808, 0x908) for x in ((s, F), (0x18, U))]),
        case(a, 'free_block_after_free_fragment_mix', 0x40,
             [(0x8, F), (0x18, U), (0x58, F), (0x18, U), (0x48, F), (0x18, U)]),
    ]


def allocate_top_cases():
    a = ALLOCATE_TOP
    return [
        case(a, 'small_highest_in_lists', 0x20, [(0x28, F), (0x18, U), (0x48, F), (0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_highest_in_tree', 0x20, [(0x28, F), (0x18, U), (0x308, F), (0x18, U), (0x208, F), (0x18, U)]),
        case(a, 'small_ignores_smaller_lists', 0x40, [(0x18, U), (0x88, F), (0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_exact_whole_block', 0x28, [(0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_front_fragment', 0x18, [(0x28, F), (0x18, U)]),
        case(a, 'small_front_to_list', 0x18, [(0x88, F), (0x18, U)]),
        case(a, 'small_front_to_tree', 0x18, [(0x18, U), (0x408, F), (0x18, U)]),
        case(a, 'small_tree_walk_seven', 0x40,
             [x for s in (0x808, 0x208, 0x608, 0x308, 0x708, 0x408, 0x508) for x in ((s, F), (0x18, U))]),
        case(a, 'small_tree_walk_eight', 0x40,
             [x for s in (0x908, 0x208, 0x608, 0x308, 0x708, 0x408, 0x508, 0x808) for x in ((s, F), (0x18, U))]),
        case(a, 'small_nothing_fits', 0x40, [(0x18, U), (0x28, F), (0x18, U)]),
        case(a, 'small_from_largest_list', 0x40, [(0x18, U), (0x1f8, F), (0x18, U)]),
        case(a, 'small_minimum_request', 0, [(0x18, F), (0x18, U), (0x18, F), (0x18, U)]),
        case(a, 'not_enough_available', 0x40, [(0x88, F), (0x18, U)], available=0x10),
        case(a, 'large_lower_bound_then_successors', 0x300,
             [x for s in (0x808, 0x208, 0x308, 0x608, 0x408, 0x708, 0x508) for x in ((s, F), (0x18, U))]),
        case(a, 'large_highest_is_lower_bound', 0x300,
             [(0x208, F), (0x18, U), (0x708, F), (0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'large_exact_whole_block', 0x308, [(0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'large_front_to_list', 0x300, [(0x388, F), (0x18, U)]),
        case(a, 'large_front_fragment', 0x300, [(0x318, F), (0x18, U)]),
        case(a, 'large_front_to_tree', 0x300, [(0x18, U), (0x808, F), (0x18, U), (0x408, F), (0x18, U)]),
        case(a, 'large_none_fits', 0x900, [(0x208, F), (0x18, U), (0x308, F), (0x18, U)]),
        case(a, 'large_ignores_lists', 0x200, [(0x1f8, F), (0x18, U), (0x208, F), (0x18, U)]),
        case(a, 'large_empty_tree', 0x200, [(0x88, F), (0x18, U)]),
    ]


# The arena buffer is page-aligned and its first block starts at 0x10 (payload 0x20), so the
# layouts below place payloads at known alignments.
def aligned_cases():
    a = ALIGNED
    return [
        case(a, 'alignment_not_power_of_two', 0x40, [(0x88, F), (0x18, U)], alignment=0x30),
        case(a, 'alignment_zero_plain', 0x40, [(0x88, F), (0x18, U)], alignment=0),
        case(a, 'alignment_16_plain', 0x40, [(0x88, F), (0x18, U)], alignment=0x10),
        case(a, 'not_enough_available', 0x40, [(0x88, F), (0x18, U)], available=0x20, alignment=0x40),
        case(a, 'own_list_aligned_block', 0x20, [(0x28, F), (0x18, U), (0x28, F), (0x18, U)], alignment=0x20),
        case(a, 'own_list_none_aligned_larger_inside', 0x18, [(0x28, U), (0x18, F), (0x18, U), (0x88, F), (0x18, U)],
             alignment=0x40),
        case(a, 'larger_list_aligned_start', 0x18, [(0x18, U), (0x88, F), (0x18, U)], alignment=0x40),
        case(a, 'larger_list_inside_front_and_rest', 0x18, [(0x88, F), (0x18, U)], alignment=0x40),
        case(a, 'larger_list_inside_reaches_end', 0x18, [(0x38, F), (0x18, U)], alignment=0x40),
        case(a, 'larger_list_too_small_then_next', 0x18, [(0x18, U), (0x28, F), (0x18, U), (0x88, F), (0x18, U)],
             alignment=0x80),
        # The payload is 16 bytes short of an aligned address: the allocation still goes a full
        # alignment further, leaving room for a usable free block in front.
        case(a, 'inside_skips_close_boundary', 0x18, [(0x8, U), (0x88, F), (0x18, U)], alignment=0x40),
        case(a, 'last_list_inside', 0x18, [(0x1f8, F), (0x18, U)], alignment=0x40),
        case(a, 'tree_smallest_inside', 0x100, [(0x208, F), (0x18, U)], alignment=0x100),
        case(a, 'tree_smallest_aligned_start_split', 0x100, [(0xd8, U), (0x408, F), (0x18, U)], alignment=0x100),
        case(a, 'tree_smallest_aligned_start_whole', 0x1f8, [(0xd8, U), (0x208, F), (0x18, U)], alignment=0x100),
        case(a, 'tree_walks_to_successor', 0x100, [(0x208, F), (0x18, U), (0x408, F), (0x18, U), (0x308, F), (0x18, U)],
             alignment=0x200),
        case(a, 'tree_nothing_fits', 0x100, [(0x208, F), (0x18, U), (0x228, F), (0x18, U)], alignment=0x400),
        case(a, 'small_nothing_free', 0x40, [(0x18, U), (0x28, F), (0x18, U)], alignment=0x40),
        case(a, 'large_inside_front_to_tree', 0x300, [(0x808, F), (0x18, U)], alignment=0x400),
        case(a, 'large_inside_rest_to_tree', 0x300, [(0x1008, F), (0x18, U)], alignment=0x400),
        case(a, 'large_inside_reaches_end', 0x300, [(0x6f8, F), (0x18, U)], alignment=0x400),
        case(a, 'large_lower_bound_skips_small', 0x300, [(0x208, F), (0x18, U), (0x308, F), (0x18, U), (0x808, F), (0x18, U)],
             alignment=0x400),
        case(a, 'large_aligned_start_exact', 0x308, [(0x3d8, U), (0x308, F), (0x18, U)], alignment=0x400),
        case(a, 'large_none', 0x900, [(0x208, F), (0x18, U)], alignment=0x40),
        case(a, 'large_tree_seven_nodes', 0x2f0,
             [x for s in (0x208, 0x308, 0x408, 0x508, 0x608, 0x708, 0x808) for x in ((s, F), (0x18, U))], alignment=0x800),
    ]


def aligned_top_cases():
    a = ALIGNED_TOP
    return [
        case(a, 'alignment_not_power_of_two', 0x40, [(0x88, F), (0x18, U)], alignment=0x60),
        case(a, 'alignment_16_plain_top', 0x40, [(0x88, F), (0x18, U)], alignment=0x10),
        case(a, 'not_enough_available', 0x40, [(0x88, F), (0x18, U)], available=0x8, alignment=0x40),
        case(a, 'own_list_aligned_whole', 0x18, [(0x18, U), (0x18, F), (0x18, U)], alignment=0x20),
        case(a, 'own_list_unaligned_none', 0x18, [(0x18, F), (0x18, U)], alignment=0x40),
        case(a, 'start_split', 0x18, [(0x18, U), (0x48, F), (0x18, U)], alignment=0x40),
        case(a, 'inside_reaches_end', 0x18, [(0x88, F), (0x18, U)], alignment=0x20),
        case(a, 'inside_with_rest', 0x18, [(0x98, F), (0x18, U)], alignment=0x40),
        case(a, 'highest_qualifying_not_highest', 0x18, [(0x88, F), (0x18, U), (0x28, F), (0x18, U)], alignment=0x40),
        case(a, 'tree_front_to_tree', 0x100, [(0x808, F), (0x18, U)], alignment=0x100),
        case(a, 'tree_front_and_rest_to_tree', 0x100, [(0x18, U), (0x808, F), (0x18, U)], alignment=0x400),
        case(a, 'tree_payload_below_own', 0x100, [(0x18, U), (0x808, F), (0x18, U)], alignment=0x800),
        case(a, 'tree_highest_of_seven', 0x40,
             [x for s in (0x808, 0x208, 0x608, 0x308, 0x708, 0x408, 0x508) for x in ((s, F), (0x18, U))], alignment=0x100),
        case(a, 'large_lower_bound_successors', 0x300,
             [x for s in (0x808, 0x208, 0x308, 0x608, 0x408, 0x708, 0x508) for x in ((s, F), (0x18, U))], alignment=0x200),
        case(a, 'large_none', 0x900, [(0x208, F), (0x18, U)], alignment=0x40),
        case(a, 'large_start_whole', 0x308, [(0x3d8, U), (0x308, F), (0x18, U)], alignment=0x400),
        case(a, 'small_nothing_fits', 0x40, [(0x18, U), (0x28, F), (0x18, U)], alignment=0x40),
        case(a, 'empty_heap', 0x40, [(0x18, U)], alignment=0x40),
    ]


def free_case(cid, blocks, target, heap_range=(0x20, None), error=0, **kw):
    """Frees the payload of blocks[target]; `error` = how many error reports the case expects."""
    payload = ARENA_START + sum(u + 8 for u, _ in blocks[:target]) + 16
    stubs = [{'address': FATAL_ERROR, 'argc': 4}] * error
    c = case(FREE, cid, 0, blocks, heap_range=heap_range, stubs=stubs, **kw)
    c['args']['rsi'] = f'buf:arena+{payload}'
    c['returns'] = 'void'
    return c


def free_cases():
    return [
        free_case('between_used', [(0x18, U), (0x48, U), (0x18, U)], 1),
        free_case('first_block', [(0x48, U), (0x18, U)], 0),
        free_case('merge_previous_small', [(0x28, F), (0x48, U), (0x18, U)], 1),
        free_case('merge_previous_tree', [(0x308, F), (0x48, U), (0x18, U), (0x208, F), (0x18, U)], 1),
        free_case('merge_previous_fragment', [(0x8, F), (0x48, U), (0x18, U)], 1),
        free_case('merge_next_small', [(0x18, U), (0x48, U), (0x38, F), (0x18, U)], 1),
        free_case('merge_next_tree', [(0x18, U), (0x48, U), (0x408, F), (0x18, U), (0x208, F), (0x18, U)], 1),
        free_case('merge_next_fragment', [(0x18, U), (0x48, U), (0x8, F), (0x18, U)], 1),
        free_case('merge_both_into_tree', [(0x98, F), (0x48, U), (0x188, F), (0x18, U)], 1),
        free_case('merge_both_small', [(0x28, F), (0x28, U), (0x28, F), (0x18, U)], 1),
        free_case('merge_both_trees', [(0x308, F), (0x48, U), (0x408, F), (0x18, U), (0x508, F), (0x18, U)], 1),
        free_case('becomes_large', [(0x18, U), (0x208, U), (0x18, U)], 1),
        free_case('largest_small_result', [(0x18, U), (0x1f8, U), (0x18, U)], 1),
        free_case('fragment_result', [(0x18, U), (0x8, U), (0x18, U)], 1),
        free_case('null_payload', [(0x48, U), (0x18, U)], 0, heap_range=None) | {'args': {'rdi': 'buf:heap', 'rsi': 0}},
        free_case('double_free_reported', [(0x18, U), (0x8, F), (0x18, U)], 1, error=1),
        free_case('below_range_reported', [(0x18, U), (0x48, U), (0x18, U)], 1, heap_range=(0x60, None), error=1),
        free_case('above_range_reported', [(0x18, U), (0x48, U), (0x18, U)], 1, heap_range=(0x20, 0x30), error=1),
        free_case('range_edges', [(0x18, U), (0x48, U), (0x18, U)], 1, heap_range=(0x40, 0x40)),
        free_case('counts_wrap', [(0x18, U), (0x48, U), (0x18, U)], 1, available=0xfffffffffffffff0, count=0),
    ]


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    which = sys.argv[2] if len(sys.argv) == 3 else 'allocate'
    cases = {'allocate': allocate_cases, 'allocate_top': allocate_top_cases, 'aligned': aligned_cases,
             'aligned_top': aligned_top_cases, 'free': free_cases}[which]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
