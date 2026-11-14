// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the config getter the flipper reads its flip modes with
// (frame_timing_config_get_int_024eba00).
//
// The config is an ordered map from wide-string keys to wide-string values. A missing key gives
// the caller's fallback. Otherwise the value is copied into a working string (on the value's
// allocator, with the value's owns byte), every reference marker in it is expanded in turn (the
// expansion returns where the next search starts), and the text is parsed as a base-10 long whose
// low 32 bits are returned; a heap buffer the copy ended up with is freed through its allocator.
#include <cstddef>
#include <cstdint>

#include "../engine/msvc_tree.h"
#include "../engine/wide_string.h"
#include "../engine/wide_string_assign.h"
#include "runtime/original.h"

namespace {

using engine::WideString;
using ConfigNode = engine::TreeNode<WideString, WideString>;
static_assert(offsetof(ConfigNode, key) == 0x20 && offsetof(ConfigNode, value) == 0x58);

struct Config {
    uint8_t unknown_0x00[0x08];
    ConfigNode *head;
};
static_assert(offsetof(Config, head) == 0x08);

// What the lookup fills in: the found node (the head when the key is missing) after two words
// this getter never reads.
struct ConfigLookup {
    uint8_t unknown_0x00[0x10];
    ConfigNode *node;
};
static_assert(offsetof(ConfigLookup, node) == 0x10);

constexpr uint64_t not_found = ~uint64_t{0};
constexpr int decimal = 10;

RT_ORIGINAL(0x024ed680, config_lookup, ConfigLookup *(ConfigLookup *out, Config *config, WideString *key));
RT_ORIGINAL(0x024ed7e0, config_expand_reference, uint64_t(Config *config, WideString *text, uint64_t at));
RT_ORIGINAL(0x02d74270, wide_string_find,
            uint64_t(WideString *text, const char16_t *needle, uint64_t from, uint64_t needle_length));
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));
RT_ORIGINAL(0x02fc02a8, c_wcstol, int64_t(const char16_t *text, char16_t **end, int base));
RT_GLOBAL(0x04dc2012, reference_marker, const char16_t);

uint64_t find_reference(WideString *text, uint64_t from)
{
    const char16_t *marker = reference_marker.address();
    return wide_string_find(text, marker, from, c_wcslen(marker));
}

}  // namespace

extern "C" uint32_t bb_frame_timing_config_get_int_024eba00(Config *config, WideString *key, uint32_t fallback)
{
    ConfigLookup lookup;
    config_lookup(&lookup, config, key);
    ConfigNode *node = lookup.node;
    if (node == config->head) return fallback;

    WideString text;
    engine::wide_string_begin(&text, node->value.allocator);
    engine::wide_string_assign_from(&text, &node->value, 0, engine::wide_string_rest);
    text.owns = node->value.owns;

    for (uint64_t at = find_reference(&text, 0); at != not_found;)
        at = find_reference(&text, config_expand_reference(config, &text, at));

    const char16_t *chars = text.capacity >= engine::wide_string_inline_capacity ? text.heap : text.inline_text;
    char16_t *end;
    const auto value = static_cast<uint32_t>(c_wcstol(chars, &end, decimal));
    engine::wide_string_release(&text);
    return value;
}
