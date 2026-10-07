// SPDX-License-Identifier: GPL-2.0-or-later
// Event system (SprjEmkSystem): find or add an event entry.
//
// event_emk_add (0x016efd00) keeps two singly linked lists of entries (heads at +0x0 and +0x8 of
// its owner; next at +0x70), keyed by an id (+0x28, read from *(spec+0x8), -1 when absent) and
// a short sub-id (+0x2c). When both keys are non-negative and an entry with them exists in
// either list, it does nothing. Otherwise it allocates 0xe0 bytes through the allocator
// singleton (vtable slot +0x58, alignment 0x10), constructs the entry (0x016ec990, which also
// takes a seventh argument from the stack), inserts it into the second list in key order, and
// when the low byte of `start` is set, advances it once (vtable slot +0x10) with the frame-time
// descriptor: 1/30 s, 1/60 s in the 60 FPS patches (BB_TARGET_FPS=60, target_fps.h).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t allocator_singleton = 0x05940420;
constexpr uint32_t construct_entry = 0x016ec990;
constexpr uint32_t descriptor_vtable = 0x056efd30;   // + 0x10
constexpr unsigned key_offset = 0x28, subkey_offset = 0x2c, next_offset = 0x70;
constexpr uint64_t entry_size = 0xe0;

struct Descriptor {
    void *vtable;
    uint32_t seconds_bits;
};

using Alloc = unsigned char *(void *self, uint64_t size, uint64_t align);
using Construct = void(unsigned char *entry, int32_t subkey, unsigned char *spec, void *arg3, int32_t arg4, int32_t arg5);
using Advance = void(unsigned char *entry, Descriptor *step);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }

unsigned char *next(unsigned char *e) { return get<unsigned char *>(e, next_offset); }

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

std::string addr(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// Recording: both lists as chains of small node buffers (keys and next only), up to 64 nodes
// each, the spec's id, and the allocator, constructor and advance as stubs.
struct Capture {
    int number = -1;
    std::string buffers, memory, stubs;
    void buffer(const std::string &name, unsigned size)
    {
        buffers += (buffers.empty() ? "" : ", ") + ("\"" + name + "\": {\"size\": " + std::to_string(size) + "}");
    }
    void mem(const std::string &e) { memory += (memory.empty() ? "" : ", ") + e; }
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    bool list(const char *tag, unsigned head_offset, unsigned char *head)
    {
        std::string at = "buf:owner+" + std::to_string(head_offset);
        int n = 0;
        for (unsigned char *e = head; e; e = next(e), n++) {
            if (n == 64) return false;
            const std::string name = std::string(tag) + std::to_string(n);
            buffer(name, next_offset + 8);
            mem("{\"addr\": \"" + at + "\", \"pointer\": \"" + name + "\"}");
            mem("{\"addr\": \"buf:" + name + "+" + std::to_string(key_offset) + "\", \"bytes\": \"" +
                hex32(get<uint32_t>(e, key_offset)) + "\"}");
            mem("{\"addr\": \"buf:" + name + "+" + std::to_string(subkey_offset) + "\", \"bytes\": \"" +
                hex32(get<uint16_t>(e, subkey_offset)).substr(0, 4) + "\"}");
            at = "buf:" + name + "+" + std::to_string(next_offset);
        }
        mem("{\"addr\": \"" + at + "\", \"bytes\": \"0000000000000000\"}");
        return true;
    }
};

}  // namespace

extern "C" void bb_event_emk_add(unsigned char *owner, uint32_t start, int32_t subkey, unsigned char *spec,
                                 void *arg4, int32_t arg5, int32_t arg6)
{
    Capture cap;
    cap.number = rt_capture_begin("event_emk_add");
    const int32_t *id_ptr = get<const int32_t *>(spec, 0x8);
    const int32_t key = id_ptr ? *id_ptr : -1;
    if (cap.number >= 0) {
        cap.buffer("owner", 16);
        cap.buffer("spec", 16);
        if (id_ptr) {
            cap.buffer("id", 4);
            cap.mem("{\"addr\": \"buf:spec+8\", \"pointer\": \"id\"}");
            cap.mem("{\"addr\": \"buf:id\", \"bytes\": \"" + hex32(static_cast<uint32_t>(key)) + "\"}");
        } else {
            cap.mem("{\"addr\": \"buf:spec+8\", \"bytes\": \"0000000000000000\"}");
        }
        if (!cap.list("a", 0, get<unsigned char *>(owner, 0)) || !cap.list("b", 8, get<unsigned char *>(owner, 8)))
            cap.number = -1;   // a list too long to record: skip this call
    }
    auto write = [&](const char *what) {
        if (cap.number < 0) return;
        char head[512];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x016efd00\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:owner\", \"rsi\": \"0x%x\", \"rdx\": \"0x%x\", \"rcx\": \"buf:spec\", "
                      "\"r8\": \"0x%llx\", \"r9\": \"0x%x\"}, \"note\": \"%s\", ",
                      cap.number, start, static_cast<uint32_t>(subkey), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(arg4)),
                      static_cast<uint32_t>(arg5), what);
        const std::string json = std::string(head) + "\"buffers\": {" + cap.buffers + "}, \"memory\": [" + cap.memory +
                                 "], \"stubs\": [" + cap.stubs + "], \"imports\": []}";
        rt_capture_write("event_emk_add", cap.number, json.c_str());
    };

    if ((key | subkey) >= 0) {
        for (unsigned head : {0u, 8u})
            for (unsigned char *e = get<unsigned char *>(owner, head); e; e = next(e))
                if (get<int32_t>(e, key_offset) == key && static_cast<int32_t>(get<int16_t>(e, subkey_offset)) == subkey) {
                    write("found");
                    return;
                }
    }

    void *allocator = *rt::ptr<void *>(allocator_singleton);
    void *alloc_fn = (*static_cast<void ***>(allocator))[0x58 / 8];
    unsigned char *entry = reinterpret_cast<Alloc *>(alloc_fn)(allocator, entry_size, 0x10);
    if (cap.number >= 0) {
        cap.buffer("allocator", 8);
        cap.buffer("allocator_vt", 0x60);
        cap.mem("{\"addr\": \"" + addr(allocator_singleton) + "\", \"pointer\": \"allocator\"}");
        cap.mem("{\"addr\": \"buf:allocator\", \"pointer\": \"allocator_vt\"}");
        cap.mem("{\"addr\": \"buf:allocator_vt+88\", \"guest\": \"" + addr(guest_address(alloc_fn)) + "\"}");
        cap.stub("{\"address\": \"" + addr(guest_address(alloc_fn)) + "\", \"argc\": 3, \"ret\": \"" +
                 std::string(entry ? "buf:entry" : "0x0") + "\"}");
    }
    if (!entry) {
        write("no memory");
        return;
    }
    // The constructor's sixth argument is the caller's seventh (stack): not compared in cases.
    rt::fn<Construct>(construct_entry)(entry, subkey, spec, arg4, arg5, arg6);
    const int32_t entry_key = get<int32_t>(entry, key_offset);
    void *advance_fn = (*reinterpret_cast<void ***>(entry))[0x10 / 8];
    if (cap.number >= 0) {
        cap.buffer("entry", static_cast<unsigned>(entry_size));
        cap.buffer("entry_vt", 0x18);
        cap.stub("{\"address\": \"0x016ec990\", \"argc\": 5, \"writes\": [{\"arg\": 0, \"offset\": 0, \"pointer\": \"entry_vt\"}, "
                 "{\"arg\": 0, \"offset\": 40, \"bytes\": \"" + hex32(static_cast<uint32_t>(entry_key)) + "\"}]}");
        cap.mem("{\"addr\": \"buf:entry_vt+16\", \"guest\": \"" + addr(guest_address(advance_fn)) + "\"}");
    }

    unsigned char *head = get<unsigned char *>(owner, 8);
    unsigned char *prev = nullptr;
    for (unsigned char *e = head; e; e = next(e)) {
        const int32_t k = get<int32_t>(e, key_offset);
        if (entry_key == k) {
            if (!(static_cast<int32_t>(get<int16_t>(e, subkey_offset)) >= subkey)) break;
        } else if (entry_key > k) {
            break;
        }
        prev = e;
    }
    if (prev) {
        put<unsigned char *>(entry, next_offset, next(prev));
        put<unsigned char *>(prev, next_offset, entry);
    } else {
        put<unsigned char *>(entry, next_offset, head);
        put<unsigned char *>(owner, 8, entry);
    }

    if (static_cast<uint8_t>(start)) {
        Descriptor step{rt::ptr<unsigned char>(descriptor_vtable) + 0x10, frame_timing::fixed_step_bits()};
        reinterpret_cast<Advance *>(advance_fn)(entry, &step);
        if (cap.number >= 0)
            cap.stub("{\"address\": \"" + addr(guest_address(advance_fn)) + "\", \"argc\": 2, \"argi\": [0, 1], "
                     "\"argmem\": [{\"arg\": 1, \"size\": 12}]}");
    }
    write("added");
}
