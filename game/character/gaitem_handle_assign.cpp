// SPDX-License-Identifier: GPL-2.0-or-later
// Character: assigning item handles (the game's SprjGaitem). gaitem_handle.cpp describes a
// handle and has its copy, release and move assignment.
#include <cstdint>

#include "gaitem_handle.h"

namespace {

using character::GaitemManager;

// Copy-assigns `source` to `target`. The item `source` names gets its reference first, so an
// item both handles name is never freed in between; then the handle `target` held is released
// and `target` takes `source`'s handle. Nothing happens when both hold the same handle.
//
// Each handle is read from memory again after the step before it, as the game does: a handle
// may be stored where that step writes (a count in the manager's slot table, or anywhere the
// item free reaches).
uint32_t *copy_assign(uint32_t *target, const uint32_t *source)
{
    if (*target == *source)
        return target;
    if (*source != 0) {
        GaitemManager *items = character::require_gaitem_manager();
        character::gaitem_add_reference(items, *source);
    }
    if (*target != 0) {
        GaitemManager *items = character::require_gaitem_manager();
        character::gaitem_release_reference(items, *target);
    }
    *target = *source;
    return target;
}

}  // namespace

// The game has two copies of the copy assignment.
extern "C" uint32_t *bb_character_gaitem_handle_copy_assign_01e8bd80(uint32_t *target, const uint32_t *source)
{
    return copy_assign(target, source);
}

extern "C" uint32_t *bb_character_gaitem_handle_copy_assign_01e8bed0(uint32_t *target, const uint32_t *source)
{
    return copy_assign(target, source);
}

// Assigns the handle value `handle` to `target`: the copy assignment with the new handle passed
// by value, so only `target` is read again.
extern "C" uint32_t *bb_character_gaitem_handle_assign_value_01e8c020(uint32_t *target, uint32_t handle)
{
    if (*target == handle)
        return target;
    if (handle != 0) {
        GaitemManager *items = character::require_gaitem_manager();
        character::gaitem_add_reference(items, handle);
    }
    if (*target != 0) {
        GaitemManager *items = character::require_gaitem_manager();
        character::gaitem_release_reference(items, *target);
    }
    *target = handle;
    return target;
}
