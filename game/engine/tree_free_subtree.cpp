// SPDX-License-Identifier: GPL-2.0-or-later
// The subtree release of the engine's trees (tree_free_subtree.h), as the game compiles it for
// each kind of set and map: 104 copies of the same code as the release of the set of 64-bit keys
// (set_free_subtree.cpp, 0x02b3bda0). The copies differ only in the function each one calls for a
// node's right subtree, which is itself. What a copy's nodes hold after their links is not known
// here, and the release does not read it, so each copy is given a node's links alone.
#include "tree_free_subtree.h"

namespace {

using Links = engine::TreeNodeLinks;
using Tree = engine::Tree<Links>;

}  // namespace

extern "C" void bb_engine_tree_free_subtree_016b7550(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_016dfc80(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_016dfce0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_016f1c10(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_017c4d20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_017c8f00(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_017d22c0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_017dd280(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_018904d0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01900d00(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01957700(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01957760(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01964c70(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01995650(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_019b7410(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01bd2650(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c6d380(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c6d3e0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c7cab0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c7cb10(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c7cb70(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01c93820(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01db3a20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01db3de0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01dcfbf0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01dcfc50(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01e20ec0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01e20f20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01e20f80(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01e20fe0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01e21040(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_01ea2860(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_020b7160(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_020c4700(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_020f01b0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_020f2e20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02112df0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_021d4ee0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_022ae740(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_022aee90(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_023261a0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0232c8d0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0232f6d0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02366060(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_023660c0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0236f1f0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0237d410(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0237dda0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_023fa1b0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02455e80(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0248fa70(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02492ad0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_024a8360(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_024d3200(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02500e20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02537970(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_025379d0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0255af40(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02622d30(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0265c090(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0265d100(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0268d060(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_026f4500(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0277bd00(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0277bd60(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0277c950(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_0277d500(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a4f1b0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a50620(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a50680(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a506e0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a73900(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02a74300(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02b30bf0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02bbb6c0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02bc9ea0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02be6b80(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02beec40(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02bef8f0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02c26b50(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02c27500(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d2c540(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d65fa0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d67d90(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d75dc0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d78fc0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d79a20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d7a4f0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d83c30(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d84600(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02d852f0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02da03d0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02daa780(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02db1dc0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02db1e20(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02db4240(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02dbb9b0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02dbd250(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02dcf0b0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02dd88f0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02ddef00(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02e5f850(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02f0ab00(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}

extern "C" void bb_engine_tree_free_subtree_02f81de0(Tree *tree, Links *node)
{
    engine::tree_free_subtree(tree, node);
}
