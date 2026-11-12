// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the fixed-size allocator's chunk release (Dantelion2 DLFixedAllocator; layout in
// fixed_allocator.h).
//
// engine_fixed_allocator_release_chunks_02484ae0 (0x02484ae0) takes an allocator and a node of
// its chunk tree and gives the chunks of that subtree back to the backing allocator: the left
// subtree, then the right, then the node's own chunk (whose memory ends at its descriptor). The
// tree itself is left as it was; the callers forget it afterwards.
#include "fixed_allocator.h"

using namespace engine::fixed_allocator;

extern "C" void bb_engine_fixed_allocator_release_chunks_02484ae0(Allocator *allocator, TreeNode *node)
{
    if (node->left)
        bb_engine_fixed_allocator_release_chunks_02484ae0(allocator, node->left);
    if (node->right)
        bb_engine_fixed_allocator_release_chunks_02484ae0(allocator, node->right);
    BackingAllocator *backing = allocator->backing;
    backing->vtable->free(backing, chunk_memory(allocator, node));
}
