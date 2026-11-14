// SPDX-License-Identifier: GPL-2.0-or-later
// Canonical recorder and absent-host checks on owned command storage.
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

extern "C" void bb_render_set_gs_mode(render::PacketBuffer *, uint32_t, uint32_t);

namespace {
unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (0)
constexpr std::size_t storage_bytes = 512, prefix_offset = 64, initial_offset = 64, redirected_offset = 128;
constexpr uint8_t accepted = 0x80, packet_fill = 0x6b, redirected_fill = 0x7c;
constexpr uint32_t cut_high_bit = 2, cut_value_mask = 3;
constexpr std::size_t packet_bytes = sizeof(render::PreparationCommandPacket);
constexpr uint32_t packet_words = packet_bytes / render::packet_word_bytes;
constexpr uint32_t reset_words = 2 * packet_words;
constexpr std::size_t reset_bytes = 2 * packet_bytes;
constexpr unsigned mapped_pages = 2;
// A raw nonzero mode that packing clears still takes the one-packet path.
constexpr std::array<uint32_t, 2> raw_branch_modes{0, 0x10};

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

struct Fixture {
    alignas(uintptr_t) std::array<uint8_t, storage_bytes> prefix{}, packet{}, replacement{};
    uint32_t mode = 1, cut = cut_high_bit;
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
        packet.fill(packet_fill); replacement.fill(redirected_fill);
        storage(begin(), begin() + initial_offset, begin() + storage_bytes);
    }
    void emit() { bb_render_set_gs_mode(commands(), mode, cut); }
};

uint8_t reserve(render::PacketBuffer *commands, uint32_t requested, uintptr_t argument)
{
    auto &fixture = *reinterpret_cast<Fixture *>(argument);
    CHECK(requested == (fixture.mode == 0 ? reset_words : packet_words));
    ++fixture.callback_count;
    CHECK(fixture.callback_count == 1);
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
    const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
    auto *mapping = static_cast<uint8_t *>(mmap(nullptr, std::size_t(page) * mapped_pages,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    CHECK(mapping != MAP_FAILED); CHECK(mprotect(mapping, std::size_t(page), PROT_NONE) == 0);
    auto *commands = reinterpret_cast<render::PacketBuffer *>(mapping + page - sizeof(uintptr_t));
    for (uint32_t mode : raw_branch_modes) {
        ++scenarios; Fixture fixture;
        field(commands, offsetof(render::PacketBuffer, cursor), fixture.begin());
        field(commands, offsetof(render::PacketBuffer, limit), fixture.begin() + storage_bytes);
        bb_render_set_gs_mode(commands, mode, cut_value_mask);
        CHECK(render::packet_cursor(commands) == fixture.begin() + (mode == 0 ? reset_bytes : packet_bytes));
    }
    CHECK(munmap(mapping, std::size_t(page) * mapped_pages) == 0);
    for (uint32_t mode : raw_branch_modes) {
        ++scenarios; Fixture fixture; fixture.mode = mode; fixture.redirect = true;
        callback(fixture); fixture.emit();
        CHECK(fixture.callback_count == 1);
        CHECK(render::packet_cursor(fixture.commands()) == reinterpret_cast<uintptr_t>(fixture.replacement.data()) +
            redirected_offset + (mode == 0 ? reset_bytes : packet_bytes));
    }
    // With no host, an aliased final packet may have an unrecordable shape.
    // The original still completes; observer validation must stay disabled.
    ++scenarios; Fixture aliased; aliased.mode = 0;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(aliased.prefix.data());
    const uintptr_t cursor = reinterpret_cast<uintptr_t>(aliased.commands()) + render::packet_word_bytes;
    aliased.storage(begin, cursor, begin + storage_bytes); aliased.emit();
    CHECK(render::packet_cursor(aliased.commands()) == cursor + reset_bytes);
}

#if !defined(PB_GS_OBSERVER_ABSENT)
constexpr std::size_t reset_packet_count = 2;
constexpr uint32_t gs_mode_address = 0xa290, streamout_address = 0xa2e5;
constexpr std::array<uint32_t, 5> direct_modes{0, 1, 0x10, 0x30, UINT32_MAX};
constexpr std::array<uint32_t, 5> cut_inputs{0, 1, 2, 3, UINT32_MAX};
constexpr std::array<uint8_t, 3> callback_results{0, 2, accepted};
constexpr uint32_t alias_initial_payload = 0x31;
void enabled() { CHECK(rt_render_observer_available()); rt_render_forget(); }

void recorded(uintptr_t begin, uintptr_t cursor, bool reset_streamout)
{
    std::size_t count;
    const auto *calls = rt_render_submitted(begin, UINT32_MAX, &count);
    CHECK(calls && count == (reset_streamout ? reset_packet_count : 1u));
    const auto *packets = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
    constexpr std::array<uint32_t, 2> addresses{gs_mode_address, streamout_address};
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(calls[i].kind == RT_CALL_REGISTER && calls[i].u.reg.address == addresses[i]);
        CHECK(calls[i].u.reg.value == packets[i].payload.get());
        // Both events retain the original observer's one admitted call position.
        CHECK(calls[i].offset == cursor - begin);
    }
}

void direct()
{
    for (uint32_t mode : direct_modes) {
        for (uint32_t cut : cut_inputs) {
            ++scenarios; enabled(); Fixture fixture; fixture.mode = mode; fixture.cut = cut; fixture.emit();
            recorded(fixture.begin(), fixture.begin() + initial_offset, mode == 0);
            CHECK(render::packet_cursor(fixture.commands()) == fixture.begin() + initial_offset +
                (mode == 0 ? reset_bytes : packet_bytes));
        }
    }
}

void callbacks()
{
    for (uint32_t mode : raw_branch_modes) {
        for (uint8_t result : callback_results) {
            ++scenarios; enabled(); Fixture fixture; fixture.mode = mode; fixture.redirect = true;
            fixture.callback_result = result; callback(fixture); const auto untouched = fixture.replacement;
            fixture.emit(); CHECK(fixture.callback_count == 1);
            const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.replacement.data());
            if (result) recorded(begin, begin + redirected_offset, mode == 0);
            else {
                std::size_t count; rt_render_submitted(begin, UINT32_MAX, &count);
                CHECK(count == 0 && fixture.replacement == untouched);
            }
        }
    }
}

void final_publication()
{
    for (uint32_t mode : {0u, 1u}) {
        ++scenarios; enabled(); Fixture fixture; fixture.mode = mode; fixture.cut = cut_value_mask;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.prefix.data());
        const uintptr_t commands = reinterpret_cast<uintptr_t>(fixture.commands());
        // Nonzero aliases the first payload; zero aliases the second payload.
        const uintptr_t cursor = mode == 0 ? commands - 4 : commands + 8;
        fixture.storage(begin, cursor, begin + storage_bytes); fixture.emit();
        recorded(begin, cursor, mode == 0);
        const auto *packet = reinterpret_cast<const render::PreparationCommandPacket *>(cursor);
        const uint32_t final_value = (mode == 0 ? packet[1] : packet[0]).payload.get();
        CHECK(final_value == static_cast<uint32_t>(cursor + (mode == 0 ? reset_bytes : packet_bytes)));
        CHECK(final_value != (mode == 0 ? 0u : alias_initial_payload));
    }
}

void bad_offset(bool too_large)
{
    Fixture fixture;
    const uintptr_t cursor = render::packet_cursor(fixture.commands());
    field(fixture.commands(), offsetof(render::PacketBuffer, begin),
          too_large ? cursor - uintptr_t(UINT32_MAX) - 1 : cursor + 1);
    fixture.emit();
}

void invalid_offsets()
{
    constexpr int redirection_failed = 2;
    for (bool too_large : {false, true}) {
        ++scenarios; const pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            const rlimit no_core{0, 0}; setrlimit(RLIMIT_CORE, &no_core);
            if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(redirection_failed);
            bad_offset(too_large); std::_Exit(0);
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
#if !defined(PB_GS_OBSERVER_ABSENT)
    direct(); callbacks(); final_publication(); invalid_offsets(); rt_render_forget();
#endif
    std::printf("GS-mode observer: %u scenarios, %u checks, zero failures\n", scenarios, checks);
    return 0;
}
