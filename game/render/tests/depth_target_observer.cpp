// SPDX-License-Identifier: GPL-2.0-or-later
// Real depth packets and canonical recorders on owned command and descriptor storage.
#include "../depth_target_observation.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_depth_render_target(render::PacketBuffer *, const void *);

namespace {
unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (0)
constexpr std::size_t storage_bytes = 512, target_words = 13;
constexpr uint32_t initial_offset = 64, redirected_offset = 128, full_words = 24;
constexpr uint8_t accepted = 0x80, packet_fill = 0x6b;
constexpr std::size_t z_read_base_word = 2, stencil_read_base_word = 3;
constexpr std::array<uint32_t, target_words> target{
    0x01020304, 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00, 0x76543210,
    0x80000001, 0xfedcba98, 0x7fc12345, 0x80000000, 0xffc54321, 0x40000000, 0x01234567};

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

struct Fixture {
    alignas(uintptr_t) std::array<uint8_t, storage_bytes> prefix{}, packet{}, replacement{};
    std::array<uint32_t, target_words> descriptor = target;
    uint32_t expected_request = full_words;
    uint8_t callback_result = accepted;
    bool redirect{}, invalidate{}, validate{}, change_words{};
    render::PacketBuffer *commands() { return reinterpret_cast<render::PacketBuffer *>(prefix.data()); }
    uintptr_t begin() const { return reinterpret_cast<uintptr_t>(packet.data()); }
    void storage(uintptr_t begin, uintptr_t cursor, uintptr_t limit)
    {
        field(commands(), offsetof(render::PacketBuffer, begin), begin);
        field(commands(), offsetof(render::PacketBuffer, cursor), cursor);
        field(commands(), offsetof(render::PacketBuffer, limit), limit);
    }
    Fixture()
    {
        packet.fill(packet_fill); replacement.fill(packet_fill);
        storage(begin(), begin() + initial_offset, begin() + storage_bytes);
    }
    void emit() { bb_render_set_depth_render_target(commands(), descriptor.data()); }
};

uint8_t reserve(render::PacketBuffer *commands, uint32_t requested, uintptr_t argument)
{
    auto &fixture = *reinterpret_cast<Fixture *>(argument); CHECK(requested == fixture.expected_request);
    if (fixture.redirect) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.replacement.data());
        field(commands, offsetof(render::PacketBuffer, begin), begin);
        field(commands, offsetof(render::PacketBuffer, cursor), begin + redirected_offset);
    }
    if (fixture.change_words) {
        for (auto &word : fixture.descriptor) word ^= UINT32_MAX;
    }
    if (fixture.invalidate) {
        fixture.descriptor[z_read_base_word] = fixture.descriptor[stencil_read_base_word] = 0;
    }
    if (fixture.validate) fixture.descriptor[z_read_base_word] = target[z_read_base_word];
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
    ++scenarios;
    const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
    constexpr unsigned mapped_pages = 2;
    auto *mapping = static_cast<uint8_t *>(mmap(nullptr, std::size_t(page) * mapped_pages,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    CHECK(mapping != MAP_FAILED); CHECK(mprotect(mapping, std::size_t(page), PROT_NONE) == 0);
    auto *commands = reinterpret_cast<render::PacketBuffer *>(mapping + page - sizeof(uintptr_t));
    Fixture fixture;
    field(commands, offsetof(render::PacketBuffer, cursor), fixture.begin());
    field(commands, offsetof(render::PacketBuffer, limit), fixture.begin() + storage_bytes);
    bb_render_set_depth_render_target(commands, fixture.descriptor.data());
    CHECK(render::packet_cursor(commands) == fixture.begin() + sizeof(render::DepthTargetPacket));
    bb_render_set_depth_render_target(commands, nullptr);
    CHECK(render::packet_cursor(commands) == fixture.begin() + sizeof(render::DepthTargetPacket) +
        sizeof(render::ClearDepthTargetPacket));
    render::record_depth_target_block({}, reinterpret_cast<const render::DepthTargetPacket *>(mapping), nullptr);
    render::record_depth_target_clear({}, reinterpret_cast<const render::ClearDepthTargetPacket *>(mapping));
    CHECK(munmap(mapping, std::size_t(page) * mapped_pages) == 0);
    ++scenarios; Fixture redirected; redirected.redirect = true; callback(redirected); redirected.emit();
    CHECK(render::packet_cursor(redirected.commands()) == reinterpret_cast<uintptr_t>(redirected.replacement.data()) +
        redirected_offset + sizeof(render::DepthTargetPacket));
}

#if !defined(PB_DEPTH_OBSERVER_ABSENT)
void enabled() { CHECK(rt_render_observer_available()); rt_render_forget(); }

void full_record(uintptr_t begin, const render::DepthTargetPacket *packet, const render::DepthTargetPacket *trailing)
{
    std::size_t count;
    const auto *calls = rt_render_submitted(begin, UINT32_MAX, &count);
    constexpr unsigned register_count = 12, block_values = 8;
    CHECK(count == register_count && calls);
    std::array<uint32_t, block_values> values; std::memcpy(values.data(), &packet->z_and_stencil_info, sizeof(values));
    for (unsigned i = 0; i < block_values; ++i) {
        CHECK(calls[i].kind == RT_CALL_REGISTER);
        CHECK(calls[i].u.reg.address == RT_CONTEXT_REGISTERS_FIRST + packet->index.get() + i);
        CHECK(calls[i].u.reg.value == values[i]);
        CHECK(calls[i].offset == reinterpret_cast<uintptr_t>(packet) - begin);
    }
    constexpr unsigned singleton_packets = 4;
    const std::array<const render::PreparationCommandPacket *, singleton_packets> singles{
        &trailing->depth_info, &trailing->depth_view, &trailing->htile_data_base, &trailing->htile_surface};
    constexpr std::array<uint32_t, singleton_packets> addresses{0xa00f, 0xa002, 0xa005, 0xa2af};
    for (unsigned i = 0; i < singles.size(); ++i) {
        const auto &call = calls[block_values + i];
        CHECK(call.kind == RT_CALL_REGISTER && call.u.reg.address == addresses[i]);
        CHECK(call.u.reg.value == singles[i]->payload.get());
        CHECK(call.offset == reinterpret_cast<uintptr_t>(singles[i]) - begin);
    }
}

void clear_record(uintptr_t begin, const render::ClearDepthTargetPacket *packet)
{
    std::size_t count; const auto *calls = rt_render_submitted(begin, UINT32_MAX, &count);
    constexpr unsigned register_count = 2;
    CHECK(count == register_count && calls);
    constexpr std::array<uint32_t, register_count> addresses{0xa010, 0xa011};
    for (unsigned i = 0; i < register_count; ++i) {
        CHECK(calls[i].kind == RT_CALL_REGISTER && calls[i].u.reg.address == addresses[i]);
        CHECK(calls[i].u.reg.value == 0);
        CHECK(calls[i].offset == reinterpret_cast<uintptr_t>(packet) - begin + i * sizeof(render::PreparationCommandPacket));
    }
}

void direct_and_clear()
{
    enum class Binding { Both, DepthOnly, StencilOnly };
    constexpr Binding bindings[]{Binding::Both, Binding::DepthOnly, Binding::StencilOnly};
    for (Binding mode : bindings) {
        ++scenarios; enabled(); Fixture fixture;
        if (mode == Binding::DepthOnly) fixture.descriptor[stencil_read_base_word] = 0;
        if (mode == Binding::StencilOnly) fixture.descriptor[z_read_base_word] = 0;
        fixture.emit(); const auto *packet = reinterpret_cast<const render::DepthTargetPacket *>(fixture.begin() + initial_offset);
        full_record(fixture.begin(), packet, packet);
        CHECK(packet->z_and_stencil_info.get() == render::Field<uint64_t>::read(fixture.descriptor.data()));
    }
    constexpr bool use_descriptor[]{false, true};
    for (bool mode : use_descriptor) {
        ++scenarios; enabled(); Fixture fixture;
        fixture.descriptor[z_read_base_word] = fixture.descriptor[stencil_read_base_word] = 0;
        bb_render_set_depth_render_target(fixture.commands(), mode ? fixture.descriptor.data() : nullptr);
        clear_record(fixture.begin(), reinterpret_cast<const render::ClearDepthTargetPacket *>(fixture.begin() + initial_offset));
    }
}

void callbacks()
{
    constexpr uint32_t clear_words = 6;
    constexpr std::size_t depth_view_word = 8;
    ++scenarios; enabled(); Fixture fixture; fixture.redirect = fixture.change_words = true;
    callback(fixture); fixture.emit(); const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.replacement.data());
    const auto *packet = reinterpret_cast<const render::DepthTargetPacket *>(begin + redirected_offset);
    full_record(begin, packet, packet); CHECK(packet->depth_view.payload.get() == fixture.descriptor[depth_view_word]);
    ++scenarios; enabled(); Fixture rejected; rejected.redirect = true; rejected.callback_result = 0;
    callback(rejected); const auto untouched = rejected.replacement; rejected.emit(); std::size_t count;
    rt_render_submitted(reinterpret_cast<uintptr_t>(rejected.replacement.data()), UINT32_MAX, &count);
    CHECK(count == 0 && rejected.replacement == untouched);
    ++scenarios; enabled(); Fixture absent; absent.redirect = absent.invalidate = true; callback(absent); absent.emit();
    clear_record(reinterpret_cast<uintptr_t>(absent.replacement.data()),
        reinterpret_cast<const render::ClearDepthTargetPacket *>(absent.replacement.data() + redirected_offset));
    ++scenarios; enabled(); Fixture populated; populated.redirect = populated.validate = true;
    populated.descriptor[z_read_base_word] = populated.descriptor[stencil_read_base_word] = 0;
    populated.expected_request = clear_words; callback(populated); populated.emit();
    packet = reinterpret_cast<const render::DepthTargetPacket *>(populated.replacement.data() + redirected_offset);
    full_record(reinterpret_cast<uintptr_t>(populated.replacement.data()), packet, packet);
}

void borrowed_overlap()
{
    ++scenarios; enabled(); Fixture fixture;
    auto *packet = reinterpret_cast<render::DepthTargetPacket *>(fixture.packet.data() + initial_offset);
    constexpr std::size_t borrowed_offset = 20;
    auto *borrowed = reinterpret_cast<uint8_t *>(packet) + borrowed_offset;
    std::memcpy(borrowed, target.data(), sizeof(target));
    bb_render_set_depth_render_target(fixture.commands(), borrowed);
    full_record(fixture.begin(), packet, packet);
    CHECK(render::Field<uint32_t>::read(borrowed) != target[0]);
}

void final_publication()
{
    constexpr std::array<std::size_t, 2> packet_offsets{0, 8};
    for (std::size_t packet_offset : packet_offsets) {
        ++scenarios; enabled(); Fixture fixture;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(fixture.prefix.data());
        const uintptr_t cursor = begin + packet_offset;
        const std::size_t pair_offset = offsetof(render::PacketBuffer, cursor) - packet_offset -
            offsetof(render::DepthTargetPacket, z_and_stencil_info);
        auto *aliased_source = reinterpret_cast<uint8_t *>(fixture.descriptor.data()) + pair_offset;
        std::memcpy(aliased_source, &cursor, sizeof(cursor));
        fixture.storage(begin, cursor, begin + storage_bytes); fixture.emit();
        const auto *packet = reinterpret_cast<const render::DepthTargetPacket *>(cursor);
        full_record(begin, packet, packet);
        const auto *published_pair = reinterpret_cast<const uint8_t *>(&packet->z_and_stencil_info) + pair_offset;
        CHECK(render::Field<uint64_t>::read(published_pair) == cursor + sizeof(render::DepthTargetPacket));
        CHECK(render::Field<uint64_t>::read(published_pair) != cursor);
    }
}

void expect_abort(void (*action)())
{
    constexpr int redirection_failed = 2;
    ++scenarios; const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) { const rlimit no_core{0, 0}; setrlimit(RLIMIT_CORE, &no_core);
        if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(redirection_failed);
        action();
        std::_Exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

void malformed_block()
{
    enabled(); Fixture fixture; fixture.emit(); rt_render_forget();
    auto *packet = reinterpret_cast<render::DepthTargetPacket *>(fixture.packet.data() + initial_offset);
    constexpr uint32_t nop_header = 0xc0001000;
    packet->header.set(nop_header);
    render::record_depth_target_block({fixture.begin(), initial_offset, true}, packet, packet);
}

void malformed_late_single()
{
    enabled(); Fixture fixture; fixture.emit();
    auto *packet = reinterpret_cast<render::DepthTargetPacket *>(fixture.packet.data() + initial_offset);
    packet->htile_surface.register_index.set(0);
    render::record_depth_target_block({fixture.begin(), initial_offset, true}, packet, packet);
}

void malformed_late_header()
{
    enabled(); Fixture fixture; fixture.emit(); rt_render_forget();
    auto *packet = reinterpret_cast<render::DepthTargetPacket *>(fixture.packet.data() + initial_offset);
    constexpr uint32_t nop_header = 0xc0001000;
    packet->htile_surface.header.set(nop_header);
    render::record_depth_target_block({fixture.begin(), initial_offset, true}, packet, packet);
}

void nonzero_clear()
{
    enabled(); Fixture fixture; bb_render_set_depth_render_target(fixture.commands(), nullptr);
    auto *packet = reinterpret_cast<render::ClearDepthTargetPacket *>(fixture.packet.data() + initial_offset);
    packet->stencil_info.payload.set(1);
    render::record_depth_target_clear({fixture.begin(), initial_offset, true}, packet);
}
#endif
}  // namespace

int main()
{
    headless();
#if !defined(PB_DEPTH_OBSERVER_ABSENT)
    direct_and_clear(); callbacks(); borrowed_overlap(); final_publication();
    expect_abort(malformed_block); expect_abort(malformed_late_single); expect_abort(malformed_late_header); expect_abort(nonzero_clear);
    rt_render_forget();
#endif
    std::printf("depth-target observer: %u scenarios, %u checks, zero failures\n", scenarios, checks);
    return 0;
}
