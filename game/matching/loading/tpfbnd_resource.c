// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the tpfbnd binder resource constructor
// (game/loading/tpfbnd_resource.cpp).

typedef struct {
    const void *vtable;
    char unknown_0x08[0xa0];
} TpfbndResource;

extern const char tpfbnd_resource_vtable[];
void binder_resource_construct(TpfbndResource *resource, const void *type_name, unsigned long heap_word);

// Constructs the base binder resource, installs the tpfbnd vtable, and preserves the vtable pointer
// left in RAX at return.
const void *loading_tpfbnd_resource_construct_0181fb10(TpfbndResource *resource, const void *type_name,
                                                       unsigned long heap_word)
{
    binder_resource_construct(resource, type_name, heap_word);
    resource->vtable = tpfbnd_resource_vtable;
    return tpfbnd_resource_vtable;
}
