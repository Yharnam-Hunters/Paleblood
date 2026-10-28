// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD's thread-name lookup copies a 64-bit affinity mask from an eight-slot table.
// Nonblocking names match their first 23 bytes; the other names must match exactly.
// An unknown name still reports success and leaves the output untouched.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct FmodThreadAffinities {
    uint64_t mixer;
    uint64_t audio_out;
    uint64_t stream;
    uint64_t nonblocking;
    uint64_t file;
    uint64_t geometry;
    uint64_t event_net_node;
    uint64_t record_update;
};
static_assert(offsetof(FmodThreadAffinities, mixer) == 0x00);
static_assert(offsetof(FmodThreadAffinities, audio_out) == 0x08);
static_assert(offsetof(FmodThreadAffinities, stream) == 0x10);
static_assert(offsetof(FmodThreadAffinities, nonblocking) == 0x18);
static_assert(offsetof(FmodThreadAffinities, file) == 0x20);
static_assert(offsetof(FmodThreadAffinities, geometry) == 0x28);
static_assert(offsetof(FmodThreadAffinities, event_net_node) == 0x30);
static_assert(offsetof(FmodThreadAffinities, record_update) == 0x38);

RT_ORIGINAL(0x011c4970, compare_thread_names, int(const char *left, const char *right));
RT_ORIGINAL(0x011c4990, compare_thread_name_prefix, int(const char *left, const char *right, int32_t length));

RT_GLOBAL(0x0586d430, fmod_thread_affinities, FmodThreadAffinities);
RT_GLOBAL(0x04c7d061, mixer_name, const char);
RT_GLOBAL(0x04c7d073, audio_out_name, const char);
RT_GLOBAL(0x04c7d088, stream_name, const char);
RT_GLOBAL(0x04c7d09b, nonblocking_name, const char);
RT_GLOBAL(0x04c7d0b3, file_name, const char);
RT_GLOBAL(0x04c7d0c4, geometry_name, const char);
RT_GLOBAL(0x04c7d0d9, event_net_node_name, const char);
RT_GLOBAL(0x04c7d0f2, record_update_name, const char);

constexpr int32_t nonblocking_prefix_length = 23;

}  // namespace

extern "C" int bb_audio_fmod_thread_affinity_lookup(const char *name, uint64_t *out)
{
    if (compare_thread_names(name, mixer_name.address()) == 0)
        *out = fmod_thread_affinities.get().mixer;
    else if (compare_thread_names(name, audio_out_name.address()) == 0)
        *out = fmod_thread_affinities.get().audio_out;
    else if (compare_thread_names(name, stream_name.address()) == 0)
        *out = fmod_thread_affinities.get().stream;
    else if (compare_thread_name_prefix(name, nonblocking_name.address(), nonblocking_prefix_length) == 0)
        *out = fmod_thread_affinities.get().nonblocking;
    else if (compare_thread_names(name, file_name.address()) == 0)
        *out = fmod_thread_affinities.get().file;
    else if (compare_thread_names(name, geometry_name.address()) == 0)
        *out = fmod_thread_affinities.get().geometry;
    else if (compare_thread_names(name, event_net_node_name.address()) == 0)
        *out = fmod_thread_affinities.get().event_net_node;
    else if (compare_thread_names(name, record_update_name.address()) == 0)
        *out = fmod_thread_affinities.get().record_update;
    return 0;
}
