// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize one static empty wide string and register its original destructor. The guard
// is tested as a byte, then published as a full qword only after registration returns.
#include "static_wide_string.h"
#include "engine.h"

extern "C" void bb_engine_initialize_static_wide_string_0217ffb0()
{
    namespace string = engine::static_wide_string;
    if (static_cast<uint8_t>(string::initialization_guard.get()) != 0) return;

    engine::Allocator *allocator = string::default_allocator.get();
    if (!allocator) {
        allocator = string::acquire_default_allocator();
        string::default_allocator.get() = allocator;
    }

    string::HeapInfoPrefix info;
    // Keep the observed eight-byte output prefix without changing the shared allocator view.
    const auto query_info = reinterpret_cast<string::HeapInfoCall>(allocator->vtable->info);
    query_info(&info, allocator, 0);
    if ((info.flags & engine::allocator_info_valid) == 0) {
        engine::fatal_error(nullptr, string::invalid_heap_line, string::invalid_heap_message.address());
    }

    // Retain the captured allocator even if the info or diagnostic call changes its singleton.
    engine::WideString &value = string::instance.get();
    value.allocator = allocator;
    value.capacity = engine::wide_string_inline_capacity - 1;
    value.size = 0;
    value.inline_text[0] = u'\0';
    value.owns = 1;
    const auto destructor = reinterpret_cast<string::Destructor>(string::destroy_instance.address());
    (void)string::register_destructor(destructor, &value, string::destructor_dso.address());
    string::initialization_guard.get() = 1;
}
