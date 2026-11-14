// SPDX-License-Identifier: GPL-2.0-or-later
// Owned storage checks for the canonical ESGS register observer and absent hosts.
#include "../packet_observation.h"
#include "../present_register_packets.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_esgs_ring_itemsize(render::PacketBuffer *, uint32_t);

namespace {
unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (0)
constexpr std::size_t storage_bytes = 512, prefix_offset = 64, initial_offset = 64, redirected_offset = 128;
constexpr std::size_t packet_bytes = sizeof(render::PreparationCommandPacket), mapped_pages = 2;
constexpr uint32_t packet_words = packet_bytes / render::packet_word_bytes;
constexpr uint8_t accepted = 0x80, packet_seed = 0x6b, replacement_seed = 0x7c;
constexpr uint32_t itemsize_sample = 0x4123;
constexpr std::array<std::size_t, 3> malformed_packet_offsets{
    offsetof(render::PacketBuffer, cursor) - render::packet_word_bytes,
    offsetof(render::PacketBuffer, cursor),
    offsetof(render::PacketBuffer, cursor) + render::packet_word_bytes};

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

struct Fixture {
    alignas(uintptr_t) std::array<uint8_t, storage_bytes> prefix{}, packet{}, replacement{};
    uint32_t itemsize = itemsize_sample;
    unsigned callback_count{};
    uint8_t callback_result = accepted;
    bool redirect{};
    render::PacketBuffer *commands() { return reinterpret_cast<render::PacketBuffer *>(prefix.data() + prefix_offset); }
    uintptr_t begin() const { return reinterpret_cast<uintptr_t>(packet.data()); }
    void storage(uintptr_t begin, uintptr_t cursor, uintptr_t limit)
    {
        field(commands(), offsetof(render::PacketBuffer, begin), begin);
        field(commands(), offsetof(render::PacketBuffer, cursor), cursor);
        field(commands(), offsetof(render::PacketBuffer, limit), limit);
    }
    Fixture()
    {
        packet.fill(packet_seed); replacement.fill(replacement_seed);
        storage(begin(), begin() + initial_offset, begin() + storage_bytes);
    }
    void emit() { bb_render_set_esgs_ring_itemsize(commands(), itemsize); }
};

uint8_t reserve(render::PacketBuffer *commands, uint32_t requested, uintptr_t argument)
{
    auto &fixture = *reinterpret_cast<Fixture *>(argument);
    CHECK(requested == packet_words); ++fixture.callback_count; CHECK(fixture.callback_count == 1);
    if (fixture.redirect) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.replacement.data());
        field(commands, offsetof(render::PacketBuffer, begin), begin);
        field(commands, offsetof(render::PacketBuffer, cursor), begin + redirected_offset);
    }
    return fixture.callback_result;
}

void callback(Fixture &fixture)
{
    field(fixture.commands(), offsetof(render::PacketBuffer, limit), render::packet_cursor(fixture.commands()));
    field(fixture.commands(), offsetof(render::PacketBuffer, space_callback), render::PacketSpaceCallback(reserve));
    field(fixture.commands(), offsetof(render::PacketBuffer, callback_argument), reinterpret_cast<uintptr_t>(&fixture));
}

void headless()
{
    if (rt_render_observer_available()) return;
    // Begin lies in an unreadable page; all fields the original uses remain readable.
    ++scenarios;
    const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        const rlimit no_core{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &no_core) == 0);
        const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
        auto *mapping = static_cast<uint8_t *>(mmap(nullptr, std::size_t(page) * mapped_pages,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        CHECK(mapping != MAP_FAILED); CHECK(mprotect(mapping, std::size_t(page), PROT_NONE) == 0);
        auto *commands = reinterpret_cast<render::PacketBuffer *>(mapping + page - sizeof(uintptr_t));
        Fixture fixture;
        field(commands, offsetof(render::PacketBuffer, cursor), fixture.begin());
        field(commands, offsetof(render::PacketBuffer, limit), fixture.begin() + storage_bytes);
        bb_render_set_esgs_ring_itemsize(commands, fixture.itemsize);
        CHECK(render::packet_cursor(commands) == fixture.begin() + packet_bytes);
        CHECK(munmap(mapping, std::size_t(page) * mapped_pages) == 0);
        std::_Exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ++scenarios; Fixture redirected; redirected.redirect = true; callback(redirected); redirected.emit();
    CHECK(redirected.callback_count == 1);
    CHECK(render::packet_cursor(redirected.commands()) == reinterpret_cast<uintptr_t>(redirected.replacement.data()) +
          redirected_offset + packet_bytes);
    // Cursor publication may damage a header or index; absent hosts must not validate it.
    for (std::size_t offset : malformed_packet_offsets) {
        ++scenarios; Fixture aliased; const uintptr_t cursor = reinterpret_cast<uintptr_t>(aliased.commands()) + offset;
        aliased.storage(reinterpret_cast<uintptr_t>(aliased.prefix.data()), cursor,
                        reinterpret_cast<uintptr_t>(aliased.prefix.data()) + storage_bytes);
        aliased.emit(); CHECK(render::packet_cursor(aliased.commands()) == cursor + packet_bytes);
    }
}

#if !defined(PB_ESGS_OBSERVER_ABSENT)
constexpr uint32_t register_address = 0xa2ab, itemsize_field_mask = 0x7fff;
constexpr int setup_failure_exit = 2;
constexpr std::array<std::size_t, 2> prefix_packet_offsets{0, offsetof(render::PreparationCommandPacket, payload)};
constexpr std::array<uint8_t, 5> callback_results{0, 1, 2, accepted, 0xff};
constexpr std::array<uint32_t, 14> inputs{0, 1, 2, 0x7f, 0x80, 0x100, 0x3fff, 0x4000,
                                       0x7fff, 0x8000, 0x8001, 0xffff, 0x10000, UINT32_MAX};
void enabled() { CHECK(rt_render_observer_available()); rt_render_forget(); }

void recorded(uintptr_t begin, uintptr_t cursor)
{
    std::size_t count;
    const auto *calls = rt_render_submitted(begin, UINT32_MAX, &count);
    CHECK(calls && count == 1);
    const auto *packet = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
    CHECK(calls[0].kind == RT_CALL_REGISTER && calls[0].u.reg.address == register_address);
    CHECK(calls[0].u.reg.value == packet->payload.get());
    CHECK(calls[0].offset == cursor - begin);
}

void direct()
{
    for (uint32_t value : inputs) {
        for (std::size_t unaligned : {std::size_t(0), std::size_t(1)}) {
            ++scenarios; enabled(); Fixture fixture; fixture.itemsize = value;
            const uintptr_t cursor = fixture.begin() + initial_offset + unaligned;
            fixture.storage(fixture.begin(), cursor, fixture.begin() + storage_bytes); fixture.emit();
            recorded(fixture.begin(), cursor); CHECK(render::packet_cursor(fixture.commands()) == cursor + packet_bytes);
            CHECK(fixture.callback_count == 0);
        }
    }
}

void callbacks()
{
    for (uint8_t result : callback_results) {
        ++scenarios; enabled(); Fixture fixture; fixture.redirect = true; fixture.callback_result = result;
        callback(fixture); const auto untouched = fixture.replacement; fixture.emit(); CHECK(fixture.callback_count == 1);
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.replacement.data());
        if (result) recorded(begin, begin + redirected_offset);
        else {
            std::size_t count; rt_render_submitted(begin, UINT32_MAX, &count);
            CHECK(count == 0 && fixture.replacement == untouched);
        }
    }
    // A declined reservation must not inspect an invalid observer position.
    ++scenarios; enabled(); Fixture refused; refused.callback_result = 0; callback(refused);
    field(refused.commands(), offsetof(render::PacketBuffer, begin), render::packet_cursor(refused.commands()) + 1);
    refused.emit(); CHECK(refused.callback_count == 1);
    std::size_t count; rt_render_submitted(refused.begin(), UINT32_MAX, &count); CHECK(count == 0);
}

void aliases()
{
    for (std::size_t offset : prefix_packet_offsets) {
        ++scenarios; enabled(); Fixture fixture;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.prefix.data());
        const uintptr_t cursor = reinterpret_cast<uintptr_t>(fixture.commands()) + offset;
        fixture.storage(begin, cursor, begin + storage_bytes);
        fixture.itemsize = (static_cast<uint32_t>(cursor + packet_bytes) & itemsize_field_mask) ^ 1;
        fixture.emit(); recorded(begin, cursor);
        CHECK(render::packet_cursor(fixture.commands()) == cursor + packet_bytes);
        if (offset == offsetof(render::PreparationCommandPacket, payload)) {
            const auto *packet = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
            CHECK(packet->payload.get() == static_cast<uint32_t>(cursor + packet_bytes));
            CHECK(packet->payload.get() != fixture.itemsize);
        }
    }
}

void invalid_offsets()
{
    for (bool too_large : {false, true}) {
        ++scenarios; const pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            const rlimit no_core{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &no_core) == 0);
            if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(setup_failure_exit);
            Fixture fixture; const uintptr_t cursor = render::packet_cursor(fixture.commands());
            field(fixture.commands(), offsetof(render::PacketBuffer, begin),
                  too_large ? cursor - uintptr_t(UINT32_MAX) - 1 : cursor + 1);
            fixture.emit(); std::_Exit(0);
        }
        int status; CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
}
#endif
}  // namespace

int main()
{
    headless();
#if !defined(PB_ESGS_OBSERVER_ABSENT)
    direct(); callbacks(); aliases(); invalid_offsets(); rt_render_forget();
#endif
    // Forked guard/offset children also assert internally; this counter names parent checks only.
    std::printf("ESGS-itemsize observer: %u scenarios, %u parent checks, zero failures\n", scenarios, checks);
    return 0;
}
