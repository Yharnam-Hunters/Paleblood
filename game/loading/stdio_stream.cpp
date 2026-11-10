// SPDX-License-Identifier: GPL-2.0-or-later
// Files opened through the C library's stdio: a stream object that opens its path with fopen
// and records why it failed, and a holder of three FILE pointers that closes them all.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct CFile;   // the C library's FILE

RT_ORIGINAL(0x02fbe738, c_fopen, CFile *(const char *path, const char *mode));
RT_ORIGINAL(0x02fbe748, c_rewind, void(CFile *));
RT_ORIGINAL(0x02fbe7b8, c_fclose, int32_t(CFile *));
RT_ORIGINAL(0x02fbe758, c_errno_location, int32_t *());   // __error

// fopen modes in the executable's read-only data (the same strings are passed on).
RT_GLOBAL(0x04b3e4f0, mode_read, const char);               // "rb"
RT_GLOBAL(0x04b3e501, mode_read_update, const char);        // "r+b"
RT_GLOBAL(0x04b3e4f7, mode_write, const char);              // "wb"
RT_GLOBAL(0x04b3e4f3, mode_write_update, const char);       // "w+b"
RT_GLOBAL(0x04b3e4fe, mode_append, const char);             // "ab"
RT_GLOBAL(0x04b3e4fa, mode_append_update, const char);      // "a+b"

// The stream's open mode bits.
constexpr uint32_t stream_read_too = 0x01;   // with truncate or append: "+" (read as well)
constexpr uint32_t stream_update = 0x02;     // without truncate or append: "r+b"
constexpr uint32_t stream_truncate = 0x04;
constexpr uint32_t stream_append = 0x08;

// A path string: a pointer with two tag bits, whose characters start 12 bytes in.
struct PathString {
    uint8_t unknown_0x00[0xc];
    char text[1];
};
static_assert(offsetof(PathString, text) == 0xc);
constexpr uintptr_t path_tag_bits = 0x3;

struct StdioStream {
    uint8_t unknown_0x00[0x10];
    uintptr_t path;        // tagged PathString pointer
    uint8_t is_open;
    uint8_t unknown_0x19[0x7];
    CFile *file;
    uint32_t mode;
    uint32_t error;        // 0, or one of the stream_error values
    uint32_t position;     // reset by every open
};
static_assert(offsetof(StdioStream, path) == 0x10 && offsetof(StdioStream, is_open) == 0x18);
static_assert(offsetof(StdioStream, file) == 0x20 && offsetof(StdioStream, mode) == 0x28);
static_assert(offsetof(StdioStream, error) == 0x2c && offsetof(StdioStream, position) == 0x30);

// FreeBSD errno values and the stream's own error codes for them.
constexpr int32_t errno_not_permitted = 1;    // EPERM
constexpr int32_t errno_no_entry = 2;         // ENOENT
constexpr int32_t errno_access = 13;          // EACCES
constexpr int32_t errno_no_space = 28;        // ENOSPC
constexpr uint32_t stream_error_not_found = 0x1001;
constexpr uint32_t stream_error_denied = 0x1002;
constexpr uint32_t stream_error_other = 0x1003;
constexpr uint32_t stream_error_no_space = 0x1004;

const char *fopen_mode(uint32_t mode)
{
    if (mode & stream_truncate) return (mode & stream_read_too) ? mode_write_update.address() : mode_write.address();
    if (mode & stream_append) return (mode & stream_read_too) ? mode_append_update.address() : mode_append.address();
    return (mode & stream_update) ? mode_read_update.address() : mode_read.address();
}

// errno is fetched again for every comparison, as the original does.
uint32_t error_of_errno()
{
    if (*c_errno_location() == errno_no_entry) return stream_error_not_found;
    if (*c_errno_location() == errno_access) return stream_error_denied;
    if (*c_errno_location() == errno_not_permitted) return stream_error_denied;
    if (*c_errno_location() == errno_no_space) return stream_error_no_space;
    return stream_error_other;
}

// Three files closed together; which file is which is not known yet.
constexpr size_t stream_file_count = 3;
struct StreamFiles {
    CFile *files[stream_file_count];
};

}  // namespace

// Opens the stream's path in the mode its bits select and rewinds it. Callers 0x00464b84 and
// 0x00464cb4 (tail calls) and 0x00465330.
extern "C" void bb_loading_stdio_stream_open(StdioStream *stream)
{
    const auto *path = reinterpret_cast<const PathString *>(stream->path & ~path_tag_bits);
    CFile *file = c_fopen(path->text, fopen_mode(stream->mode));
    stream->file = file;
    bool open = false;
    if (file) {
        c_rewind(file);
        open = stream->file != nullptr;   // the original reads the field again after the rewind
    }
    stream->is_open = open;
    stream->error = open ? 0 : error_of_errno();
    stream->position = 0;
}

// Closes and clears each open file, last to first. Callers around 0x02e62f32..0x02e6309b.
extern "C" void bb_loading_stdio_close_files(StreamFiles *holder)
{
    for (size_t i = stream_file_count; i-- > 0;) {
        if (!holder->files[i]) continue;
        c_fclose(holder->files[i]);
        holder->files[i] = nullptr;
    }
}
