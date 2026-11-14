// SPDX-License-Identifier: GPL-2.0-or-later
// Actual writer refusal/offset tests and direct production-observer shape boundaries.
#include "../gs_ring_packets.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_gs_ring_sizes(render::PacketBuffer *, const uint32_t *, uint32_t);

namespace {
namespace ring = render::gs_ring;
unsigned scenarios, checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: game/render/tests/gs_ring_sizes_observer_failure.cpp:%d: %s\n", \
                 __LINE__, #condition); std::exit(1); \
} } while (0)
constexpr std::size_t event_count = 9, shape_fields = 8, packet_bytes = 68, pipe_ends = 2, stream_count = 4;
constexpr std::size_t events_per_stream = 2, maximum_event = event_count - 2, total_event = event_count - 1;
constexpr uint32_t initial_maximum = 3;
constexpr uint32_t itemsize_header = 0xc0046900, offset_header = 0xc0036900, scalar_header = 0xc0016900;
constexpr uint32_t itemsize_index = 0x2d7, offset_index = 0x298, maximum_index = 0x2ce, total_index = 0x2ac;
constexpr int setup_failure_exit = 2, pipe_failure_exit = 3;
constexpr std::array<uint32_t, event_count> addresses{
    0xa2d7, 0xa298, 0xa2d8, 0xa299, 0xa2d9, 0xa29a, 0xa2da, 0xa2ce, 0xa2ac};
constexpr std::array<uint32_t, event_count> raw_values{
    0x80001234, 0x80002345, 0x80003456, 0x80004567, 0x80005678,
    0x80006789, 0x8000789a, 0x80000800, 0x80007fff};
struct Record { uint64_t buffer; uint32_t offset, address, value; };
std::array<Record, event_count> records;
unsigned record_count, refuse_at;
int event_pipe = -1;
const render::PacketBuffer *publication_commands;
uintptr_t published_cursor;

struct Packets {
    ring::ItemsizePacket items;
    ring::OffsetPacket offsets;
    ring::ScalarPacket maximum, total;
    Packets()
    {
        items.header.set(itemsize_header); items.register_index.set(itemsize_index);
        offsets.header.set(offset_header); offsets.register_index.set(offset_index);
        maximum.header.set(scalar_header); maximum.register_index.set(maximum_index);
        total.header.set(scalar_header); total.register_index.set(total_index);
        for (std::size_t stream = 0; stream < stream_count; ++stream) {
            items.values[stream].set(raw_values[stream * events_per_stream]);
            if (stream + 1 < stream_count) offsets.values[stream].set(raw_values[stream * events_per_stream + 1]);
        }
        maximum.payload.set(raw_values[maximum_event]); total.payload.set(raw_values[total_event]);
    }
    void damage(std::size_t index)
    {
        const std::array<render::Field<uint32_t> *, shape_fields> fields = {
            &items.header, &items.register_index, &offsets.header, &offsets.register_index,
            &maximum.header, &maximum.register_index, &total.header, &total.register_index};
        fields[index]->set(fields[index]->get() ^ 1);
    }
    void observe(uint32_t offset = 0)
    {
        const render::PacketObservation call{reinterpret_cast<uintptr_t>(&items), offset, true};
        ring::observe_gs_ring_sizes(call, &items, &offsets, &maximum, &total);
    }
};

enum class Position { Admitted, BeforeBegin, BeyondFormat };

void writer(uint32_t offset, Position position = Position::Admitted)
{
    alignas(uintptr_t) std::array<uint8_t, 128> storage{};
    constexpr std::array<uint32_t, stream_count> sizes{1, 2, 4, 8};
    const uintptr_t cursor = reinterpret_cast<uintptr_t>(storage.data()); CHECK(cursor >= offset);
    render::PacketBuffer commands{};
    commands.begin = position == Position::BeforeBegin ? cursor + 1 :
        position == Position::BeyondFormat ? cursor - uintptr_t(UINT32_MAX) - 1 : cursor - offset;
    commands.limit = cursor + packet_bytes; commands.cursor = cursor;
    publication_commands = &commands; published_cursor = cursor + packet_bytes;
    bb_render_set_gs_ring_sizes(&commands, sizes.data(), initial_maximum);
    CHECK(render::packet_cursor(&commands) == published_cursor);
    publication_commands = nullptr;
    CHECK(record_count == event_count);
    for (std::size_t event = 0; event < event_count; ++event) {
        CHECK(records[event].buffer == cursor - offset && records[event].offset == offset);
        CHECK(records[event].address == addresses[event]);
    }
}

enum class Boundary { Refusal, Shape, Unavailable, PositionBefore, PositionBeyond };

void child_boundary(Boundary mode, unsigned selected)
{
    ++scenarios;
    std::array<int, pipe_ends> descriptors; CHECK(pipe(descriptors.data()) == 0);
    const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        CHECK(close(descriptors[0]) == 0); event_pipe = descriptors[1];
        const rlimit no_core{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &no_core) == 0);
        if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(setup_failure_exit);
        record_count = 0; refuse_at = mode == Boundary::Refusal ? selected : 1;
        publication_commands = nullptr;
        if (mode == Boundary::Refusal) writer(0);
        else if (mode == Boundary::Shape) { Packets packets; packets.damage(selected); packets.observe(); }
        else if (mode == Boundary::PositionBefore) writer(0, Position::BeforeBegin);
        else if (mode == Boundary::PositionBeyond) writer(0, Position::BeyondFormat);
        else {
            const render::PacketObservation absent{0, 0, false};
            ring::observe_gs_ring_sizes(absent, nullptr, nullptr, nullptr, nullptr);
        }
        std::_Exit(0);
    }
    CHECK(close(descriptors[1]) == 0); int status; CHECK(waitpid(child, &status, 0) == child);
    if (mode == Boundary::Unavailable) CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    else CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    std::array<char, event_count + 1> events{};
    const ssize_t count = read(descriptors[0], events.data(), events.size()); CHECK(close(descriptors[0]) == 0);
    CHECK(count == (mode == Boundary::Refusal ? ssize_t(selected) : 0));
}

void direct_values()
{
    ++scenarios; record_count = refuse_at = 0; publication_commands = nullptr;
    Packets packets; packets.observe(); CHECK(record_count == event_count);
    for (std::size_t event = 0; event < event_count; ++event) {
        CHECK(records[event].buffer == reinterpret_cast<uintptr_t>(&packets.items));
        CHECK(records[event].offset == 0 && records[event].address == addresses[event]);
        CHECK(records[event].value == raw_values[event]);
    }
}
}  // namespace

extern "C" int rt_render_record_draw_auto(uint64_t, uint32_t, uint32_t, char *, std::size_t) { return 0; }
extern "C" int rt_render_record_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value,
                                        char *error, std::size_t size)
{
    if (publication_commands) CHECK(render::packet_cursor(publication_commands) == published_cursor);
    CHECK(record_count < records.size());
    records[record_count++] = {buffer, offset, address, value};
    if (event_pipe >= 0) {
        const char event = 'r'; if (write(event_pipe, &event, 1) != 1) std::_Exit(pipe_failure_exit);
    }
    if (size) error[0] = 0;
    return refuse_at && record_count == refuse_at ? -1 : 0;
}

int main()
{
    CHECK(rt_render_observer_available());
    for (unsigned event = 1; event <= event_count; ++event) child_boundary(Boundary::Refusal, event);
    for (unsigned part = 0; part < shape_fields; ++part) child_boundary(Boundary::Shape, part);
    child_boundary(Boundary::Unavailable, 0);
    child_boundary(Boundary::PositionBefore, 0);
    child_boundary(Boundary::PositionBeyond, 0);
    direct_values();
    constexpr std::array<uint32_t, 2> endpoints{0, UINT32_MAX};
    for (uint32_t offset : endpoints) { ++scenarios; record_count = refuse_at = 0; writer(offset); }
    std::printf("GS-ring boundary observer: %u scenarios, %u parent checks, zero failures\n", scenarios, checks);
    return 0;
}
