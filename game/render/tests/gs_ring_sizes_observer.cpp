// SPDX-License-Identifier: GPL-2.0-or-later
// Owned writer/recorder integration and absent-host boundaries for GS ring sizes.
#include "../command_reservation.h"
#include "../render_observe.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_gs_ring_sizes(render::PacketBuffer *, const uint32_t *, uint32_t);

namespace {
unsigned scenarios, checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: game/render/tests/gs_ring_sizes_observer.cpp:%d: %s\n", \
                 __LINE__, #condition); std::exit(1); \
} } while (0)
constexpr std::size_t mapping_pages = 12, prefix_offset = 128;
constexpr std::size_t cursor_offset = 128, redirected_offset = 256, descriptor_offset = 128;
constexpr std::size_t stream_count = 4, packet_bytes = 68, prefix_page = 3, redirect_page = 2;
constexpr uint8_t packet_seed = 0xa5;
constexpr uint32_t initial_maximum = 3;
constexpr std::array<uint32_t, stream_count> initial_sizes{1, 2, 4, 8};
constexpr std::array<uint32_t, stream_count> callback_sizes{0x12, 0x123, 0x4123, 0x789a};
constexpr uint32_t requested_words = 17;
constexpr uint8_t accepted = 0x80;
#if !defined(PB_GS_RING_OBSERVER_ABSENT)
constexpr unsigned pointer_word_bits = 32;
constexpr int setup_failure_exit = 2;
constexpr uint32_t itemsize_mask = 0x7fff;
constexpr std::size_t hint_attempts = 8;
constexpr uintptr_t hint_high_word = 0x50, hint_stride = uintptr_t(1) << pointer_word_bits;
#endif

struct Storage {
    uint8_t *bytes{};
    std::size_t page{};
    Storage()
    {
        const long measured = sysconf(_SC_PAGESIZE); CHECK(measured > 0);
        page = std::size_t(measured);
#if defined(PB_GS_RING_OBSERVER_ABSENT)
        // This profile exercises no encoded live pointers and needs no special address.
        void *mapped = mmap(nullptr, page * mapping_pages, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mapped != MAP_FAILED) bytes = static_cast<uint8_t *>(mapped);
#else
        // Non-fixed hints never replace an existing mapping. Masked packet words
        // can encode pointers into this fresh owned mapping for live-cursor cases.
        for (std::size_t attempt = 0; attempt < hint_attempts; ++attempt) {
            const uintptr_t hint = (hint_high_word + attempt) * hint_stride;
            void *mapped = mmap(reinterpret_cast<void *>(hint), page * mapping_pages,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (mapped == MAP_FAILED) continue;
            const uintptr_t address = reinterpret_cast<uintptr_t>(mapped);
            if ((address & UINT32_MAX) == 0 && (address >> pointer_word_bits) <= itemsize_mask) {
                bytes = static_cast<uint8_t *>(mapped); break;
            }
            if (munmap(mapped, page * mapping_pages)) std::_Exit(setup_failure_exit);
        }
#endif
        CHECK(bytes != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(bytes) % page == 0);
        std::memset(bytes, packet_seed, page * mapping_pages);
    }
    ~Storage() { CHECK(munmap(bytes, page * mapping_pages) == 0); }
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(bytes); }
};

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

struct Fixture;
Fixture *active_fixture;
struct Fixture {
    Storage storage;
    uintptr_t prefix = storage.base() + storage.page * prefix_page + prefix_offset;
    uintptr_t descriptor = storage.base() + descriptor_offset;
    uint32_t maximum = initial_maximum;
    uint8_t callback_result = accepted;
    unsigned callback_count{};
    bool redirect{}, change_descriptor{}, null_descriptor{};
    render::PacketBuffer *commands() const { return reinterpret_cast<render::PacketBuffer *>(prefix); }
    uintptr_t begin() const { return storage.base() + storage.page; }
    uintptr_t replacement() const { return storage.base() + storage.page * redirect_page; }
    void values(const std::array<uint32_t, stream_count> &input)
    {
        std::memcpy(reinterpret_cast<void *>(descriptor), input.data(), sizeof(input));
    }
    void layout(uintptr_t begin, uintptr_t cursor)
    {
        field(commands(), offsetof(render::PacketBuffer, begin), begin);
        field(commands(), offsetof(render::PacketBuffer, cursor), cursor);
        field(commands(), offsetof(render::PacketBuffer, limit), cursor + packet_bytes);
    }
    Fixture() { layout(begin(), begin() + cursor_offset); values(initial_sizes); }
    static uint8_t reserve(render::PacketBuffer *commands, uint32_t words, uintptr_t argument)
    {
        CHECK(active_fixture && argument == reinterpret_cast<uintptr_t>(active_fixture));
        auto &fixture = *active_fixture;
        CHECK(commands == fixture.commands() && words == requested_words);
        CHECK(++fixture.callback_count == 1);
        if (fixture.change_descriptor) fixture.values(callback_sizes);
        if (fixture.redirect) {
            field(commands, offsetof(render::PacketBuffer, begin), fixture.replacement());
            field(commands, offsetof(render::PacketBuffer, cursor), fixture.replacement() + redirected_offset);
        }
        field(commands, offsetof(render::PacketBuffer, callback_argument), uintptr_t(0));
        return fixture.callback_result;
    }
    void callback()
    {
        field(commands(), offsetof(render::PacketBuffer, limit), render::packet_cursor(commands()));
        field(commands(), offsetof(render::PacketBuffer, space_callback), render::PacketSpaceCallback(reserve));
        field(commands(), offsetof(render::PacketBuffer, callback_argument), reinterpret_cast<uintptr_t>(this));
    }
    void emit()
    {
        active_fixture = this;
        bb_render_set_gs_ring_sizes(commands(), null_descriptor ? nullptr : reinterpret_cast<const uint32_t *>(descriptor), maximum);
        active_fixture = nullptr;
    }
};

void headless()
{
    if (rt_render_observer_available()) return;
    ++scenarios;
    const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        const rlimit no_core{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &no_core) == 0);
        const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
        constexpr std::size_t guard_pages = 2;
        auto *mapped = static_cast<uint8_t *>(mmap(nullptr, std::size_t(page) * guard_pages,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        CHECK(mapped != MAP_FAILED); CHECK(mprotect(mapped, std::size_t(page), PROT_NONE) == 0);
        auto *commands = reinterpret_cast<render::PacketBuffer *>(mapped + page - sizeof(uintptr_t));
        Fixture fixture; const uintptr_t cursor = fixture.begin() + cursor_offset;
        field(commands, offsetof(render::PacketBuffer, cursor), cursor);
        field(commands, offsetof(render::PacketBuffer, limit), cursor + packet_bytes);
        bb_render_set_gs_ring_sizes(commands, reinterpret_cast<const uint32_t *>(fixture.descriptor), fixture.maximum);
        CHECK(render::packet_cursor(commands) == cursor + packet_bytes);
        CHECK(munmap(mapped, std::size_t(page) * guard_pages) == 0);
        std::_Exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ++scenarios; Fixture redirected; redirected.redirect = true; redirected.callback(); redirected.emit();
    CHECK(redirected.callback_count == 1);
    CHECK(render::packet_cursor(redirected.commands()) == redirected.replacement() + redirected_offset + packet_bytes);
    // MAX/header publication is damaged here, but an absent observer must not validate it.
    ++scenarios; Fixture aliased;
    constexpr uintptr_t tail_alias_distance = 28;
    aliased.layout(aliased.begin(), aliased.prefix - tail_alias_distance); aliased.emit();
    CHECK(render::packet_cursor(aliased.commands()) == aliased.prefix + sizeof(render::PacketBuffer));
}

#if !defined(PB_GS_RING_OBSERVER_ABSENT)
constexpr std::size_t packet_count = 4, event_count = 9, tail_bytes = 44;
constexpr uint32_t max_vertices_mask = 0x7ff, untouched_packet_word = 0xa5a5a5a5;
constexpr std::array<std::size_t, packet_count> ordinary_packet_offsets{0, 24, 44, 56};
constexpr std::array<uint32_t, event_count> event_registers{
    0xa2d7, 0xa298, 0xa2d8, 0xa299, 0xa2d9, 0xa29a, 0xa2da, 0xa2ce, 0xa2ac};

void enabled() { CHECK(rt_render_observer_available()); rt_render_forget(); }

void recorded(uintptr_t begin, uintptr_t cursor, const std::array<uintptr_t, packet_count> &packets)
{
    std::size_t count;
    const auto *calls = rt_render_submitted(begin, UINT32_MAX, &count);
    CHECK(calls && count == event_count);
    const std::array<uintptr_t, event_count> payloads = {
        packets[0] + 8, packets[1] + 8, packets[0] + 12, packets[1] + 12,
        packets[0] + 16, packets[1] + 16, packets[0] + 20, packets[2] + 8, packets[3] + 8};
    for (std::size_t event = 0; event < event_count; ++event) {
        CHECK(calls[event].kind == RT_CALL_REGISTER && calls[event].u.reg.address == event_registers[event]);
        CHECK(calls[event].offset == cursor - begin);
        CHECK(calls[event].u.reg.value == render::Field<uint32_t>::read(reinterpret_cast<const void *>(payloads[event])));
    }
}

std::array<uintptr_t, packet_count> ordinary_packets(uintptr_t cursor)
{
    std::array<uintptr_t, packet_count> packets;
    for (std::size_t packet = 0; packet < packet_count; ++packet) packets[packet] = cursor + ordinary_packet_offsets[packet];
    return packets;
}

void direct()
{
    constexpr std::array<std::array<uint32_t, stream_count>, 6> inputs{{
        {0, 0, 0, 0}, {1, 2, 4, 8}, {0x7fff, 1, 0, 0}, {0x8000, 0x10000, 0x80000000, UINT32_MAX},
        {0x7f, 0x80, 0xff, 0x100}, {0x1234, 0x2345, 0x4567, 0x6789}}};
    constexpr std::array<uint32_t, 6> maximums{0, 1, 2, 0x7ff, 0x800, UINT32_MAX};
    for (const auto &values : inputs) {
        for (uint32_t maximum : maximums) {
            ++scenarios; enabled(); Fixture fixture; fixture.values(values); fixture.maximum = maximum;
            const uintptr_t cursor = fixture.begin() + cursor_offset; fixture.emit();
            recorded(fixture.begin(), cursor, ordinary_packets(cursor));
            const uint32_t a = values[0], b = a + values[1], c = b + values[2], sum = c + values[3];
            const std::array<uint32_t, event_count> expected = {
                a & itemsize_mask, a * maximum & itemsize_mask, values[1] & itemsize_mask, b * maximum & itemsize_mask,
                values[2] & itemsize_mask, c * maximum & itemsize_mask, values[3] & itemsize_mask,
                maximum & max_vertices_mask, sum * maximum & itemsize_mask};
            std::size_t count; const auto *calls = rt_render_submitted(fixture.begin(), UINT32_MAX, &count);
            CHECK(count == event_count);
            for (std::size_t event = 0; event < event_count; ++event) CHECK(calls[event].u.reg.value == expected[event]);
            CHECK(render::packet_cursor(fixture.commands()) == cursor + packet_bytes);
            CHECK(fixture.callback_count == 0);
        }
    }
    constexpr std::array<std::size_t, 3> displacements{0, 1, 3};
    for (std::size_t displacement : displacements) {
        ++scenarios; enabled(); Fixture fixture; fixture.prefix += displacement; fixture.descriptor += displacement;
        const uintptr_t cursor = fixture.begin() + cursor_offset + displacement;
        fixture.layout(fixture.begin(), cursor); fixture.values(initial_sizes); fixture.emit();
        recorded(fixture.begin(), cursor, ordinary_packets(cursor));
    }
}

void callbacks()
{
    constexpr std::array<uint8_t, 5> results{0, 1, 2, accepted, 0xff};
    for (uint8_t result : results) {
        ++scenarios; enabled(); Fixture fixture; fixture.redirect = true; fixture.change_descriptor = true;
        fixture.callback_result = result; fixture.callback(); fixture.emit(); CHECK(fixture.callback_count == 1);
        const uintptr_t cursor = fixture.replacement() + redirected_offset;
        if (result) recorded(fixture.replacement(), cursor, ordinary_packets(cursor));
        else {
            std::size_t count; rt_render_submitted(fixture.replacement(), UINT32_MAX, &count); CHECK(count == 0);
            CHECK(render::Field<uint32_t>::read(reinterpret_cast<const void *>(cursor)) == untouched_packet_word);
        }
    }
    ++scenarios; enabled(); Fixture fixture; fixture.null_descriptor = true;
    fixture.callback_result = 0; fixture.callback();
    field(fixture.commands(), offsetof(render::PacketBuffer, begin), render::packet_cursor(fixture.commands()) + 1);
    fixture.emit(); CHECK(fixture.callback_count == 1);
    std::size_t count; rt_render_submitted(fixture.begin(), UINT32_MAX, &count); CHECK(count == 0);
}

void aliases()
{
    // Pointers are encoded by masked data, while final publication can cross bit15.
    for (bool second_reload : {false, true}) {
        ++scenarios; enabled(); Fixture fixture;
        constexpr uintptr_t encoded_cursor_offset = 0x7ff0;
        const uintptr_t target = fixture.storage.base() + encoded_cursor_offset;
        const uint32_t low = uint32_t(target), high = uint32_t(target >> 32);
        CHECK(low <= itemsize_mask && high <= itemsize_mask);
        const uintptr_t cursor = second_reload ? fixture.prefix - sizeof(uintptr_t) * 2 : fixture.prefix + sizeof(uintptr_t);
        fixture.layout(fixture.begin(), cursor);
        const std::array<uint32_t, stream_count> alias_sizes = second_reload ?
            std::array<uint32_t, stream_count>{low, (high - low) & itemsize_mask, 5, 7} :
            std::array<uint32_t, stream_count>{low, high, 3, 7};
        fixture.values(alias_sizes);
        fixture.maximum = second_reload ? 1 : initial_maximum; fixture.emit();
        const uintptr_t offset_cursor = second_reload ? fixture.prefix + sizeof(uintptr_t) : target + ordinary_packet_offsets[1];
        const uintptr_t tail_cursor = second_reload ? target : offset_cursor;
        const std::array<uintptr_t, packet_count> packets = {
            cursor, offset_cursor, tail_cursor + 20, tail_cursor + 32};
        recorded(fixture.begin(), cursor, packets);
        CHECK(render::packet_cursor(fixture.commands()) == tail_cursor + tail_bytes);
        CHECK(uint32_t(tail_cursor + tail_bytes) > itemsize_mask);
    }
    ++scenarios; enabled(); Fixture fixture;
    constexpr std::size_t tail_prefix_page = 9;
    constexpr uintptr_t total_alias_distance = 48;
    fixture.prefix = fixture.storage.base() + fixture.storage.page * tail_prefix_page + prefix_offset;
    const uintptr_t cursor = fixture.prefix - total_alias_distance;
    fixture.layout(fixture.begin(), cursor); fixture.emit();
    recorded(fixture.begin(), cursor, ordinary_packets(cursor));
    const uint32_t total = render::Field<uint32_t>::read(reinterpret_cast<const void *>(fixture.prefix + 16));
    CHECK(total == uint32_t(fixture.prefix + packet_bytes - total_alias_distance)); CHECK(total > itemsize_mask);
}
#endif
}  // namespace

int main()
{
    headless();
#if !defined(PB_GS_RING_OBSERVER_ABSENT)
    direct(); callbacks(); aliases(); rt_render_forget();
#endif
    std::printf("GS-ring observer: %u scenarios, %u parent checks, zero failures\n", scenarios, checks);
    return 0;
}
