// SPDX-License-Identifier: GPL-2.0-or-later
// The engine containers' answer to an allocation that fails ("Transaction failed"): inside a
// transaction (a thread-local flag) the thread jumps back to where the transaction started; outside
// one, the failure is reported and the caller carries on with the null buffer.
#pragma once

#include <cstdint>

#include "engine.h"
#include "runtime/original.h"
#include "thread.h"

namespace engine {

// Offsets from the thread pointer of the thread's transaction flag and its jump buffer.
RT_GLOBAL(0x057e4318, transaction_active_offset, const int64_t);
RT_GLOBAL(0x057e4320, transaction_jump_buffer_offset, const int64_t);
RT_ORIGINAL(0x02fbe978, c_longjmp, void(void *buffer, int32_t value));
// "Transaction failed. See above message for detail."
RT_GLOBAL(0x04b37d80, transaction_failed_message, const char);
constexpr int32_t transaction_failed_line = 0x109;

// What a container does when its allocator returns null. The jump does not come back; in the
// game, nothing follows the call to it.
inline void report_failed_allocation()
{
    unsigned char *thread = thread_pointer();
    if (*reinterpret_cast<int32_t *>(thread + transaction_active_offset.get()) != 0)
        c_longjmp(thread + transaction_jump_buffer_offset.get(), 1);
    fatal_error(nullptr, transaction_failed_line, transaction_failed_message.address());
}

}  // namespace engine
