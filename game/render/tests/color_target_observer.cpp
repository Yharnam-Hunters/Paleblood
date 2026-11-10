// SPDX-License-Identifier: GPL-2.0-or-later
// The real writer and recorder operate only on owned synthetic command/descriptor storage.
#include "../color_target_observation.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void bb_render_set_render_target(render::PacketBuffer *, uint32_t, const void *);

namespace {
unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (0)

enum ColorWord { Base, Pitch, Slice, View, Info, Attrib, Reserved, Cmask, CmaskSlice, Fmask, FmaskSlice };
constexpr std::size_t storage_bytes = 512;
constexpr uint32_t initial_offset = 64, redirected_offset = 128;
constexpr uint32_t block_words = 15, clear_words = 3;
constexpr uint8_t accepted = 0x80, packet_fill = 0x6b;
constexpr uint32_t base_word = 0x12345678;
constexpr uint32_t color_rgba8 = 0xa, srgb = 6, alternate_swap = 1;
constexpr uint32_t format_shift = 2, number_shift = 8, swap_shift = 11;
constexpr uint32_t target_width = 1920, target_height = 1080;
constexpr uint32_t pitch_pixels_per_unit = 8, slice_pixels_per_unit = 64;
constexpr uint32_t tile_mode = 10;
constexpr unsigned mapped_pages = 2;
constexpr std::array<uint32_t, RT_COLOR_TARGET_WORDS> target_words = {
    base_word, target_width / pitch_pixels_per_unit - 1,
    target_width * target_height / slice_pixels_per_unit - 1, 0,
    color_rgba8 << format_shift | srgb << number_shift | alternate_swap << swap_shift,
    tile_mode, 0xa5a55a5a, 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00};

template <typename T> void field(render::PacketBuffer *commands, std::size_t offset, T value)
{
    std::memcpy(reinterpret_cast<uint8_t *>(commands) + offset, &value, sizeof(value));
}

struct Fixture {
    alignas(uintptr_t) std::array<uint8_t, storage_bytes> prefix{}, packet{}, replacement{};
    std::array<uint32_t, RT_COLOR_TARGET_WORDS> descriptor = target_words;
    uint8_t callback_result = accepted;
    uint32_t expected_request = block_words;
    bool redirect{}, invalidate{}, validate{};

    render::PacketBuffer *commands() { return reinterpret_cast<render::PacketBuffer *>(prefix.data()); }
    uintptr_t begin() const { return reinterpret_cast<uintptr_t>(packet.data()); }
    void storage(uintptr_t start, uintptr_t cursor, uintptr_t limit)
    {
        field(commands(), offsetof(render::PacketBuffer, begin), start);
        field(commands(), offsetof(render::PacketBuffer, cursor), cursor);
        field(commands(), offsetof(render::PacketBuffer, limit), limit);
    }
    Fixture()
    {
        packet.fill(packet_fill); replacement.fill(packet_fill);
        storage(begin(), begin() + initial_offset, begin() + storage_bytes);
    }
    void emit(unsigned slot = 0, const void *source = nullptr)
    {
        bb_render_set_render_target(commands(), slot, source ? source : descriptor.data());
    }
};

uint8_t reserve(render::PacketBuffer *commands, uint32_t requested, uintptr_t argument)
{
    auto &fixture = *reinterpret_cast<Fixture *>(argument);
    CHECK(requested == fixture.expected_request);
    if (fixture.redirect) {
        const uintptr_t start = reinterpret_cast<uintptr_t>(fixture.replacement.data());
        field(commands, offsetof(render::PacketBuffer, begin), start);
        field(commands, offsetof(render::PacketBuffer, cursor), start + redirected_offset);
    }
    if (fixture.invalidate) fixture.descriptor[Base] = 0;
    if (fixture.validate) fixture.descriptor[Base] = base_word;
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
    auto *mapping = static_cast<uint8_t *>(mmap(nullptr, std::size_t(page) * mapped_pages,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    CHECK(mapping != MAP_FAILED);
    CHECK(mprotect(mapping, std::size_t(page), PROT_NONE) == 0);
    auto *commands = reinterpret_cast<render::PacketBuffer *>(mapping + page - sizeof(uintptr_t));
    Fixture fixture;
    field(commands, offsetof(render::PacketBuffer, cursor), fixture.begin());
    field(commands, offsetof(render::PacketBuffer, limit), fixture.begin() + storage_bytes);
    bb_render_set_render_target(commands, 0, fixture.descriptor.data());
    CHECK(reinterpret_cast<const render::ColorTargetPacket *>(fixture.packet.data())->header.get() ==
        render::color_target_packet::block_header);
    bb_render_set_render_target(commands, 0, nullptr);
    CHECK(render::packet_cursor(commands) == fixture.begin() +
        (block_words + clear_words) * render::packet_word_bytes);
    // Neither disabled observer helper evaluates the inaccessible packet fields.
    render::record_color_target_block({}, reinterpret_cast<const render::ColorTargetPacket *>(mapping));
    render::record_color_target_clear({}, reinterpret_cast<const render::PreparationCommandPacket *>(mapping));
    CHECK(munmap(mapping, std::size_t(page) * mapped_pages) == 0);
    ++scenarios; Fixture moved; moved.redirect = true; callback(moved); moved.emit();
    CHECK(reinterpret_cast<const render::ColorTargetPacket *>(moved.replacement.data() + redirected_offset)->
        base_and_pitch.get() != 0);
}

#if !defined(PB_COLOR_OBSERVER_ABSENT)
void enabled() { CHECK(rt_render_observer_available()); rt_render_forget(); }

const rt_render_call &record(uintptr_t buffer, uint32_t offset, unsigned slot)
{
    std::size_t count;
    const auto *calls = rt_render_submitted(buffer, UINT32_MAX, &count);
    CHECK(count == 1 && calls && calls->kind == RT_CALL_COLOR_TARGET);
    CHECK(calls->offset == offset && calls->u.color_target.slot == slot);
    return *calls;
}

void block_record(uintptr_t buffer, uint32_t offset, unsigned slot, const render::ColorTargetPacket *packet)
{
    std::array<uint32_t, RT_COLOR_TARGET_WORDS> final_words;
    std::memcpy(final_words.data(), &packet->base_and_pitch, final_words.size() * sizeof(uint32_t));
    rt_color_target expected{};
    constexpr std::size_t error_capacity = 256;
    char error[error_capacity]{};
    const int decoded = rt_color_target_decode(final_words.data(), &expected, error, sizeof(error));
    CHECK(decoded >= 0);
    const auto &call = record(buffer, offset, slot);
    CHECK(call.u.color_target.enabled == decoded);
    if (!decoded) return;
    const auto &target = call.u.color_target.target;
    CHECK(target.address == expected.address && target.pitch == expected.pitch && target.height == expected.height);
    CHECK(target.first_slice == expected.first_slice && target.last_slice == expected.last_slice);
    CHECK(target.format == expected.format && target.number_type == expected.number_type);
    CHECK(target.component_swap == expected.component_swap && target.tile_mode_index == expected.tile_mode_index);
    CHECK(target.samples == expected.samples && target.fast_clear == expected.fast_clear &&
        target.compression == expected.compression);
}

void direct_and_clears()
{
    for (unsigned slot : {0u, unsigned(RT_COLOR_TARGETS - 1)}) {
        ++scenarios; enabled(); Fixture fixture; fixture.emit(slot);
        block_record(fixture.begin(), initial_offset, slot,
            reinterpret_cast<const render::ColorTargetPacket *>(fixture.packet.data() + initial_offset));
        const auto &target = record(fixture.begin(), initial_offset, slot).u.color_target.target;
        constexpr uint64_t expected_address = uint64_t(base_word) << number_shift;
        CHECK(target.address == expected_address && target.pitch == target_width && target.height == target_height);
        for (bool null_descriptor : {false, true}) {
            ++scenarios; enabled(); Fixture clear; clear.descriptor[Base] = 0;
            bb_render_set_render_target(clear.commands(), slot, null_descriptor ? nullptr : clear.descriptor.data());
            CHECK(!record(clear.begin(), initial_offset, slot).u.color_target.enabled);
        }
    }
    ++scenarios; enabled(); Fixture invalid_format; invalid_format.descriptor[Info] = 0; invalid_format.emit();
    CHECK(!record(invalid_format.begin(), initial_offset, 0).u.color_target.enabled);
    ++scenarios; enabled(); Fixture prefix_alias; prefix_alias.descriptor[Base] = 0;
    const uintptr_t owner = reinterpret_cast<uintptr_t>(prefix_alias.prefix.data());
    prefix_alias.storage(owner, owner, owner + storage_bytes); prefix_alias.emit();
    CHECK(!record(owner, 0, 0).u.color_target.enabled);
}

void callback_changes()
{
    for (bool initially_valid : {false, true}) {
        for (uint8_t result : {uint8_t(0), accepted}) {
            ++scenarios; enabled(); Fixture fixture;
            fixture.redirect = true; fixture.callback_result = result;
            fixture.descriptor[Base] = initially_valid ? base_word : 0;
            fixture.expected_request = initially_valid ? block_words : clear_words;
            fixture.invalidate = initially_valid; fixture.validate = !initially_valid;
            callback(fixture); const auto unchanged = fixture.replacement; fixture.emit();
            const uintptr_t start = reinterpret_cast<uintptr_t>(fixture.replacement.data());
            if (!result) {
                std::size_t count; rt_render_submitted(start, UINT32_MAX, &count);
                CHECK(count == 0 && fixture.replacement == unchanged);
            } else if (initially_valid) {
                CHECK(!record(start, redirected_offset, 0).u.color_target.enabled);
            } else {
                block_record(start, redirected_offset, 0,
                    reinterpret_cast<const render::ColorTargetPacket *>(start + redirected_offset));
            }
        }
    }
}

void borrowed_source_overlap()
{
    ++scenarios; enabled(); Fixture fixture;
    auto *output = fixture.packet.data() + initial_offset;
    constexpr std::size_t source_offset = offsetof(render::ColorTargetPacket, slice_and_view);
    auto source = target_words;
    source[FmaskSlice] = color_rgba8 << format_shift;
    source[Fmask] = 0;
    source[Pitch] = 0;
    std::memcpy(output + source_offset, source.data(), sizeof(source));
    fixture.emit(0, output + source_offset);
    const auto *packet = reinterpret_cast<const render::ColorTargetPacket *>(output);
    CHECK(uint32_t(packet->base_and_pitch.get()) == base_word);
    CHECK(uint32_t(packet->slice_and_view.get()) == source[FmaskSlice]);
    block_record(fixture.begin(), initial_offset, 0, packet);
}

struct WordBoundaryPage {
    void *reservation;
    std::size_t size;
    uintptr_t start;
    WordBoundaryPage()
    {
        const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
        size = std::size_t(UINT32_MAX) + 1 + std::size_t(page);
        reservation = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(reservation != MAP_FAILED);
        start = (reinterpret_cast<uintptr_t>(reservation) + UINT32_MAX) & ~uintptr_t(UINT32_MAX);
        CHECK(start + uintptr_t(page) <= reinterpret_cast<uintptr_t>(reservation) + size);
        CHECK(mprotect(reinterpret_cast<void *>(start), std::size_t(page), PROT_READ | PROT_WRITE) == 0);
    }
    ~WordBoundaryPage() { CHECK(munmap(reservation, size) == 0); }
};

void final_bank_alias(unsigned requested_slot)
{
    enabled(); WordBoundaryPage tail; Fixture fixture;
    const uintptr_t owner = reinterpret_cast<uintptr_t>(fixture.prefix.data());
    constexpr unsigned pointer_high_shift = 32;
    constexpr std::size_t packet_offset = offsetof(render::PacketBuffer, cursor) -
        offsetof(render::ColorTargetPacket, index);
    fixture.storage(owner, owner + packet_offset, owner + storage_bytes);
    fixture.descriptor[Base] = uint32_t(tail.start >> pointer_high_shift);
    fixture.descriptor[Pitch] = 0;
    fixture.emit(requested_slot);
    const auto *packet = reinterpret_cast<const render::ColorTargetPacket *>(owner + packet_offset);
    const uint32_t final_index = packet->index.get();
    const unsigned final_slot = render::observed_color_target_slot(final_index, render::color_target_packet::base_index);
    constexpr unsigned changed_banks = sizeof(render::ColorTargetPacket) / render::color_target_packet::register_stride;
    CHECK(final_slot == requested_slot + changed_banks);
    block_record(owner, uint32_t(packet_offset), final_slot, packet);
}

void unsupported_bank_alias()
{
    constexpr unsigned first_overflowing_slot = 4;
    final_bank_alias(first_overflowing_slot);
}
void unsupported_slot() { enabled(); Fixture fixture; fixture.emit(RT_COLOR_TARGETS); }
void unsupported_clear_payload()
{
    enabled(); render::PreparationCommandPacket packet{};
    packet.header.set(render::preparation_packet::set_context_reg_one_value_header);
    packet.register_index.set(render::color_target_packet::info_index);
    packet.payload.set(color_rgba8 << format_shift);
    render::record_color_target_clear({1, 0, true}, &packet);
}
void unsupported_header()
{
    enabled(); render::ColorTargetPacket packet{};
    packet.header.set(render::nop_one_word_header);
    packet.index.set(render::color_target_packet::base_index);
    render::record_color_target_block({1, 0, true}, &packet);
}
void clear_publication_alias()
{
    enabled(); Fixture fixture; fixture.descriptor[Base] = 0;
    const uintptr_t owner = reinterpret_cast<uintptr_t>(fixture.prefix.data());
    constexpr std::size_t packet_offset = offsetof(render::PacketBuffer, cursor) -
        offsetof(render::PreparationCommandPacket, payload);
    fixture.storage(owner, owner + packet_offset, owner + storage_bytes); fixture.emit();
}
void expect_abort(void (*action)())
{
    constexpr int redirection_failed = 2;
    ++scenarios; const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        const rlimit no_core{0, 0}; setrlimit(RLIMIT_CORE, &no_core);
        if (!std::freopen("/dev/null", "w", stderr)) std::_Exit(redirection_failed);
        action();
        std::_Exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
#endif
}  // namespace

int main()
{
    headless();
#if !defined(PB_COLOR_OBSERVER_ABSENT)
    direct_and_clears(); callback_changes(); borrowed_source_overlap();
    ++scenarios; final_bank_alias(0);
    expect_abort(unsupported_bank_alias); expect_abort(unsupported_slot);
    expect_abort(unsupported_clear_payload); expect_abort(unsupported_header);
    expect_abort(clear_publication_alias);
    rt_render_forget();
#endif
    std::printf("color-target observer: %u scenarios, %u checks, zero failures\n", scenarios, checks);
    return 0;
}
