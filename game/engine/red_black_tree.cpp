// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the red-black tree the robust heap and the fixed allocator keep their free blocks and
// chunks in (robust_heap.h, fixed_allocator.h).
//
// A node is two child pointers (left, right) and a word holding its parent's address with the
// colour in bit 0: set for black. The root's word is just the black bit. The code never masks the
// bit off a word it knows is black: it subtracts it, and it writes the black bit onto a child it
// moves without reading the child's colour (in a valid tree that child is black).
//
// engine_tree_insert_fixup_024f8350 (0x024f8350) takes a node the caller has just linked below
// `parent` (null for an empty tree; then the caller sets the root). It clears the node's children
// and makes it red, then rebalances: while the parent is red, a red uncle makes the parent and the
// uncle black and the grandparent red, and the walk goes on from the grandparent; a black or
// missing uncle ends it with one rotation (two when the node is an inner child), the old
// grandparent becoming the red child of the new subtree top.
//
// engine_tree_remove_024f8460 (0x024f8460) unlinks a node. A node with at most one child is
// replaced by that child; otherwise its successor (the leftmost node of its right subtree) takes
// its place, parent and colour. When the position that went away was black, the tree is
// rebalanced from the child that moved up (`x`, maybe null) and its parent:
// - a red `x`, or `x` at the root, turns black and ends it;
// - a red sibling is rotated above the parent and turns black; the rest is then finished in place,
//   below it, without going round the loop again;
// - a black sibling with a red far nephew: one rotation of the parent toward `x` (after a
//   rotation of the sibling when only the near nephew is red);
// - a black sibling with black nephews turns red, and the walk goes on from the parent.
#include <cstddef>
#include <cstdint>

namespace {

constexpr int left = 0, right = 1, side_count = 2;

struct TreeNode {
    TreeNode *child[side_count];  // left, right
    uintptr_t parent_and_colour;
};
static_assert(offsetof(TreeNode, parent_and_colour) == 0x10);

constexpr uintptr_t black = 1;

int other_side(int side) { return 1 - side; }

uintptr_t address_of(const TreeNode *node) { return reinterpret_cast<uintptr_t>(node); }

TreeNode *node_at(uintptr_t word) { return reinterpret_cast<TreeNode *>(word); }

bool is_black(const TreeNode *node) { return (node->parent_and_colour & black) != 0; }

// The link above `node`: the root, or its parent's child pointer that holds it. `parent_word` is
// the node's own word, known black.
TreeNode **link_above(TreeNode **root, TreeNode *node, uintptr_t parent_word)
{
    TreeNode *parent = node_at(parent_word - black);
    if (parent == nullptr)
        return root;
    return parent->child[left] == node ? &parent->child[left] : &parent->child[right];
}

TreeNode *parent_of(uintptr_t word) { return node_at(word & ~black); }

// Gives `child` (maybe null) a new parent and keeps its colour.
void set_parent(TreeNode *child, TreeNode *parent)
{
    if (child != nullptr)
        child->parent_and_colour = (child->parent_and_colour & black) | address_of(parent);
}

// Puts `replacement` where `node` hangs below `parent` (or at the root).
void replace_child(TreeNode **root, TreeNode *parent, TreeNode *node, TreeNode *replacement)
{
    if (parent == nullptr)
        *root = replacement;
    else if (parent->child[left] == node)
        parent->child[left] = replacement;
    else
        parent->child[right] = replacement;
}

// Rotates `top` (the parent's child on the sibling's side) above `parent`, toward `x_side`: `top`
// takes the parent's place and word, and the parent becomes its black child.
void rotate_parent_down(TreeNode **root, TreeNode *parent, uintptr_t parent_word, TreeNode *top, int x_side)
{
    const int sibling_side = other_side(x_side);
    TreeNode *moved = top->child[x_side];
    parent->child[sibling_side] = moved;
    set_parent(moved, parent);
    top->child[x_side] = parent;
    TreeNode *grandparent = parent_of(parent_word);
    if (grandparent == nullptr) {
        top->parent_and_colour = parent_word | black;
        *root = top;
    } else {
        TreeNode **link = grandparent->child[left] == parent ? &grandparent->child[left] : &grandparent->child[right];
        top->parent_and_colour = parent_word;
        *link = top;
    }
    parent->parent_and_colour = address_of(top) | black;
}

// The first case of the rebalance: the sibling is red. It is rotated above the parent and takes the
// parent's word with black; the case is then finished below it, in place.
void rebalance_red_sibling(TreeNode **root, TreeNode *parent, uintptr_t parent_word, TreeNode *sibling, int x_side)
{
    const int sibling_side = other_side(x_side);
    TreeNode *new_sibling = sibling->child[x_side];
    parent->child[sibling_side] = new_sibling;
    sibling->child[x_side] = parent;
    sibling->parent_and_colour = parent_word | black;
    replace_child(root, parent_of(parent_word), parent, sibling);

    TreeNode *top;
    TreeNode *far = new_sibling->child[sibling_side];
    if (far != nullptr && !is_black(far)) {
        far->parent_and_colour = address_of(new_sibling) | black;
        top = new_sibling;
    } else {
        TreeNode *near = new_sibling->child[x_side];
        if (near == nullptr || is_black(near)) {
            // Both nephews black: the parent turns black and its new sibling red.
            parent->parent_and_colour = address_of(sibling) | black;
            new_sibling->parent_and_colour = address_of(parent);
            return;
        }
        TreeNode *inner = near->child[sibling_side];
        new_sibling->child[x_side] = inner;
        if (inner != nullptr)
            inner->parent_and_colour = address_of(new_sibling) | black;
        near->child[sibling_side] = new_sibling;
        new_sibling->parent_and_colour = address_of(near) | black;
        top = near;
    }
    // Rotate the parent down below `top`, which hangs red below the old sibling.
    TreeNode *moved = top->child[x_side];
    parent->child[sibling_side] = moved;
    set_parent(moved, parent);
    top->child[x_side] = parent;
    top->parent_and_colour = address_of(sibling);
    sibling->child[x_side] = top;
    parent->parent_and_colour = address_of(top) | black;
}

// Restores the black counts after a black position went away; `x` (maybe null) moved up into it
// below `parent`.
void rebalance_after_remove(TreeNode **root, TreeNode *x, TreeNode *parent)
{
    for (;;) {
        if (x != nullptr) {
            if (!is_black(x) || parent == nullptr) {
                x->parent_and_colour = address_of(parent) | black;
                return;
            }
        } else if (parent == nullptr) {
            return;
        }
        const uintptr_t parent_word = parent->parent_and_colour;
        const int x_side = parent->child[left] == x ? left : right;
        const int sibling_side = other_side(x_side);
        TreeNode *sibling = parent->child[sibling_side];
        if (!is_black(sibling)) {
            rebalance_red_sibling(root, parent, parent_word, sibling, x_side);
            return;
        }
        TreeNode *far = sibling->child[sibling_side];
        if (far != nullptr && !is_black(far)) {
            far->parent_and_colour = address_of(sibling) | black;
            rotate_parent_down(root, parent, parent_word, sibling, x_side);
            return;
        }
        TreeNode *near = sibling->child[x_side];
        if (near == nullptr || is_black(near)) {
            // Both nephews black: the sibling turns red and the walk goes up.
            sibling->parent_and_colour = address_of(parent);
            x = parent;
            parent = parent_of(parent_word);
            continue;
        }
        // Only the near nephew is red: rotate it above the sibling first.
        TreeNode *inner = near->child[sibling_side];
        sibling->child[x_side] = inner;
        if (inner != nullptr)
            inner->parent_and_colour = address_of(sibling) | black;
        near->child[sibling_side] = sibling;
        sibling->parent_and_colour = address_of(near) | black;
        rotate_parent_down(root, parent, parent_word, near, x_side);
        return;
    }
}

}  // namespace

extern "C" void bb_engine_tree_insert_fixup_024f8350(TreeNode **root, TreeNode *node, TreeNode *parent)
{
    node->child[right] = nullptr;
    node->child[left] = nullptr;
    if (parent == nullptr) {
        node->parent_and_colour = black;
        return;
    }
    node->parent_and_colour = address_of(parent);

    uintptr_t parent_word = parent->parent_and_colour;
    while ((parent_word & black) == 0) {
        TreeNode *grandparent = node_at(parent_word);
        const int uncle_side = grandparent->child[left] == parent ? right : left;
        const int parent_side = other_side(uncle_side);
        TreeNode *uncle = grandparent->child[uncle_side];

        if (uncle == nullptr || is_black(uncle)) {
            // Rotate the inner child above its parent first, then the top above the grandparent.
            TreeNode *top = parent;
            if (parent->child[uncle_side] == node) {
                TreeNode *inner = node->child[parent_side];
                parent->child[uncle_side] = inner;
                if (inner != nullptr)
                    inner->parent_and_colour = address_of(parent) | black;
                parent->parent_and_colour = address_of(node);
                node->child[parent_side] = parent;
                top = node;
            }
            const uintptr_t grandparent_word = grandparent->parent_and_colour;
            *link_above(root, grandparent, grandparent_word) = top;
            top->parent_and_colour = grandparent_word;
            TreeNode *moved = top->child[uncle_side];
            grandparent->child[parent_side] = moved;
            if (moved != nullptr)
                moved->parent_and_colour = parent_word | black;
            grandparent->parent_and_colour = address_of(top);
            top->child[uncle_side] = grandparent;
            return;
        }

        // A red uncle: push the grandparent's black down to both its children.
        const uintptr_t grandparent_word = grandparent->parent_and_colour;
        uncle->parent_and_colour = parent_word | black;
        parent->parent_and_colour = parent_word | black;
        if (grandparent_word == black) {
            grandparent->parent_and_colour = black;  // the root stays black
            return;
        }
        grandparent->parent_and_colour = grandparent_word - black;
        node = grandparent;
        parent = node_at(grandparent_word - black);
        parent_word = parent->parent_and_colour;
    }
}

extern "C" void bb_engine_tree_remove_024f8460(TreeNode **root, TreeNode *node)
{
    const uintptr_t node_word = node->parent_and_colour;
    TreeNode *node_parent = parent_of(node_word);
    TreeNode *x;
    TreeNode *x_parent;
    uintptr_t removed_word;  // the word of the position that goes away (its colour counts)

    if (node->child[left] == nullptr || node->child[right] == nullptr) {
        x = node->child[left] != nullptr ? node->child[left] : node->child[right];
        set_parent(x, node_parent);
        replace_child(root, node_parent, node, x);
        removed_word = node_word;
        x_parent = node_parent;
    } else {
        TreeNode *successor = node->child[right];
        while (successor->child[left] != nullptr)
            successor = successor->child[left];
        replace_child(root, node_parent, node, successor);
        x = successor->child[right];
        removed_word = successor->parent_and_colour;
        TreeNode *successor_parent = parent_of(removed_word);
        if (successor_parent == node) {
            x_parent = successor;
        } else {
            set_parent(x, successor_parent);
            successor_parent->child[left] = x;
            successor->child[right] = node->child[right];
            set_parent(node->child[right], successor);
            x_parent = successor_parent;
        }
        successor->parent_and_colour = node->parent_and_colour;
        successor->child[left] = node->child[left];
        set_parent(node->child[left], successor);
    }

    if ((removed_word & black) != 0)
        rebalance_after_remove(root, x, x_parent);
}
