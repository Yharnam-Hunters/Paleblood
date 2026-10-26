// SPDX-License-Identifier: GPL-2.0-or-later
// Open with fixed zero flags/mode, query the opaque status, and publish one word
// before the descriptor. The caller at 0x011a40b0 supplies both output pointers.
// Preserve the captured guest guard even when an output aliases its GOT slot.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace {

constexpr std::size_t status_bytes = 120;
constexpr std::size_t status_word_offset = 0x48;
constexpr std::size_t status_tail_offset = 0x4c;
constexpr std::size_t status_start_alignment = 16;
constexpr int open_error = 0x17;
constexpr int status_error = 0x21;

// The consumer proves only this word and the available extent, not a host stat
// layout or initialization of the other bytes. The instance starts 16-aligned.
struct OpaqueFileStatus {
    std::byte unknown_0x00[status_word_offset];
    uint32_t word_48;
    std::byte unknown_0x4c[status_bytes - status_tail_offset];
};
static_assert(offsetof(OpaqueFileStatus, word_48) == status_word_offset);
static_assert(sizeof(OpaqueFileStatus) == status_bytes);

RT_ORIGINAL(0x02fbf3f8, kernel_open_file,
            int32_t(const char *path, uint64_t flags, uint64_t mode));
RT_ORIGINAL(0x02fbfe48, kernel_file_status,
            int32_t(uint64_t descriptor, OpaqueFileStatus *status));
RT_GLOBAL(0x057e3be0, guest_guard_object, const std::byte *);
RT_ORIGINAL(0x02fbe4f8, guest_stack_check_failed, void());

uint64_t read_guard(const std::byte *address)
{
    uint64_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

[[noreturn]] void fail_guard()
{
    guest_stack_check_failed();
    // The original import does not return; a returning substitute is outside
    // its contract and cannot be assigned a synthetic wrapper error result.
    __builtin_unreachable();
}

}  // namespace

extern "C" int bb_kernel_file_open(const char *path, uintptr_t, uintptr_t,
                                    void *word_out, void *descriptor_out)
{
    const std::byte *const guard_address = guest_guard_object.get();
    const uint64_t entry_guard = read_guard(guard_address);
    const int32_t descriptor = kernel_open_file(path, 0, 0);
    int result = open_error;
    if (descriptor >= 0) {
        // No initialization: success must supply word_48; no other status byte
        // is read. Failed status calls need not initialize that word.
        alignas(status_start_alignment) OpaqueFileStatus status;
        result = status_error;
        if (kernel_file_status(static_cast<uint64_t>(static_cast<uint32_t>(descriptor)), &status) == 0) {
            const uint32_t selected_word = status.word_48;
            const int64_t published_descriptor = descriptor;
            std::memcpy(word_out, &selected_word, sizeof(selected_word));
            std::memcpy(descriptor_out, &published_descriptor, sizeof(published_descriptor));
            result = 0;
        }
    }
    // Both stores and either error path precede this read of the original
    // captured pointee. An output may have changed the pointer slot itself.
    if (read_guard(guard_address) != entry_guard) {
        fail_guard();
    }
    return result;
}
