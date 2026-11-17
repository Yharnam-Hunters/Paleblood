// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: tpfbnd binder resource constructor (structs in tpfbnd_resource.h).
//
// loading_tpfbnd_resource_construct_0181fb10 (0x0181fb10): constructs the SprjFile-backed
// binder resource base (0x017ea230) with the caller's type name and heap word, then installs
// the tpfbnd resource vtable (0x0576ac10). Its allocation site in the binder cache
// (0x01822120) inlines the same two steps with u"tpfbnd" and 0.
//
// No direct or indirect callers of 0x0181fb10 exist in the executable (only unwind metadata),
// so void callers cannot be proved from call sites. Because the original unconditionally leaves
// the relocated vtable address in RAX at return, the replacement returns that pointer (i64)
// rather than relying on void codegen.
#include "tpfbnd_resource.h"

using namespace loading::tpfbnd;

extern "C" const uint8_t *bb_loading_tpfbnd_resource_construct_0181fb10(TpfbndResource *resource,
                                                                       const char16_t *type_name,
                                                                       uint64_t heap_word)
{
    binder_resource_construct(resource, type_name, heap_word);
    const uint8_t *vtable = tpfbnd_resource_vtable.address();
    resource->vtable = vtable;
    return vtable;
}
