// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: reading from the input stream over memory (structs in memory_stream.h).
//
// loading_memory_stream_read_024de230 (0x024de230), slot 4 of the stream's vtable (0x057b5090):
// copies up to `count` bytes from the current position and returns how many. A stream that is not
// open returns -1; a count of 0 returns 0. The number is the smaller of the count and what is left,
// cut to 32 bits: if that is not positive (nothing left, or 2 GB or more), nothing is copied and
// the result is 0. Otherwise both the position and the word at +0x20 move on by it.
#include <cstdint>

#include "memory_stream.h"

using namespace loading::memory_stream;

extern "C" int32_t bb_loading_memory_stream_read_024de230(MemoryInputStream *stream, void *destination,
                                                          uint64_t count)
{
    if (!stream->vtable->is_open(stream))
        return stream_not_open;
    if (count == 0)
        return 0;
    const uint64_t left = stream->size - stream->position;
    const uint64_t wanted = count < left ? count : left;
    const auto copied = static_cast<int32_t>(wanted);
    if (copied <= 0)
        return 0;
    engine_memcpy(destination, stream->data + stream->position, copied);
    stream->count_0x20 += copied;
    stream->position += copied;
    return copied;
}
