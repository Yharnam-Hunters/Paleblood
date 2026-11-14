// SPDX-License-Identifier: GPL-2.0-or-later
// A refusing canonical host must abort; malformed final shapes must emit no event.
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

extern "C" void bb_render_set_gs_mode(render::PacketBuffer *, uint32_t, uint32_t);

namespace {
int event_pipe = -1;
unsigned checks;
constexpr std::size_t pipe_ends = 2, event_bytes = 2;
constexpr uint32_t cut_value_mask = 3;
constexpr int redirection_failed = 2, event_write_failed = 3;
enum class Scenario { RefusingHost, FirstHeader, LastHeader };
constexpr std::array<Scenario, 3> scenarios{Scenario::RefusingHost, Scenario::FirstHeader, Scenario::LastHeader};
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
        close(descriptors[0]); event_pipe = descriptors[1];
        const rlimit no_core{0, 0}; setrlimit(RLIMIT_CORE, &no_core);
        if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(redirection_failed);
        alignas(uintptr_t) std::array<uint8_t, 256> prefix{}, packet{};
        constexpr std::size_t prefix_offset = 64;
        auto *commands = reinterpret_cast<render::PacketBuffer *>(prefix.data() + prefix_offset);
        const uintptr_t begin = reinterpret_cast<uintptr_t>(prefix.data());
        const uintptr_t owner = reinterpret_cast<uintptr_t>(commands);
        // Keep host refusal separate from damaged first and second packets.
        const uintptr_t cursor = scenario == Scenario::RefusingHost ? reinterpret_cast<uintptr_t>(packet.data()) :
            owner + (scenario == Scenario::FirstHeader ? offsetof(render::PacketBuffer, cursor) : render::packet_word_bytes);
        field(commands, offsetof(render::PacketBuffer, begin), scenario == Scenario::RefusingHost ? cursor : begin);
        field(commands, offsetof(render::PacketBuffer, limit), cursor + packet.size());
        field(commands, offsetof(render::PacketBuffer, cursor), cursor);
        bb_render_set_gs_mode(commands, scenario == Scenario::LastHeader ? 0u : 1u, cut_value_mask);
        std::_Exit(0);
    }
    close(descriptors[1]); int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    std::array<char, event_bytes> events{};
    const ssize_t count = read(descriptors[0], events.data(), events.size()); close(descriptors[0]);
    CHECK(count == (scenario == Scenario::RefusingHost ? 1 : 0));
}
}  // namespace

extern "C" int rt_render_record_draw_auto(uint64_t, uint32_t, uint32_t, char *, std::size_t) { return 0; }

extern "C" int rt_render_record_register(uint64_t, uint32_t, uint32_t, uint32_t, char *error, std::size_t size)
{
    const char event = 'r';
    if (write(event_pipe, &event, 1) != 1) std::_Exit(event_write_failed);
    if (size) error[0] = 0;
    return -1;
}

int main()
{
    CHECK(rt_render_observer_available());
    for (Scenario scenario : scenarios) rejected(scenario);
    std::printf("GS-mode refusal observer: %zu scenarios, %u checks, zero failures\n", scenarios.size(), checks);
    return 0;
}
