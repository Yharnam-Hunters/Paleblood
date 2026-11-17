// SPDX-License-Identifier: GPL-2.0-or-later
// The family posts_text_event_to_delegate_at_0x50 (symbols/families.csv): copies of one engine
// function. It assigns the text the object points at (+0x48) to the wide string it gets, then
// posts the event {2, -, that string} to the object's delegate (delegate_event.h), when it is set.
// The originals leave the event's middle dword unset on the stack; it is 0 here.
#include "delegate_event.h"
#include "wide_string.h"

namespace {

RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));

struct TextPoster {
    uint8_t unknown_0x0[0x48];
    const char16_t *text;
};
static_assert(offsetof(TextPoster, text) == 0x48);

constexpr uint32_t text_event_kind = 2;

}  // namespace

extern "C" void bb_engine_posts_text_event_to_delegate_at_0x50(void *poster, engine::WideString *string)
{
    const char16_t *text = static_cast<const TextPoster *>(poster)->text;
    const uint64_t length = text[0] != 0 ? c_wcslen(text) : 0;
    engine::wide_string_assign(string, text, length);
    engine::delegate_event::post(poster, text_event_kind, 0, reinterpret_cast<uint64_t>(string));
}
