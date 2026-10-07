// SPDX-License-Identifier: GPL-2.0-or-later
// Render: a per-frame update that ages a list of reference-counted objects and steps a child.
//
// render_frame_update_02377bd0 (0x02377bd0, once a frame in gameplay; `step` is the caller's
// frame argument, passed on unchanged):
//   1. when +0x38 is set: 0x02379d60(&this->+0x8, list, &this->+0x28, *this->+0x30, this->+0x30,
//      this->+0x38) (it may change the list);
//   2. walks the list (sentinel at +0x10, next +0x0, prev +0x8, object +0x10): each object is
//      updated with 0x02373850(object, step); when that returns false, the reference is dropped
//      (atomic decrement of +0x8; at 1 the object's vtable slot 0 destroys it, at 0 or less the
//      engine's "Invalid Unref()" fatal error is reported) and the node is unlinked and freed
//      through the allocator at +0x20 (vtable slot +0x70), and the count at +0x18 goes down;
//   3. when +0x80 is set: 0x0236e000(this->+0x80, step, RendMan->+0x28) (RendMan: singleton
//      0x05940298);
//   4. when +0x70 is set: its +0x20 = this->+0x98, then its vtable slot +0x40 with a frame time
//      of 1/30 s (0 when +0x8c is set), and +0x8c = 0.
// BB_TARGET_FPS=uncapped does what "Uncap FPS++" does: step 4 passes the flipper's measured frame
// time (+0x264) whatever +0x8c says, and step 3 drops the RendMan check.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t list_prepare = 0x02379d60, update_object = 0x02373850, update_child80 = 0x0236e000;
constexpr uint32_t rendman_singleton = 0x05940298, flipper_singleton = 0x059404f8;
constexpr uint32_t fatal_error = 0x024b55b0;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd, str_rendman = 0x04d3b1e1,
                   str_bad_unref = 0x04d40331, k_frame_time = 0x04d29170;   // 1/30 s

using Prepare = void(void *, void *, void *, void *, void *, void *);
using Update = uint8_t(void *object, void *step);
using Child80 = void(void *child, void *step, void *rendman_field);
using Fatal = void(const char *, int, const char *, ...);
using Destroy = void(void *object);
using Free = void(void *allocator, void *node);
using Step = void(void *child, float frame_time);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }
void *slot(void *object, unsigned off) { return (*static_cast<void ***>(object))[off / 8]; }

uint32_t guest_address(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

std::string hexbytes(const void *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        const unsigned b = static_cast<const unsigned char *>(p)[i];
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

std::string addr(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// Recording: the list as the walk sees it (after step 1), the objects' reference counts and the
// update results, the destroy and free targets, and the child objects as stand-ins.
struct Capture {
    int number = -1;
    std::string buffers, memory, stubs;
    std::map<const void *, std::string> names;
    void buffer(const std::string &n, unsigned size) { buffers += (buffers.empty() ? "" : ", ") + ("\"" + n + "\": {\"size\": " + std::to_string(size) + "}"); }
    void mem(const std::string &e) { memory += (memory.empty() ? "" : ", ") + e; }
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    std::string name(const void *p, const char *prefix, unsigned size)
    {
        auto it = names.find(p);
        if (it != names.end()) return it->second;
        const std::string n = prefix + std::to_string(names.size());
        names[p] = n;
        buffer(n, size);
        return n;
    }
    void pointer(const std::string &at, const std::string &to) { mem("{\"addr\": \"" + at + "\", \"pointer\": \"" + to + "\"}"); }
    void raw(const std::string &at, const void *p, size_t n) { mem("{\"addr\": \"" + at + "\", \"bytes\": \"" + hexbytes(p, n) + "\"}"); }
    void target(const std::string &vt, unsigned off, void *fn)
    {
        mem("{\"addr\": \"buf:" + vt + "+" + std::to_string(off) + "\", \"guest\": \"" + addr(guest_address(fn)) + "\"}");
    }
};

void record_list(Capture &cap, unsigned char *self)
{
    unsigned char *sentinel = get<unsigned char *>(self, 0x10);
    const std::string s = cap.name(sentinel, "node", 0x18);
    cap.pointer("buf:self+16", s);
    unsigned char *n = sentinel;
    int guard = 0;
    do {
        unsigned char *next = get<unsigned char *>(n, 0x0);
        const std::string a = cap.name(n, "node", 0x18), b = cap.name(next, "node", 0x18);
        cap.pointer("buf:" + a + "+0", b);
        cap.pointer("buf:" + b + "+8", a);
        if (next != sentinel) {
            void *object = get<void *>(next, 0x10);
            if (object) {
                const std::string o = cap.name(object, "obj", 16);
                cap.pointer("buf:" + b + "+16", o);
                cap.raw("buf:" + o + "+8", static_cast<unsigned char *>(object) + 8, 4);
                const std::string vt = o + "_vt";
                cap.buffer(vt, 8);
                cap.pointer("buf:" + o, vt);
                cap.target(vt, 0, slot(object, 0));
            } else {
                cap.raw("buf:" + b + "+16", &object, 8);
            }
        }
        n = next;
    } while (n != sentinel && ++guard < 256);
    if (guard >= 256) cap.number = -1;   // a list too long to record
}

}  // namespace

extern "C" void bb_render_frame_update_02377bd0(unsigned char *self, void *step)
{
    Capture cap;
    cap.number = rt_capture_begin("render_frame_update_02377bd0");
    const bool uncapped = frame_timing::target_fps() == frame_timing::Target::uncapped;

    if (void *r9 = get<void *>(self, 0x38)) {
        void *r8 = get<void *>(self, 0x30);
        rt::fn<Prepare>(list_prepare)(self + 0x8, get<void *>(self, 0x10), self + 0x28, *static_cast<void **>(r8), r8, r9);
        if (cap.number >= 0) {
            cap.buffer("prep30", 8);
            cap.buffer("prep38", 8);
            cap.pointer("buf:self+48", "prep30");
            cap.pointer("buf:self+56", "prep38");
            cap.raw("buf:prep30", r8, 8);
            cap.stub("{\"address\": \"0x02379d60\", \"argc\": 6}");
        }
    } else if (cap.number >= 0) {
        cap.mem("{\"addr\": \"buf:self+56\", \"bytes\": \"0000000000000000\"}");
    }
    if (cap.number >= 0) {
        record_list(cap, self);
        void *allocator = get<void *>(self, 0x20);
        cap.buffer("allocator", 8);
        cap.buffer("allocator_vt", 0x78);
        cap.pointer("buf:self+32", "allocator");
        cap.pointer("buf:allocator", "allocator_vt");
        cap.target("allocator_vt", 0x70, slot(allocator, 0x70));
        uint64_t count = get<uint64_t>(self, 0x18);
        cap.raw("buf:self+24", &count, 8);
        // The flipper's measured frame time: read only with BB_TARGET_FPS=uncapped, recorded
        // always so the option can be checked on real frames.
        if (unsigned char *flipper = *rt::ptr<unsigned char *>(flipper_singleton)) {
            cap.buffer("flipper", 0x2c8);
            cap.pointer(addr(flipper_singleton), "flipper");
            cap.raw("buf:flipper+612", flipper + 0x264, 4);
        }
    }

    unsigned char *node = get<unsigned char *>(get<unsigned char *>(self, 0x10), 0x0);
    while (node != get<unsigned char *>(self, 0x10)) {
        unsigned char *next;
        void *object = get<void *>(node, 0x10);
        bool remove = true;
        if (object) {
            const uint8_t alive = rt::fn<Update>(update_object)(object, step);
            if (cap.number >= 0) cap.stub("{\"address\": \"0x02373850\", \"argc\": 2, \"ret\": " + std::to_string(alive) + "}");
            object = get<void *>(node, 0x10);
            if (alive) {
                remove = object == nullptr;
            } else {
                const int32_t old = __atomic_fetch_add(reinterpret_cast<int32_t *>(static_cast<unsigned char *>(object) + 8), -1,
                                                       __ATOMIC_SEQ_CST);
                if (old == 1) {
                    void *destroy = slot(object, 0);
                    reinterpret_cast<Destroy *>(destroy)(object);
                    if (cap.number >= 0) cap.stub("{\"address\": \"" + addr(guest_address(destroy)) + "\", \"argc\": 1}");
                } else if (old <= 0) {
                    rt::fn<Fatal>(fatal_error)(nullptr, 0x3e, rt::ptr<const char>(str_bad_unref));
                    if (cap.number >= 0) cap.stub("{\"address\": \"0x024b55b0\", \"argc\": 3}");
                }
                put<void *>(node, 0x10, nullptr);
            }
        }
        next = get<unsigned char *>(node, 0x0);
        if (remove && node != get<unsigned char *>(self, 0x10)) {
            unsigned char *prev = get<unsigned char *>(node, 0x8);
            put<unsigned char *>(prev, 0x0, next);
            put<unsigned char *>(get<unsigned char *>(node, 0x0), 0x8, get<unsigned char *>(node, 0x8));
            void *allocator = get<void *>(self, 0x20);
            void *free_fn = slot(allocator, 0x70);
            reinterpret_cast<Free *>(free_fn)(allocator, node);
            if (cap.number >= 0) cap.stub("{\"address\": \"" + addr(guest_address(free_fn)) + "\", \"argc\": 2}");
            put<uint64_t>(self, 0x18, get<uint64_t>(self, 0x18) - 1);
        }
        node = next;
    }

    if (unsigned char *child80 = get<unsigned char *>(self, 0x80)) {
        unsigned char *rendman = *rt::ptr<unsigned char *>(rendman_singleton);
        if (!rendman && !uncapped)
            rt::fn<Fatal>(fatal_error)(rt::ptr<const char>(str_singleton_header), 0xb1, rt::ptr<const char>(str_singleton_func),
                                       rt::ptr<const char>(str_rendman));
        void *field = get<void *>(rendman, 0x28);
        rt::fn<Child80>(update_child80)(child80, step, field);
        if (cap.number >= 0) {
            cap.buffer("child80", 8);
            cap.buffer("rendman", 0x30);
            cap.pointer("buf:self+128", "child80");
            cap.pointer(addr(rendman_singleton), "rendman");
            cap.raw("buf:rendman+40", &field, 8);
            cap.stub("{\"address\": \"0x0236e000\", \"argc\": 3}");
        }
    } else if (cap.number >= 0) {
        cap.mem("{\"addr\": \"buf:self+128\", \"bytes\": \"0000000000000000\"}");
    }

    if (unsigned char *child70 = get<unsigned char *>(self, 0x70)) {
        child70[0x20] = self[0x98];
        void *fn = slot(child70, 0x40);
        const uint32_t held = get<uint32_t>(self, 0x8c);
        float frame_time;
        if (uncapped)
            frame_time = get<float>(*rt::ptr<unsigned char *>(flipper_singleton), 0x264);
        else
            frame_time = held != 0 ? 0.0f : *rt::ptr<float>(k_frame_time);
        reinterpret_cast<Step *>(fn)(child70, frame_time);
        put<uint32_t>(self, 0x8c, 0);
        if (cap.number >= 0) {
            cap.buffer("child70", 0x28);
            cap.buffer("child70_vt", 0x48);
            cap.pointer("buf:self+112", "child70");
            cap.pointer("buf:child70", "child70_vt");
            cap.target("child70_vt", 0x40, fn);
            cap.raw("buf:self+140", &held, 4);
            cap.raw("buf:self+152", self + 0x98, 1);
            cap.stub("{\"address\": \"" + addr(guest_address(fn)) + "\", \"argc\": 1, \"argf32\": [0]}");
        }
    } else if (cap.number >= 0) {
        cap.mem("{\"addr\": \"buf:self+112\", \"bytes\": \"0000000000000000\"}");
    }

    if (cap.number >= 0) {
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x02377bd0\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:self\", \"rsi\": \"buf:step\"}, ", cap.number);
        const std::string json = std::string(head) + "\"buffers\": {\"self\": {\"size\": 160}, \"step\": {\"size\": 8}, " +
                                 cap.buffers + "}, \"memory\": [" + cap.memory + "], \"stubs\": [" + cap.stubs + "], \"imports\": []}";
        rt_capture_write("render_frame_update_02377bd0", cap.number, json.c_str());
    }
}
