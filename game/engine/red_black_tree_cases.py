#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's red-black tree (game/engine/red_black_tree.cpp), for tools/verify.py run.

usage: red_black_tree_cases.py OUT_DIR insert_fixup|remove

The trees are built by a model of the same tree (keys inserted in a fixed pseudo-random order),
laid out in `nodes` (0x20 bytes a node: left, right, the parent word with black in bit 0, and the
key, which the code never reads), with the root pointer in `root`. An insert case links a new
leaf below the parent a search for its key finds, as the callers do, and hands that state to the
fix-up; the new node's own fields start as garbage, which the fix-up overwrites. Shapes are picked
so that every path runs: an empty tree, a black parent, red uncles (also up to the root), and black
or missing uncles with the new node an outer or an inner child, on both sides. A remove case
takes a node out of such a tree: every node of a few small trees, and random ones of bigger trees.
"""
import json
import os
import random
import struct
import sys

INSERT_FIXUP, REMOVE = '0x024f8350', '0x024f8460'
NODE = 0x20


class Tree:
    def __init__(self):
        self.left, self.right, self.parent, self.black, self.key = {}, {}, {}, {}, {}
        self.root = None
        self.count = 0
        self.detached = set()  # deleted nodes: their slots stay in `nodes`, unlinked

    def new(self, key):
        n = self.count
        self.count += 1
        self.left[n] = self.right[n] = self.parent[n] = None
        self.black[n] = False
        self.key[n] = key
        return n

    def link(self, key):
        """A plain search-tree insert: the new node and its parent (None for an empty tree)."""
        n = self.new(key)
        p, at = None, self.root
        while at is not None:
            p = at
            at = self.left[at] if key < self.key[at] else self.right[at]
        self.parent[n] = p
        if p is None:
            self.root = n
        elif key < self.key[p]:
            self.left[p] = n
        else:
            self.right[p] = n
        return n, p

    def rotate(self, x, side):
        """Rotate x down to `side` ('left' rotation when side == 'left')."""
        a, b = (self.left, self.right) if side == 'left' else (self.right, self.left)
        y = b[x]
        b[x] = a[y]
        if a[y] is not None:
            self.parent[a[y]] = x
        self.parent[y] = self.parent[x]
        p = self.parent[x]
        if p is None:
            self.root = y
        elif self.left[p] == x:
            self.left[p] = y
        else:
            self.right[p] = y
        a[y] = x
        self.parent[x] = y

    def insert(self, key):
        z, _ = self.link(key)
        while self.parent[z] is not None and not self.black[self.parent[z]]:
            p = self.parent[z]
            g = self.parent[p]
            if p == self.left[g]:
                u = self.right[g]
                if u is not None and not self.black[u]:
                    self.black[p] = self.black[u] = True
                    self.black[g] = False
                    z = g
                    continue
                if z == self.right[p]:
                    z = p
                    self.rotate(z, 'left')
                    p = self.parent[z]
                self.black[p] = True
                self.black[g] = False
                self.rotate(g, 'right')
            else:
                u = self.left[g]
                if u is not None and not self.black[u]:
                    self.black[p] = self.black[u] = True
                    self.black[g] = False
                    z = g
                    continue
                if z == self.left[p]:
                    z = p
                    self.rotate(z, 'right')
                    p = self.parent[z]
                self.black[p] = True
                self.black[g] = False
                self.rotate(g, 'left')
        self.black[self.root] = True

    def delete(self, key):
        """The textbook delete, to give the remove cases trees that deletions shaped."""
        z = next(n for n in self.key if self.key[n] == key and self.attached(n))
        def transplant(u, v):
            p = self.parent[u]
            if p is None:
                self.root = v
            elif self.left[p] == u:
                self.left[p] = v
            else:
                self.right[p] = v
            if v is not None:
                self.parent[v] = p
        y, y_black = z, self.black[z]
        if self.left[z] is None:
            x, xp = self.right[z], self.parent[z]
            transplant(z, self.right[z])
        elif self.right[z] is None:
            x, xp = self.left[z], self.parent[z]
            transplant(z, self.left[z])
        else:
            y = self.right[z]
            while self.left[y] is not None:
                y = self.left[y]
            y_black = self.black[y]
            x = self.right[y]
            if self.parent[y] == z:
                xp = y
            else:
                xp = self.parent[y]
                transplant(y, self.right[y])
                self.right[y] = self.right[z]
                self.parent[self.right[y]] = y
            transplant(z, y)
            self.left[y] = self.left[z]
            self.parent[self.left[y]] = y
            self.black[y] = self.black[z]
        self.detached.add(z)
        if not y_black:
            return
        black = lambda n: n is None or self.black[n]
        while x != self.root and black(x):
            if x == self.left[xp]:
                w = self.right[xp]
                if not black(w):
                    self.black[w], self.black[xp] = True, False
                    self.rotate(xp, 'left')
                    w = self.right[xp]
                if black(self.left[w]) and black(self.right[w]):
                    self.black[w] = False
                    x, xp = xp, self.parent[xp]
                    continue
                if black(self.right[w]):
                    self.black[self.left[w]], self.black[w] = True, False
                    self.rotate(w, 'right')
                    w = self.right[xp]
                self.black[w], self.black[xp] = self.black[xp], True
                self.black[self.right[w]] = True
                self.rotate(xp, 'left')
            else:
                w = self.left[xp]
                if not black(w):
                    self.black[w], self.black[xp] = True, False
                    self.rotate(xp, 'right')
                    w = self.left[xp]
                if black(self.left[w]) and black(self.right[w]):
                    self.black[w] = False
                    x, xp = xp, self.parent[xp]
                    continue
                if black(self.left[w]):
                    self.black[self.right[w]], self.black[w] = True, False
                    self.rotate(w, 'left')
                    w = self.left[xp]
                self.black[w], self.black[xp] = self.black[xp], True
                self.black[self.left[w]] = True
                self.rotate(xp, 'right')
            x = self.root
            break
        if x is not None:
            self.black[x] = True

    def attached(self, n):
        return n not in self.detached

    def check(self):
        """Every path has the same black count and no red node has a red child."""
        def walk(n):
            if n is None:
                return 1
            for c in (self.left[n], self.right[n]):
                assert c is None or self.parent[c] == n
                if not self.black[n]:
                    assert c is None or self.black[c]
            a, b = walk(self.left[n]), walk(self.right[n])
            assert a == b
            return a + self.black[n]
        assert self.root is None or self.black[self.root]
        walk(self.root)


def q(v):
    return struct.pack('<Q', v).hex()


def layout(tree, fresh=None):
    """The buffers and memory entries for the tree; `fresh` keeps garbage in its own fields."""
    nodes = bytearray(b'\x00' * (tree.count * NODE))
    memory = []
    for n in range(tree.count):
        at = n * NODE
        nodes[at + 0x18:at + 0x20] = struct.pack('<Q', tree.key[n])
        if n == fresh:
            nodes[at:at + 0x18] = b'\xa5' * 0x18
            continue
        if not tree.attached(n):
            continue
        for offset, child in ((0, tree.left[n]), (8, tree.right[n])):
            if child is not None:
                memory.append({'addr': f'buf:nodes+{at + offset:#x}', 'pointer': f'nodes+{child * NODE:#x}'})
        p, colour = tree.parent[n], 1 if tree.black[n] else 0
        if p is None:
            nodes[at + 0x10:at + 0x18] = struct.pack('<Q', colour)
        else:
            memory.append({'addr': f'buf:nodes+{at + 0x10:#x}', 'pointer': f'nodes+{p * NODE + colour:#x}'})
    buffers = {'nodes': {'size': max(len(nodes), NODE), 'bytes': bytes(nodes).hex()}, 'root': {'size': 8}}
    if tree.root is not None:
        memory.append({'addr': 'buf:root+0x0', 'pointer': f'nodes+{tree.root * NODE:#x}'})
    return buffers, memory


def insert_case(cid, keys, new_key):
    tree = Tree()
    for k in keys:
        tree.insert(k)
    tree.check()
    node, parent = tree.link(new_key)
    buffers, memory = layout(tree, fresh=node)
    if parent is None:
        # The caller sets the root after the fix-up; before it the root slot is still empty.
        memory = [m for m in memory if m['addr'] != 'buf:root+0x0']
    return {'schema': 1, 'address': INSERT_FIXUP, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:root', 'rsi': f'buf:nodes+{node * NODE:#x}',
                     'rdx': 0 if parent is None else f'buf:nodes+{parent * NODE:#x}'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': []}


def insert_cases():
    out = [insert_case('empty_tree', [], 50),
           insert_case('black_parent_left', [50], 40), insert_case('black_parent_right', [50], 60),
           insert_case('red_uncle_at_root', [50, 40, 60], 30),
           insert_case('outer_left', [50, 40], 30), insert_case('outer_right', [50, 60], 70),
           insert_case('inner_left', [50, 40], 45), insert_case('inner_right', [50, 60], 55),
           insert_case('equal_key_goes_right', [50, 40], 40),
           insert_case('red_uncle_then_rotate', [50, 30, 70, 20, 40, 60, 80, 10, 25], 5),
           insert_case('red_uncle_twice', [8, 4, 12, 2, 6, 10, 14, 1, 3, 5, 7], 0)]
    rng = random.Random(0x5eed)
    for i in range(40):
        size = rng.choice([3, 5, 8, 13, 21, 34, 55])
        keys = rng.sample(range(1, 1000), size)
        out.append(insert_case(f'random_{i:02d}_{size}', keys, rng.randrange(0, 1001)))
    return out


def remove_case(cid, keys, remove_key, deleted=()):
    tree = Tree()
    for k in keys:
        tree.insert(k)
    for k in deleted:
        tree.delete(k)
        tree.check()
    tree.check()
    node = next(n for n in range(tree.count) if tree.key[n] == remove_key and tree.attached(n))
    buffers, memory = layout(tree)
    return {'schema': 1, 'address': REMOVE, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:root', 'rsi': f'buf:nodes+{node * NODE:#x}'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': []}


def remove_cases():
    out = [remove_case('only_node', [50], 50), remove_case('red_leaf', [50, 40], 40),
           remove_case('root_with_one_child', [50, 60], 50)]
    # Every node of a few small trees, so that each rebalance path runs on both sides.
    for size in (3, 4, 6, 7, 10, 15):
        keys = list(range(10, 10 * size + 1, 10))
        order = random.Random(size).sample(keys, size)
        for k in keys:
            out.append(remove_case(f'tree{size}_remove_{k}', order, k))
    rng = random.Random(0xdead)
    for i in range(40):
        size = rng.choice([8, 13, 21, 34, 55])
        keys = rng.sample(range(1, 1000), size)
        out.append(remove_case(f'random_{i:02d}_{size}', keys, rng.choice(keys)))
    # Trees that deletions shaped: inserts alone rarely leave a red sibling with black nephews or
    # long black chains, where the walk goes up more than once.
    rng = random.Random(0xde1e7e)
    for i in range(60):
        size = rng.choice([16, 24, 40, 64])
        keys = rng.sample(range(1, 1000), size)
        deleted = rng.sample(keys, size // 2)
        rest = [k for k in keys if k not in deleted]
        out.append(remove_case(f'shaped_{i:02d}_{size}', keys, rng.choice(rest), deleted))
    return out


VARIANTS = {'insert_fixup': insert_cases, 'remove': remove_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in VARIANTS:
        print(__doc__, file=sys.stderr)
        return 2
    cases = VARIANTS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
