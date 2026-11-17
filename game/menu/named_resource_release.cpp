// SPDX-License-Identifier: GPL-2.0-or-later
// Two families (symbols/families.csv) of the named resource release of
// the menu_named_resource_release_0238a210 replacement in scaleform_glue.cpp: its copies, on the same layout
// (releases_named_resource_held_at_0x70), and the copies of the same function on a layout whose
// members sit 8 bytes earlier (releases_named_resource_held_at_0x68). When the resource is held,
// let go of its handle; while the registry exists, release the registry's entry for its name
// (when found) and empty the name; then mark it not loaded.
#include <cstddef>
#include <cstdint>

#include "scaleform_glue.h"

namespace {

using menu::scaleform::NamedResource;

struct NamedResourceAt0x68 {
    uint8_t unknown_0x00[0x68];
    uint8_t held;
    uint8_t loaded;
    uint8_t unknown_0x6a[0x6];
    void *handle;
    engine::WideString name;
};
static_assert(offsetof(NamedResourceAt0x68, held) == 0x68 && offsetof(NamedResourceAt0x68, handle) == 0x70);
static_assert(offsetof(NamedResourceAt0x68, name) == 0x78);

char16_t *wide_text(engine::WideString &name)
{
    return name.capacity >= engine::wide_string_inline_capacity ? name.heap : name.inline_text;
}

template <typename Resource> void release_named_resource(Resource *resource)
{
    using namespace menu::scaleform;
    if (!resource->held) return;
    if (resource->handle) {
        handle_release(resource->handle);
        resource->handle = nullptr;
    }
    if (named_resource_registry.get()) {
        void *entry = registry_find(named_resource_registry.get(), wide_text(resource->name));
        if (named_resource_registry.get() && entry) registry_release(named_resource_registry.get(), entry, 1, 1);
        resource->name.size = 0;
        wide_text(resource->name)[0] = u'\0';
    }
    resource->loaded = 0;
}

}  // namespace

extern "C" void bb_menu_releases_named_resource_held_at_0x70(NamedResource *resource)
{
    release_named_resource(resource);
}

extern "C" void bb_menu_releases_named_resource_held_at_0x68(NamedResourceAt0x68 *resource)
{
    release_named_resource(resource);
}
