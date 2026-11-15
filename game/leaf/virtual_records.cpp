// SPDX-License-Identifier: GPL-2.0-or-later
// Leaf families that call virtual functions of their objects (symbols/families.csv); every member
// of each is the same bytes.
//
// The string writers pass a text and a value to slot 0x90 of a sink (a variadic call: the value is
// the result of the record's own slot 0x60). The toggles switch something on or off through slot
// 0 of their object and keep the state in a byte. The resets free a wide string's heap text through
// slot 0x70 of the allocator stored after it and leave the string empty.
//
// A wide string holds 16-bit characters: up to seven and the terminator inline, else a pointer to
// heap text in the same place; its size and capacity follow.
#include <cstddef>
#include <cstdint>

namespace {

// The function slot_offset bytes into the object's virtual table.
template <typename Function>
Function virtual_slot(const void *object, std::size_t slot_offset)
{
    const uint8_t *virtual_table = *static_cast<const uint8_t *const *>(object);
    return *reinterpret_cast<const Function *>(virtual_table + slot_offset);
}

constexpr uint64_t heap_capacity = 8;     // a capacity this large keeps the text on the heap
constexpr uint64_t inline_capacity = 7;   // an empty string's capacity

struct WideString {
    union {
        char16_t inline_text[heap_capacity];
        char16_t *heap_text;
    };
    uint64_t size;
    uint64_t capacity;

    const char16_t *text() const { return capacity < heap_capacity ? inline_text : heap_text; }
};
static_assert(offsetof(WideString, size) == 0x10);
static_assert(offsetof(WideString, capacity) == 0x18);

// Writers.
using ValueFunction = uint64_t (*)(void *record);
using SinkWrite = uint64_t (*)(void *sink, const char16_t *text, ...);
constexpr std::size_t value_slot = 0x60;
constexpr std::size_t write_slot = 0x90;

struct RecordWithStringAt0x28 {
    uint8_t unknown_0x0[0x28];
    WideString string;
};
static_assert(offsetof(RecordWithStringAt0x28, string) == 0x28);

struct StringAt8 {
    uint64_t unknown_0x0;
    WideString string;
};

struct RecordPointingAtString {
    uint8_t unknown_0x0[0x20];
    const StringAt8 *string;
};
static_assert(offsetof(RecordPointingAtString, string) == 0x20);

struct RecordWithTextAt0x20 {
    uint8_t unknown_0x0[0x20];
    const char16_t *text;
};
static_assert(offsetof(RecordWithTextAt0x20, text) == 0x20);

// The sink's write is read first and the text before the record's value is asked for, as in the
// originals.
uint64_t write_to_sink(void *record, void *sink, SinkWrite write, const char16_t *text)
{
    const uint64_t value = virtual_slot<ValueFunction>(record, value_slot)(record);
    return write(sink, text, value);
}

// Toggles: slot 0 switches on (any second argument but 0) or off (0); a result whose low 32 bits
// are not 0 is a failure and comes back whole, leaving the state byte as it was.
using SwitchFunction = uint64_t (*)(void *object, uint64_t on);
constexpr std::size_t switch_slot = 0;

template <std::size_t StateOffset>
uint64_t toggle(void *object, uint64_t on)
{
    uint8_t *state = static_cast<uint8_t *>(object) + StateOffset;
    if (on != 0 && *state != 0)
        return 0;
    const uint64_t result = virtual_slot<SwitchFunction>(object, switch_slot)(object, on);
    if (static_cast<uint32_t>(result) != 0)
        return result;
    *state = on != 0;
    return 0;
}

// Resets.
using FreeFunction = void (*)(void *allocator, char16_t *text);
constexpr std::size_t free_slot = 0x70;

struct OwnedWideString {
    WideString string;
    void *allocator;
};
static_assert(offsetof(OwnedWideString, allocator) == 0x20);

template <std::size_t Offset>
void reset(void *object)
{
    auto *owned = reinterpret_cast<OwnedWideString *>(static_cast<uint8_t *>(object) + Offset);
    if (owned->string.capacity >= heap_capacity)
        virtual_slot<FreeFunction>(owned->allocator, free_slot)(owned->allocator, owned->string.heap_text);
    owned->string.capacity = inline_capacity;
    owned->string.size = 0;
    owned->string.inline_text[0] = 0;
}

constexpr std::size_t first_state = 8;
constexpr std::size_t second_state = 0x20;
constexpr std::size_t first_string = 8;
constexpr std::size_t second_string = 0x10;

}  // namespace

extern "C" uint64_t bb_leaf_writes_wide_string_at_0x28_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    return write_to_sink(record, sink, write, static_cast<const RecordWithStringAt0x28 *>(record)->string.text());
}

extern "C" uint64_t bb_leaf_writes_wide_string_through_0x20_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    const char16_t *text = static_cast<const RecordPointingAtString *>(record)->string->string.text();
    return write_to_sink(record, sink, write, text);
}

extern "C" uint64_t bb_leaf_writes_text_at_0x20_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    return write_to_sink(record, sink, write, static_cast<const RecordWithTextAt0x20 *>(record)->text);
}

extern "C" uint64_t bb_leaf_toggles_flag_at_8_through_first_virtual(void *object, uint64_t on)
{
    return toggle<first_state>(object, on);
}

extern "C" uint64_t bb_leaf_toggles_flag_at_0x20_through_first_virtual(void *object, uint64_t on)
{
    return toggle<second_state>(object, on);
}

extern "C" void bb_leaf_resets_wide_string_at_8(void *object)
{
    reset<first_string>(object);
}

extern "C" void bb_leaf_resets_wide_string_at_0x10(void *object)
{
    reset<second_string>(object);
}
