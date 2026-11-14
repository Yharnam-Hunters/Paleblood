// SPDX-License-Identifier: GPL-2.0-or-later
// Distinguish refusing hosts from damaged final headers and register indexes.
#include "../packet_observation.h"
#include "../present_register_packets.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_esgs_ring_itemsize(render::PacketBuffer *, uint32_t);

namespace {
int event_pipe = -1;
unsigned checks;
bool accept_endpoint;
unsigned endpoint_calls;
uint64_t endpoint_buffer;
uint32_t endpoint_offset, endpoint_register, endpoint_value;
constexpr std::size_t pipe_ends = 2, event_bytes = 2, prefix_offset = 64;
constexpr int setup_failure_exit = 2;
constexpr uint32_t itemsize_sample = 0x4123, itemsize_with_ignored_high_bit = 0xc123;
constexpr uint32_t register_address = 0xa2ab;
constexpr std::array<uint32_t, 2> admitted_offsets{0, UINT32_MAX};
enum class Scenario { RefusingHost, Header, Index };
constexpr std::array<Scenario, 3> scenarios{Scenario::RefusingHost, Scenario::Header, Scenario::Index};
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (0)

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

void rejected(Scenario scenario)
{
    std::array<int, pipe_ends> descriptors; CHECK(pipe(descriptors.data()) == 0);
    const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        CHECK(close(descriptors[0]) == 0); event_pipe = descriptors[1];
        const rlimit no_core{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &no_core) == 0);
        if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(setup_failure_exit);
        alignas(uintptr_t) std::array<uint8_t, 256> prefix{}, packet{};
        auto *commands = reinterpret_cast<render::PacketBuffer *>(prefix.data() + prefix_offset);
        const uintptr_t owner = reinterpret_cast<uintptr_t>(commands);
        // +20 damages only the header, while +12 damages the index but keeps its header.
        const uintptr_t cursor = scenario == Scenario::RefusingHost ? reinterpret_cast<uintptr_t>(packet.data()) :
            owner + (scenario == Scenario::Header ? 20 : 12);
        field(commands, offsetof(render::PacketBuffer, begin),
              scenario == Scenario::RefusingHost ? cursor : reinterpret_cast<uintptr_t>(prefix.data()));
        field(commands, offsetof(render::PacketBuffer, limit), cursor + packet.size());
        field(commands, offsetof(render::PacketBuffer, cursor), cursor);
        bb_render_set_esgs_ring_itemsize(commands, itemsize_sample); std::_Exit(0);
    }
    CHECK(close(descriptors[1]) == 0); int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    std::array<char, event_bytes> events{};
    const ssize_t count = read(descriptors[0], events.data(), events.size()); CHECK(close(descriptors[0]) == 0);
    CHECK(count == (scenario == Scenario::RefusingHost ? 1 : 0));
}

void admitted_endpoints()
{
    // The recorder's submission size cannot include offset UINT32_MAX. A small
    // accepting host observes both admitted endpoints directly, without reading
    // the synthetic buffer identity as memory.
    accept_endpoint = true;
    for (uint32_t offset : admitted_offsets) {
        alignas(uintptr_t) std::array<uint8_t, 256> prefix{}, packet{};
        auto *commands = reinterpret_cast<render::PacketBuffer *>(prefix.data());
        const uintptr_t cursor = reinterpret_cast<uintptr_t>(packet.data());
        CHECK(cursor >= offset);
        const uintptr_t begin = cursor - offset;
        field(commands, offsetof(render::PacketBuffer, begin), begin);
        field(commands, offsetof(render::PacketBuffer, limit), cursor + packet.size());
        field(commands, offsetof(render::PacketBuffer, cursor), cursor);
        endpoint_calls = 0;
        bb_render_set_esgs_ring_itemsize(commands, itemsize_with_ignored_high_bit);
        CHECK(endpoint_calls == 1); CHECK(endpoint_buffer == begin);
        CHECK(endpoint_offset == offset); CHECK(endpoint_register == register_address);
        CHECK(endpoint_value == itemsize_sample);
        CHECK(render::packet_cursor(commands) == cursor + sizeof(render::PreparationCommandPacket));
    }
}
}  // namespace

extern "C" int rt_render_record_draw_auto(uint64_t, uint32_t, uint32_t, char *, std::size_t) { return 0; }
extern "C" int rt_render_record_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value,
                                        char *error, std::size_t size)
{
    if (accept_endpoint) {
        ++endpoint_calls; endpoint_buffer = buffer; endpoint_offset = offset;
        endpoint_register = address; endpoint_value = value;
        return 0;
    }
    const char event = 'r'; if (write(event_pipe, &event, 1) != 1) std::_Exit(3);
    if (size) error[0] = 0;
    return -1;
}

int main()
{
    CHECK(rt_render_observer_available());
    for (Scenario scenario : scenarios) rejected(scenario);
    admitted_endpoints();
    std::printf("ESGS-itemsize refusal observer: %zu scenarios, %u parent checks, zero failures\n", scenarios.size() + admitted_offsets.size(), checks);
    return 0;
}
