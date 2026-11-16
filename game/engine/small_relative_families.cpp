// SPDX-License-Identifier: GPL-2.0-or-later
// Twelve small engine families (symbols/families.csv), each the copies of one function that reach
// the same functions and globals (a relative shape):
//  - destroys_and_frees_object_through_owning_heap: destroy an object (its table's second slot),
//    then free it through the heap that owns it;
//  - releases_path_entry_then_deletes_it / finds_path_entry_by_text: release an entry from, or
//    find one in, the path table (loading/path_hash_table.cpp) an object holds at +0x68;
//  - adds_reference_on_interface_id_0x04b2f320: hand out the object for one interface id;
//  - reports_fatal_error_line_0xad: report one fatal error;
//  - resolves_kind_and_index_then_calls_slot_0x38 / _0x58: resolve an object by a kind and an
//    index, then call one of its table's slots;
//  - stores_vtable_releases_tagged_string_0x38_then_destroys,
//    stores_vtable_calls_slot_0x18_clears_flag_then_destroys and its deleting twin: destructors;
//  - clears_hk_array_of_4_byte_elements: clear an hkArray and free its buffer;
//  - releases_0x18_byte_elements_at_0x18_then_empties: release an array's elements and empty it.
#include <cstddef>
#include <cstdint>

#include "../loading/fios.h"
#include "allocator.h"
#include "buffer_release.h"
#include "engine.h"
#include "havok.h"
#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

// An object whose table's second slot destroys it (without freeing its memory).
struct Object;
struct ObjectVtable {
    void *unknown_slot_0x0;
    void (*destroy)(Object *);
};
static_assert(offsetof(ObjectVtable, destroy) == 0x8);
struct Object {
    const ObjectVtable *vtable;
};

void destroy_and_free(Object *object)
{
    engine::Allocator *heap = engine::heap_owning(object);
    object->vtable->destroy(object);
    heap->free(object);
}

// The path table (bucket count +0x1c, buckets +0x20) and the key it is searched with: wide
// text and its hash, marked stale so that the hash is computed first.
struct PathTable {
    uint8_t unknown_0x00[0x28];
};
struct PathKey {
    const char16_t *text;
    uint32_t hash;
    uint8_t hash_stale;
};
static_assert(offsetof(PathKey, hash) == 0x8 && offsetof(PathKey, hash_stale) == 0xc);
struct PathTableHolder {
    uint8_t unknown_0x00[0x68];
    PathTable table;
};
static_assert(offsetof(PathTableHolder, table) == 0x68);
RT_ORIGINAL(0x013e68f0, path_table_find, void *(PathTable *table, PathKey *key));
// Drops a use of an entry; true when the entry was the last use and has left the table.
RT_ORIGINAL(0x013e6ae0, path_table_release, bool(PathTable *table, Object *entry));
constexpr uint8_t hash_stale = 1;

// A reference-counted object: its table's second slot adds a reference.
struct Interface;
struct InterfaceVtable {
    void *unknown_slot_0x0;
    void (*add_reference)(Interface *);
};
static_assert(offsetof(InterfaceVtable, add_reference) == 0x8);
struct Interface {
    const InterfaceVtable *vtable;
};
RT_ORIGINAL(0x02fbe638, compare_memory, int32_t(const void *left, const void *right, uint64_t size));   // memcmp
RT_GLOBAL(0x04b2f320, handed_out_interface_id, const uint8_t);
constexpr uint64_t interface_id_size = 0x10;
constexpr uint32_t no_interface = 0x80000004;   // returned whether or not the id matched

RT_GLOBAL(0x04b38390, fatal_error_message, const char);
constexpr int32_t fatal_error_line = 0xad;

// An object resolved by a kind and an index in a context, and the slots the families call.
struct Resolved;
struct ResolvedVtable {
    void *unknown_slots_0x00[7];
    uint64_t (*slot_0x38)(Resolved *, void *context);
    void *unknown_slots_0x40[3];
    uint64_t (*slot_0x58)(Resolved *, void *context);
};
static_assert(offsetof(ResolvedVtable, slot_0x38) == 0x38 && offsetof(ResolvedVtable, slot_0x58) == 0x58);
struct Resolved {
    const ResolvedVtable *vtable;
};
struct KindAndIndex {
    uint8_t unknown_0x0[0x8];
    uint16_t kind;
    int16_t index;
};
static_assert(offsetof(KindAndIndex, kind) == 0x8 && offsetof(KindAndIndex, index) == 0xa);
RT_ORIGINAL(0x02c4f560, resolve, Resolved *(void *context, uint16_t kind, int16_t index));

// The class with a tagged string at +0x38: its table (past the offset to top and the type
// information) and its base destructor.
struct TaggedStringOwner {
    const void *vtable;
    uint8_t unknown_0x8[0x30];
    uintptr_t name;
};
static_assert(offsetof(TaggedStringOwner, name) == 0x38);
constexpr uint32_t tagged_string_owner_class = 0x05686720;
constexpr uint32_t table_past_header = 0x10;
RT_ORIGINAL(0x00d5f020, destroy_tagged_string_owner_base, void(TaggedStringOwner *));

// The class whose destructor calls its own table's slot +0x18 and clears a flag.
struct FlaggedObject;
struct FlaggedObjectVtable {
    void *unknown_slots[3];
    void (*release)(FlaggedObject *);
};
static_assert(offsetof(FlaggedObjectVtable, release) == 0x18);
struct FlaggedObject {
    const FlaggedObjectVtable *vtable;
    uint8_t unknown_0x8[0x3];
    uint8_t flags;
};
static_assert(offsetof(FlaggedObject, flags) == 0xb);
RT_GLOBAL(0x057b9530, flagged_object_table, const FlaggedObjectVtable);
RT_ORIGINAL(0x0271a600, destroy_flagged_object_base, void(FlaggedObject *));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));
constexpr uint8_t flag_released = 0x40;

void release_and_clear_flag(FlaggedObject *object)
{
    const FlaggedObjectVtable *table = flagged_object_table.address();
    object->vtable = table;
    table->release(object);
    object->flags &= static_cast<uint8_t>(~flag_released);
    destroy_flagged_object_base(object);
}

RT_GLOBAL(0x058018b0, havok_heap, engine::HkMemoryAllocator);

struct Element {
    uint8_t unknown_0x00[0x18];
};
struct ElementArray {
    uint8_t unknown_0x00[0x18];
    Element *begin;
    Element *end;
};
static_assert(sizeof(Element) == 0x18 && offsetof(ElementArray, begin) == 0x18 && offsetof(ElementArray, end) == 0x20);
RT_ORIGINAL(0x02fa7940, release_element, void(Element *));

}  // namespace

extern "C" void bb_engine_destroys_and_frees_object_through_owning_heap(Object *object)
{
    if (object)
        destroy_and_free(object);
}

extern "C" void bb_engine_releases_path_entry_then_deletes_it(PathTableHolder *holder, Object *entry)
{
    bool last_use = path_table_release(&holder->table, entry);
    if (entry && last_use)
        destroy_and_free(entry);
}

extern "C" void *bb_engine_finds_path_entry_by_text(PathTableHolder *holder, const char16_t *text)
{
    PathKey key;
    key.text = text;
    key.hash = 0;
    key.hash_stale = hash_stale;
    return path_table_find(&holder->table, &key);
}

extern "C" uint32_t bb_engine_adds_reference_on_interface_id_0x04b2f320(Interface *object, const void *id,
                                                                         Interface **out)
{
    if (compare_memory(id, handed_out_interface_id.address(), interface_id_size) == 0) {
        object->vtable->add_reference(object);
        *out = object;
    }
    return no_interface;
}

extern "C" void bb_engine_reports_fatal_error_line_0xad(void)
{
    engine::fatal_error(nullptr, fatal_error_line, fatal_error_message.address());
}

extern "C" uint64_t bb_engine_resolves_kind_and_index_then_calls_slot_0x38(const KindAndIndex *reference,
                                                                           void *context)
{
    Resolved *resolved = resolve(context, reference->kind, reference->index);
    return resolved->vtable->slot_0x38(resolved, context);
}

extern "C" uint64_t bb_engine_resolves_kind_and_index_then_calls_slot_0x58(const KindAndIndex *reference,
                                                                           void *context)
{
    Resolved *resolved = resolve(context, reference->kind, reference->index);
    return resolved->vtable->slot_0x58(resolved, context);
}

extern "C" void bb_engine_stores_vtable_releases_tagged_string_0x38_then_destroys(TaggedStringOwner *object)
{
    object->vtable = rt_guest(tagged_string_owner_class + table_past_header);
    loading::fios::tagged_string_release(&object->name);
    destroy_tagged_string_owner_base(object);
}

extern "C" void bb_engine_stores_vtable_calls_slot_0x18_clears_flag_then_destroys(FlaggedObject *object)
{
    release_and_clear_flag(object);
}

extern "C" void bb_engine_stores_vtable_calls_slot_0x18_clears_flag_destroys_then_deletes(FlaggedObject *object)
{
    release_and_clear_flag(object);
    operator_delete(object);
}

extern "C" void bb_engine_clears_hk_array_of_4_byte_elements(engine::HkArray<uint32_t> *array)
{
    array->size = 0;
    if (engine::hk_owns_buffer(array->capacity_and_flags)) {
        engine::HkMemoryAllocator *heap = havok_heap.address();
        auto bytes = static_cast<int32_t>(array->capacity_and_flags * sizeof(uint32_t));
        heap->vtable->buf_free(heap, array->data, bytes);
    }
    array->data = nullptr;
    array->capacity_and_flags = engine::hk_array_dont_deallocate;
}

extern "C" void bb_engine_releases_0x18_byte_elements_at_0x18_then_empties(ElementArray *array)
{
    Element *end = array->end;
    for (Element *element = array->begin; element != end; ++element)
        release_element(element);
    array->end = array->begin;
}
