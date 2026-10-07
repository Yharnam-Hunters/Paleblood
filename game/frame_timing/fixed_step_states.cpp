// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: state methods that advance their owner by one fixed frame step.
//
// Nine small virtual methods (one vtable slot each) drive an owner object: a state at +0x25c, a
// request word at +0x260, a busy count at +0x264, and an "advance" method at vtable slot +0xd0
// that takes a frame-time descriptor ({descriptor vtable, seconds}, the same type the frame step
// passes to the task update). The originals hard-code 1/30 s; the 60 FPS community patches
// rewrite it to 1/60 s, which BB_TARGET_FPS=60 does here (target_fps.h). What the owner is, is
// not known yet. Four shapes:
//   until_idle (5 copies, owner at +0x8; 1 copy at +0x10): when busy, advance; 1 while still
//     busy, else 2.
//   request_10: when idle, request 10, advance, clear the request (returns nothing).
//   request_7 (2 copies): 4 when busy or the state is not 2..5; else request 7, advance, clear,
//     return 2.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/capture.h"
#include "runtime/guest.h"
#include "target_fps.h"

namespace {

constexpr uint32_t descriptor_vtable = 0x056efd30;   // + 0x10: the vtable the descriptor points at
constexpr unsigned state_offset = 0x25c, request_offset = 0x260, busy_offset = 0x264;
constexpr unsigned advance_slot = 0xd0;

struct Descriptor {
    void *vtable;
    uint32_t seconds_bits;
};

using Advance = void(void *owner, Descriptor *step);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }

uint32_t guest_address(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

std::string hex32(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%02x%02x%02x%02x", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v >> 24);
    return b;
}

// Records one call for tools/verify.py: the method's object, the owner's fields it reads, and
// the advance method as a stub that leaves the busy count the real one left.
struct Capture {
    const char *function;
    uint32_t address;
    unsigned owner_field;
    int number;
    std::string owner_memory, stub;

    Capture(const char *f, uint32_t a, unsigned field) : function(f), address(a), owner_field(field)
    {
        number = rt_capture_begin(function);
    }
    void before(const unsigned char *owner)
    {
        if (number < 0) return;
        for (unsigned off : {state_offset, request_offset, busy_offset})
            owner_memory += ", {\"addr\": \"buf:owner+" + std::to_string(off) + "\", \"bytes\": \"" +
                            hex32(get<uint32_t>(owner, off)) + "\"}";
    }
    void advanced(const void *target, const unsigned char *owner)
    {
        if (number < 0) return;
        char b[64];
        std::snprintf(b, sizeof b, "0x%08x", guest_address(target));
        owner_memory += std::string(", {\"addr\": \"buf:owner_vt+") + std::to_string(advance_slot) +
                        "\", \"guest\": \"" + b + "\"}";
        stub = std::string("{\"address\": \"") + b + "\", \"argc\": 2, \"argi\": [0, 1], "
               "\"argmem\": [{\"arg\": 1, \"size\": 12}, {\"arg\": 0, \"offset\": 604, \"size\": 12}], \"writes\": [{\"arg\": 0, \"offset\": " +
               std::to_string(busy_offset) + ", \"bytes\": \"" + hex32(get<uint32_t>(owner, busy_offset)) + "\"}]}";
    }
    void write(const char *returns)
    {
        if (number < 0) return;
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x%08x\", \"id\": \"capture_%04d\", \"returns\": \"%s\", "
                      "\"args\": {\"rdi\": \"buf:self\"}, ", address, number, returns);
        const std::string json = std::string(head) +
            "\"buffers\": {\"self\": {\"size\": 24}, \"owner\": {\"size\": 616}, \"owner_vt\": {\"size\": 216}}, "
            "\"memory\": [{\"addr\": \"buf:self+" + std::to_string(owner_field) + "\", \"pointer\": \"owner\"}, "
            "{\"addr\": \"buf:owner\", \"pointer\": \"owner_vt\"}" + owner_memory + "], \"stubs\": [" + stub +
            "], \"imports\": []}";
        rt_capture_write(function, number, json.c_str());
    }
};

void advance(unsigned char *owner, Capture &cap)
{
    void *target = (*reinterpret_cast<void ***>(owner))[advance_slot / 8];
    Descriptor step{rt::ptr<unsigned char>(descriptor_vtable) + 0x10, frame_timing::fixed_step_bits()};
    reinterpret_cast<Advance *>(target)(owner, &step);
    cap.advanced(target, owner);
}

unsigned char *owner_of(unsigned char *self, unsigned field)
{
    return get<unsigned char *>(self, field);
}

uint32_t until_idle(unsigned char *self, unsigned field, const char *name, uint32_t address)
{
    Capture cap(name, address, field);
    unsigned char *owner = owner_of(self, field);
    cap.before(owner);
    uint32_t result = 2;
    if (get<int32_t>(owner, busy_offset) != 0) {
        advance(owner, cap);
        result = get<int32_t>(owner, busy_offset) != 0 ? 1 : 2;
    }
    cap.write("i32");
    return result;
}

void request_10(unsigned char *self, const char *name, uint32_t address)
{
    Capture cap(name, address, 0x8);
    unsigned char *owner = owner_of(self, 0x8);
    cap.before(owner);
    if (get<int32_t>(owner, busy_offset) == 0) {
        put<uint32_t>(owner, request_offset, 10);
        advance(owner, cap);
        put<uint32_t>(owner, request_offset, 0);
    }
    cap.write("void");
}

uint32_t request_7(unsigned char *self, const char *name, uint32_t address)
{
    Capture cap(name, address, 0x8);
    unsigned char *owner = owner_of(self, 0x8);
    cap.before(owner);
    uint32_t result = 4;
    if (get<int32_t>(owner, busy_offset) == 0 && get<uint32_t>(owner, state_offset) - 2u <= 3u) {
        put<uint32_t>(owner, request_offset, 7);
        advance(owner, cap);
        put<uint32_t>(owner, request_offset, 0);
        result = 2;
    }
    cap.write("i32");
    return result;
}

}  // namespace

#define UNTIL_IDLE(addr, field)                                                                          \
    extern "C" uint32_t bb_frame_timing_until_idle_##addr(unsigned char *self)                           \
    {                                                                                                     \
        return until_idle(self, field, "frame_timing_until_idle_" #addr, 0x##addr);                      \
    }

UNTIL_IDLE(01f6a9f0, 0x8)
UNTIL_IDLE(0200e100, 0x8)
UNTIL_IDLE(02012610, 0x8)
UNTIL_IDLE(020128f0, 0x8)
UNTIL_IDLE(02012bf0, 0x8)
UNTIL_IDLE(0200d8a0, 0x10)

extern "C" void bb_frame_timing_request_10_02012780(unsigned char *self)
{
    request_10(self, "frame_timing_request_10_02012780", 0x02012780);
}

extern "C" uint32_t bb_frame_timing_request_7_0200e270(unsigned char *self)
{
    return request_7(self, "frame_timing_request_7_0200e270", 0x0200e270);
}

extern "C" uint32_t bb_frame_timing_request_7_02012a60(unsigned char *self)
{
    return request_7(self, "frame_timing_request_7_02012a60", 0x02012a60);
}
