// SPDX-License-Identifier: GPL-2.0-or-later
// The family removes_debug_menu_entry_keyed_by_object (symbols/families.csv, docs/FAMILIES.md):
// copies of one function that removes the debug menu entry an object is keyed by. When the debug
// menu manager (0x058b3570) exists, the key is the object and an interned empty name (0x04dd85f0,
// interned through 0x013aeaf0); 0x013a33a0 removes the entry with both of its flags 1, and the
// name is destroyed (0x013aebc0). game/ai/debug_display.cpp's remove_menu_entry builds the same
// key with another name. The manager is read once, before the name is interned.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct DebugMenuManager;

// A name interned in the engine's name pool (a handle, null when the pool does not exist).
struct InternedName {
    void *handle;
};

// What identifies the object's menu entry: the object and a name.
struct MenuKey {
    const void *owner;
    InternedName name;
};
static_assert(offsetof(MenuKey, name) == 0x8 && sizeof(MenuKey) == 0x10);

RT_GLOBAL(0x058b3570, debug_menu_manager_instance, DebugMenuManager *);
RT_GLOBAL(0x04dd85f0, entry_key_name, const char16_t);
RT_ORIGINAL(0x013a33a0, debug_menu_remove, void(DebugMenuManager *, MenuKey *key, int32_t, int32_t));
RT_ORIGINAL(0x013aeaf0, interned_name_construct, void(InternedName *, const char16_t *text));
RT_ORIGINAL(0x013aebc0, interned_name_destroy, void(InternedName *));

// The removal's two flags, as the copies pass them (what they select is not known).
constexpr int32_t menu_remove_flag_a = 1, menu_remove_flag_b = 1;

}  // namespace

extern "C" void bb_engine_removes_debug_menu_entry_keyed_by_object(const void *object)
{
    DebugMenuManager *menu = debug_menu_manager_instance.get();
    if (!menu) return;
    MenuKey key;
    key.owner = object;
    interned_name_construct(&key.name, entry_key_name.address());
    debug_menu_remove(menu, &key, menu_remove_flag_a, menu_remove_flag_b);
    interned_name_destroy(&key.name);
}
