// SPDX-License-Identifier: GPL-2.0-or-later
// AI: the Havok AI manager's per-frame update (SprjHkAiManager, singleton 0x059401a0).
//
// ai_hk_frame_update_0222bc10 (0x0222bc10; called by two world updates) takes the owner (this),
// a frame/camera object and two flags:
// - stores the frame object at +0x2cb0; with the first flag, resets each of the +0x2bc8 groups
//   at +0x2bd0 (0x02224090(group, 1, 0));
// - steps the AI world with a time-step descriptor of 1/30 s (0x021cdad0(manager->+0x10, step));
// - with the second flag only, the rest: copies the frame object's matrices (0x02116830) to the
//   manager's +0x20 object; when +0x2c18 and +0x2c20 are set, passes a position (the frame
//   object's, or the player character's when WorldChrMan 0x0593e878 exists) to 0x016334b0,
//   updates +0x2c18 (0x01638ad0) and marks debug objects (+0x740 |= 2) by the flags at +0x2c91..
//   +0x2c93 and a distance map (+0x2c78, below +0x2c6c); runs a draw descriptor (callback
//   0x02232a60, flags +0x2c94..+0x2c98, +0x2c9c) over every AI object of the groups
//   (0x022211b0) or over the manager (0x021d6ea0) when +0x2c90 is set; then draws through the
//   render manager (RendMan 0x05940298): the debug render state is reset to its defaults, one
//   axis frame per (id, index) pair at +0x2cb8 (+0x2ec0 of them) found in the manager's id map
//   (0x029ae440 gives the point, 0x0135d810 draws it), and a box (0x02dadec0) for modes 3 and 2
//   at +0x2c68; last, 0x0163bb50(+0x2c18).
// Missing singletons go to the engine's fatal error with the singleton's name; the code goes on
// with the singleton read again, as the original does.
//
// "60 FPS++" (and "90 FPS++", "60FPS (no deltatime)") step the AI world by 1/60 s; BB_TARGET_FPS=60
// does too.
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t group_reset = 0x02224090, world_step = 0x021cdad0, copy_matrices = 0x02116830,
                   set_position = 0x016334b0, debug_update = 0x01638ad0, debug_flush = 0x01633ac0,
                   draw_object = 0x022211b0, draw_manager = 0x021d6ea0, render_begin = 0x021d34b0,
                   point_of = 0x029ae440, draw_axes = 0x0135d810, draw_box = 0x02dadec0, debug_end = 0x0163bb50;
constexpr uint32_t fatal_error = 0x024b55b0;
constexpr uint32_t ai_manager = 0x059401a0, world_chr_man = 0x0593e878, world_chr_man_dbg = 0x0593e880,
                   rend_man = 0x05940298;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd, str_ai_manager = 0x04d3ad5d,
                   str_world_chr_dbg = 0x04d35c4a, str_rend_man = 0x04d3b1e1;
constexpr uint32_t step_vtable = 0x056efd30;   // + 0x10
constexpr uint32_t draw_callback = 0x02232a60;
constexpr uint32_t k_origin = 0x04d17ba0, k_scale = 0x04d17bb0, k_desc_1 = 0x04d17bc0, k_desc_2 = 0x04d17bd0,
                   k_desc_0 = 0x04d17be0, k_state = 0x04d17bf0, k_w_one = 0x04d17c00, k_row_1 = 0x04d17c10,
                   k_row_2 = 0x04d17c20, k_box = 0x04d17c30, k_state_box = 0x04d17c40, k_row_0 = 0x04d28fcc;

template <typename T> T get(const void *p, unsigned off) { T v; std::memcpy(&v, static_cast<const char *>(p) + off, sizeof v); return v; }
template <typename T> void put(void *p, unsigned off, T v) { std::memcpy(static_cast<char *>(p) + off, &v, sizeof v); }
unsigned char *ptr_at(const void *p, unsigned off) { return get<unsigned char *>(p, off); }
__m128 vec(const void *p, unsigned off) { return _mm_load_ps(reinterpret_cast<const float *>(static_cast<const char *>(p) + off)); }
__m128 constant(uint32_t address) { return _mm_load_ps(rt::ptr<float>(address)); }
__attribute__((target("sse4.1"))) __m128 with_w_of(__m128 a, __m128 b) { return _mm_blend_ps(a, b, 8); }
void store(void *p, unsigned off, __m128 v) { _mm_store_ps(reinterpret_cast<float *>(static_cast<char *>(p) + off), v); }

using Fatal = void(const char *, int, const char *, const char *, ...);
using Void1 = void(void *);
using Void2 = void(void *, void *);
using Void3 = void(void *, void *, void *);
using GroupReset = void(void *, int, int);
using SetPosition = void(void *, void *, void *, int);
using PointOf = void(void *, int32_t, void *);
using DrawBox = void(void *, void *, void *, float);

struct Step {
    void *vtable;
    uint32_t seconds_bits;
};

// The draw descriptor the original builds on its stack (0x53 bytes used).
struct alignas(16) DrawDescriptor {
    float v0[4], v1[4], v2[4], v3[4];
    void *callback;
    uint32_t value;
    uint8_t on_a, on_b;
    uint8_t flags[5];
};
static_assert(offsetof(DrawDescriptor, callback) == 0x40 && offsetof(DrawDescriptor, flags) == 0x4e);

std::string hex(const void *p, size_t n)
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

std::string guest_hex(uint32_t a)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", a);
    return b;
}

// Recording: every object the function reads becomes a buffer, sized by the furthest field read;
// each field (address and length) is recorded once, with the value it had when first read.
struct Recorder {
    int number = -1;
    struct Buf {
        std::string name;
        unsigned size;
    };
    std::map<const void *, Buf> bufs;
    std::set<std::pair<std::string, unsigned>> seen;
    std::string memory, stubs, imports;
    bool on() const { return number >= 0; }
    std::string name(const void *base, unsigned extent = 16)
    {
        auto it = bufs.find(base);
        if (it == bufs.end()) it = bufs.emplace(base, Buf{"b" + std::to_string(bufs.size()), 16}).first;
        if (extent > it->second.size) it->second.size = (extent + 15) & ~15u;
        return it->second.name;
    }
    void entry(const std::string &addr, unsigned n, const std::string &rest)
    {
        if (!seen.insert({addr, n}).second) return;
        memory += (memory.empty() ? "" : ", ") + ("{\"addr\": \"" + addr + "\", " + rest + "}");
    }
    std::string at(const void *base, unsigned off, unsigned n) { return "buf:" + name(base, off + n) + "+" + std::to_string(off); }
    void bytes(const void *base, unsigned off, unsigned n)
    {
        const std::string a = at(base, off, n);
        entry(a, n, "\"bytes\": \"" + hex(static_cast<const char *>(base) + off, n) + "\"");
    }
    // A pointer field: to another recorded object (when not null), else its raw bytes.
    void pointer(const void *base, unsigned off)
    {
        const void *target = get<const void *>(base, off);
        if (!target) return bytes(base, off, 8);
        const std::string a = at(base, off, 8);
        entry(a, 8, "\"pointer\": \"" + name(target) + "\"");
    }
    void global_bytes(uint32_t address, unsigned n) { entry(guest_hex(address), n, "\"bytes\": \"" + hex(rt::ptr<void>(address), n) + "\""); }
    void global_pointer(uint32_t address)
    {
        const void *target = *rt::ptr<const void *>(address);
        if (!target) return global_bytes(address, 8);
        entry(guest_hex(address), 8, "\"pointer\": \"" + name(target) + "\"");
    }
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    std::string buffers() const
    {
        std::string s;
        for (const auto &b : bufs) s += (s.empty() ? "" : ", ") + ("\"" + b.second.name + "\": {\"size\": " + std::to_string(b.second.size) + "}");
        return s;
    }
};

unsigned char *singleton(uint32_t address, uint32_t str_name, Recorder &rec)
{
    unsigned char *v = *rt::ptr<unsigned char *>(address);
    if (rec.on()) rec.global_pointer(address);
    if (!v) {
        rt::fn<Fatal>(fatal_error)(rt::ptr<char>(str_singleton_header), 0xb1, rt::ptr<char>(str_singleton_func), rt::ptr<char>(str_name));
        if (rec.on()) rec.stub("{\"address\": \"0x024b55b0\", \"argc\": 4}");
        v = *rt::ptr<unsigned char *>(address);
    }
    return v;
}

// The debug render state reset (the original repeats it for each drawing).
unsigned char *reset_render_state(unsigned char *rend, uint32_t k_vector, Recorder &rec)
{
    const int32_t index = get<int32_t>(rend, 0x20);
    unsigned char *slot = ptr_at(rend, 0x10 + 8 * index);
    unsigned char *state = ptr_at(slot, 0x40);
    if (rec.on()) {
        rec.bytes(rend, 0x20, 4);
        rec.pointer(rend, 0x10 + 8 * index);
        rec.pointer(slot, 0x40);
        rec.bytes(state, 0x8, 1);
        rec.bytes(state, 0x18, 8);
        rec.bytes(state, 0x20, 32);
    }
    if (get<uint32_t>(state, 0x18) != 0) {
        put<uint32_t>(state, 0x18, 0);
        state[0x8] |= 4;
    }
    if (get<uint32_t>(state, 0x1c) != 1) {
        put<uint32_t>(state, 0x1c, 1);
        state[0x8] |= 8;
    }
    for (unsigned off : {0x20u, 0x30u}) {
        if (_mm_movemask_ps(_mm_cmpeq_ps(vec(state, off), constant(k_vector))) != 0xf) {
            store(state, off, constant(k_vector));
            state[0x8] |= off == 0x20 ? 0x10 : 0x20;
        }
    }
    return state;
}

}  // namespace

extern "C" void bb_ai_hk_frame_update_0222bc10(unsigned char *self, unsigned char *frame, uint64_t reset_groups,
                                               uint64_t full)
{
    Recorder rec;
    rec.number = rt_capture_begin("ai_hk_frame_update_0222bc10");
    if (rec.on()) {
        rec.name(self);
        if (frame) rec.name(frame);
    }
    put<unsigned char *>(self, 0x2cb0, frame);

    if (static_cast<uint8_t>(reset_groups)) {
        if (rec.on()) rec.bytes(self, 0x2bc8, 4);
        if (get<int32_t>(self, 0x2bc8) > 0) {
            int32_t i = 0;
            uint64_t off = 0;
            do {
                if (rec.on()) rec.pointer(self, 0x2bd0);
                rt::fn<GroupReset>(group_reset)(ptr_at(self, 0x2bd0) + off, 1, 0);
                if (rec.on()) rec.stub("{\"address\": \"0x02224090\", \"argc\": 3}");
                i++;
                off += 0x20;
            } while (i < get<int32_t>(self, 0x2bc8));
        }
    }

    unsigned char *manager = singleton(ai_manager, str_ai_manager, rec);
    Step step{rt::ptr<unsigned char>(step_vtable) + 0x10, frame_timing::fixed_step_bits()};
    if (rec.on()) rec.pointer(manager, 0x10);
    rt::fn<Void2>(world_step)(ptr_at(manager, 0x10), &step);
    if (rec.on()) rec.stub("{\"address\": \"0x021cdad0\", \"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 12}]}");

    auto finish = [&] {
        if (!rec.on()) return;
        char head[200];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x0222bc10\", \"id\": \"capture_%04d\", \"returns\": \"void\", \"args\": {", rec.number);
        std::string json = std::string(head) + "\"rdi\": \"buf:" + rec.name(self) + "\", \"rsi\": \"" +
                           (frame ? "buf:" + rec.name(frame) : std::string("0x0")) + "\", \"rdx\": \"" +
                           std::to_string(static_cast<uint8_t>(reset_groups)) + "\", \"rcx\": \"" +
                           std::to_string(static_cast<uint8_t>(full)) + "\"}, \"buffers\": {" + rec.buffers() +
                           "}, \"memory\": [" + rec.memory + "], \"stubs\": [" + rec.stubs + "], \"imports\": []}";
        rt_capture_write("ai_hk_frame_update_0222bc10", rec.number, json.c_str());
    };
    if (!static_cast<uint8_t>(full)) return finish();

    // The frame object's matrices to the manager's +0x20 object.
    manager = singleton(ai_manager, str_ai_manager, rec);
    unsigned char *view = ptr_at(manager, 0x20);
    if (rec.on()) rec.pointer(manager, 0x20);
    if (view && frame) {
        alignas(16) unsigned char tmp[0xa0];
        for (unsigned i = 0; i < 4; i++) {
            if (rec.on()) rec.bytes(frame, 0x10 + 16 * i, 16);
            store(tmp, 0x60 + 16 * i, vec(frame, 0x10 + 16 * i));
        }
        rt::fn<Void2>(copy_matrices)(tmp, frame);
        if (rec.on())
            rec.stub("{\"address\": \"0x02116830\", \"argc\": 2, \"writes\": [{\"arg\": 0, \"bytes\": \"" + hex(tmp, 0xa0) + "\"}]}");
        store(view, 0x10, vec(tmp, 0x90));
        for (unsigned i = 0; i < 6; i++) store(view, 0x20 + 16 * i, vec(tmp, 16 * i));
        if (rec.on()) rec.name(view, 0x80);
    }

    // Debug display: a position, then marks.
    if (rec.on()) {
        rec.pointer(self, 0x2c18);
        if (ptr_at(self, 0x2c18)) rec.pointer(self, 0x2c20);
    }
    if (ptr_at(self, 0x2c18) && ptr_at(self, 0x2c20)) {
        alignas(16) unsigned char position[16];
        unsigned char *chr_man = *rt::ptr<unsigned char *>(world_chr_man);
        if (rec.on()) rec.global_pointer(world_chr_man);
        if (!chr_man) {
            unsigned char *f = ptr_at(self, 0x2cb0);
            if (f) {
                if (rec.on()) {
                    rec.bytes(f, 0x30, 16);
                    rec.bytes(f, 0x40, 16);
                }
                store(position, 0, _mm_add_ps(_mm_mul_ps(vec(f, 0x30), constant(k_scale)), vec(f, 0x40)));
            } else {
                store(position, 0, constant(k_origin));
            }
        } else {
            unsigned char *dbg = singleton(world_chr_man_dbg, str_world_chr_dbg, rec);
            unsigned char *chr = ptr_at(dbg, 0x108);
            if (rec.on()) rec.pointer(dbg, 0x108);
            if (!chr) {
                chr = ptr_at(chr_man, 0x60);
                if (rec.on()) rec.pointer(chr_man, 0x60);
            }
            if (!chr) {
                store(position, 0, constant(k_origin));
            } else {
                unsigned char *p1 = ptr_at(chr, 0x58), *p2 = ptr_at(p1, 0x8), *p3 = ptr_at(p2, 0x3b0), *p4 = ptr_at(p3, 0x68);
                if (rec.on()) {
                    rec.pointer(chr, 0x58);
                    rec.pointer(p1, 0x8);
                    rec.pointer(p2, 0x3b0);
                    rec.pointer(p3, 0x68);
                    rec.bytes(p4, 0x1e0, 16);
                }
                store(position, 0, vec(p4, 0x1e0));
            }
        }
        rt::fn<SetPosition>(set_position)(ptr_at(self, 0x2c20), position, position, 1);
        if (rec.on()) rec.stub("{\"address\": \"0x016334b0\", \"argc\": 4, \"argmem\": [{\"arg\": 1, \"size\": 16}]}");
        rt::fn<Void1>(debug_update)(ptr_at(self, 0x2c18));
        if (rec.on()) {
            rec.stub("{\"address\": \"0x01638ad0\", \"argc\": 1}");
            rec.bytes(self, 0x2c90, 4);
        }
        if (!self[0x2c90]) {
            if (self[0x2c91] || self[0x2c92]) {
                unsigned char *display = ptr_at(self, 0x2c20);
                unsigned char *object = ptr_at(display, 0x8);
                if (rec.on()) {
                    rec.pointer(display, 0x8);
                    if (object) rec.bytes(object, 0x740, 1);
                }
                if (object) object[0x740] |= 2;
                if (self[0x2c92]) {
                    rt::fn<Void1>(debug_flush)(ptr_at(self, 0x2c20));
                    if (rec.on()) rec.stub("{\"address\": \"0x01633ac0\", \"argc\": 1}");
                }
            }
            if (self[0x2c93]) {
                // In-order walk of the distance map (MSVC tree: left +0x0, parent +0x8,
                // right +0x10, nil flag +0x19, object +0x20, distance +0x28).
                unsigned char *head = ptr_at(self, 0x2c78);
                unsigned char *node = ptr_at(head, 0x0);
                if (rec.on()) {
                    rec.pointer(self, 0x2c78);
                    rec.pointer(head, 0x0);
                }
                while (node != head) {
                    for (;;) {
                        unsigned char *object = ptr_at(node, 0x20);
                        if (rec.on()) rec.pointer(node, 0x20);
                        if (object) {
                            if (rec.on()) {
                                rec.bytes(self, 0x2c6c, 4);
                                rec.bytes(node, 0x28, 4);
                            }
                            if (_mm_comigt_ss(_mm_set_ss(get<float>(self, 0x2c6c)), _mm_set_ss(get<float>(node, 0x28)))) {
                                if (rec.on()) rec.bytes(object, 0x740, 1);
                                object[0x740] |= 2;
                            }
                        }
                        if (rec.on()) rec.bytes(node, 0x19, 1);
                        if (!node[0x19]) break;
                    }
                    unsigned char *next;
                    unsigned char *right = ptr_at(node, 0x10);
                    if (rec.on()) {
                        rec.pointer(node, 0x10);
                        rec.bytes(right, 0x19, 1);
                    }
                    if (!right[0x19]) {
                        do {
                            next = right;
                            right = ptr_at(right, 0x0);
                            if (rec.on()) {
                                rec.pointer(next, 0x0);
                                rec.bytes(right, 0x19, 1);
                            }
                        } while (!right[0x19]);
                    } else {
                        for (;;) {
                            next = ptr_at(node, 0x8);
                            if (rec.on()) {
                                rec.pointer(node, 0x8);
                                rec.bytes(next, 0x19, 1);
                            }
                            if (next[0x19]) break;
                            if (rec.on()) rec.pointer(next, 0x10);
                            const bool from_right = node == ptr_at(next, 0x10);
                            node = next;
                            if (!from_right) break;
                        }
                    }
                    node = next;
                }
            }
        }
    }

    // The draw descriptor over the AI objects.
    DrawDescriptor desc;
    store(&desc, 0x10, constant(k_origin));
    store(&desc, 0x20, constant(k_desc_1));
    store(&desc, 0x30, constant(k_desc_2));
    desc.on_a = 1;
    desc.on_b = 1;
    store(&desc, 0x00, constant(k_desc_0));
    desc.callback = rt::ptr<void>(draw_callback);
    if (rec.on()) rec.bytes(self, 0x2c90, 13);
    desc.value = get<uint32_t>(self, 0x2c9c);
    desc.flags[1] = self[0x2c95];
    desc.flags[0] = self[0x2c94];
    desc.flags[2] = self[0x2c96];
    desc.flags[3] = self[0x2c97];
    desc.flags[4] = self[0x2c98];
    const std::string desc_arg = "\"argmem\": [{\"arg\": 1, \"size\": 83}]";
    if (self[0x2c90]) {
        unsigned char *m = *rt::ptr<unsigned char *>(ai_manager);
        if (rec.on()) rec.global_pointer(ai_manager);
        unsigned char *world;
        if (m) {
            world = ptr_at(m, 0x10);
        } else {
            rt::fn<Fatal>(fatal_error)(rt::ptr<char>(str_singleton_header), 0xb1, rt::ptr<char>(str_singleton_func), rt::ptr<char>(str_ai_manager));
            if (rec.on()) rec.stub("{\"address\": \"0x024b55b0\", \"argc\": 4}");
            m = *rt::ptr<unsigned char *>(ai_manager);
            world = *reinterpret_cast<unsigned char *volatile *>(m + 0x10);   // read before the check, as the original
            if (!m) {
                rt::fn<Fatal>(fatal_error)(rt::ptr<char>(str_singleton_header), 0xb1, rt::ptr<char>(str_singleton_func), rt::ptr<char>(str_ai_manager));
                if (rec.on()) rec.stub("{\"address\": \"0x024b55b0\", \"argc\": 4}");
                m = *rt::ptr<unsigned char *>(ai_manager);
            }
        }
        if (rec.on()) {
            rec.pointer(m, 0x10);
            rec.pointer(m, 0x20);
        }
        rt::fn<Void3>(draw_manager)(ptr_at(m, 0x20), world, &desc);
        if (rec.on()) rec.stub("{\"address\": \"0x021d6ea0\", \"argc\": 3, \"argmem\": [{\"arg\": 2, \"size\": 83}]}");
    } else {
        if (rec.on()) rec.bytes(self, 0x2bc8, 4);
        int32_t groups = get<int32_t>(self, 0x2bc8);
        for (int64_t i = 0; groups > 0;) {
            unsigned char *base = ptr_at(self, 0x2bd0);
            if (rec.on()) {
                rec.pointer(self, 0x2bd0);
                rec.bytes(base, 32 * i + 8, 4);
            }
            int32_t count = get<int32_t>(base, 32 * i + 8);
            if (count > 0) {
                int64_t j = 0;
                do {
                    unsigned char *elements = ptr_at(base, 32 * i + 0x10);
                    if (rec.on()) {
                        rec.pointer(base, 32 * i + 0x10);
                        rec.bytes(elements, 0xb0 * j + 0x58, 4);
                    }
                    const int32_t last = get<int32_t>(elements, 0xb0 * j + 0x58) - 1;
                    if (last >= 0) {
                        int64_t k = last;
                        do {
                            unsigned char *list = ptr_at(elements, 0xb0 * j + 0x60);
                            if (rec.on()) {
                                rec.pointer(elements, 0xb0 * j + 0x60);
                                rec.pointer(list, 8 * k);
                            }
                            void *object = get<void *>(list, 8 * k);
                            if (object) {
                                rt::fn<Void2>(draw_object)(object, &desc);
                                if (rec.on()) rec.stub("{\"address\": \"0x022211b0\", \"argc\": 2, " + desc_arg + "}");
                            }
                            k--;
                        } while (static_cast<int32_t>(k) >= 0);
                        count = get<int32_t>(base, 32 * i + 8);
                    }
                    j++;
                } while (static_cast<int32_t>(j) < count);
                if (rec.on()) rec.bytes(self, 0x2bc8, 4);
                groups = get<int32_t>(self, 0x2bc8);
            }
            i++;
            if (static_cast<int32_t>(i) >= groups) break;
        }
    }

    // Drawing through the render manager.
    manager = singleton(ai_manager, str_ai_manager, rec);
    unsigned char *world = ptr_at(manager, 0x10);
    unsigned char *rend = singleton(rend_man, str_rend_man, rec);
    if (rec.on()) rec.pointer(rend, 0x20);
    rt::fn<Void2>(render_begin)(world, ptr_at(rend, 0x20));
    if (rec.on()) {
        rec.stub("{\"address\": \"0x021d34b0\", \"argc\": 2}");
        rec.bytes(self, 0x2ec0, 8);
    }
    if (get<uint64_t>(self, 0x2ec0) != 0) {
        rend = singleton(rend_man, str_rend_man, rec);
        unsigned char *renderer = ptr_at(rend, 0x20);
        if (rec.on()) rec.pointer(rend, 0x20);
        reset_render_state(renderer, k_state, rec);
        const uint64_t pairs = get<uint64_t>(self, 0x2ec0);
        if (pairs) {
            const uint32_t low = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) + 0x2cb8);
            const unsigned first = 0x2cb8 + ((0u - low) & 3);
            uint64_t n = 0;
            do {
                if (rec.on()) rec.bytes(self, first + 8 * n, 8);
                const uint32_t id = get<uint32_t>(self, first + 8 * n);
                const int32_t index = get<int32_t>(self, first + 8 * n + 4);
                if (id != 0xffffffffu && index >= 0) {
                    unsigned char *m = singleton(ai_manager, str_ai_manager, rec);
                    unsigned char *w = ptr_at(m, 0x10);
                    unsigned char *head = ptr_at(w, 0x10);
                    if (rec.on()) {
                        rec.pointer(m, 0x10);
                        rec.pointer(w, 0x10);
                    }
                    // lower_bound(id) in the id map (key +0x20, unsigned; value +0x28).
                    unsigned char *found = head;
                    unsigned char *link = head;
                    unsigned link_off = 0x8;
                    for (;;) {
                        unsigned char *node = ptr_at(link, link_off);
                        if (rec.on()) {
                            rec.pointer(link, link_off);
                            rec.bytes(node, 0x19, 1);
                        }
                        if (node[0x19]) break;
                        if (rec.on()) rec.bytes(node, 0x20, 4);
                        if (get<uint32_t>(node, 0x20) < id) {
                            link = node;
                            link_off = 0x10;
                        } else {
                            link = node;
                            link_off = 0x0;
                            found = node;
                        }
                    }
                    if (found != head) {
                        if (rec.on()) rec.bytes(found, 0x20, 4);
                        unsigned char *match = get<uint32_t>(found, 0x20) > id ? head : found;
                        if (match != head) {
                            unsigned char *entity = ptr_at(match, 0x28);
                            if (rec.on()) rec.pointer(match, 0x28);
                            unsigned char *a = entity ? ptr_at(entity, 0xb8) : nullptr;
                            if (rec.on() && entity) rec.pointer(entity, 0xb8);
                            if (a) {
                                unsigned char *b = ptr_at(a, 0x50);
                                unsigned char *model = ptr_at(b, 0x28);
                                if (rec.on()) {
                                    rec.pointer(a, 0x50);
                                    rec.pointer(b, 0x28);
                                    rec.bytes(model, 0x18, 4);
                                    rec.bytes(model, 0x118, 4);
                                }
                                __m128 point = constant(k_origin);
                                alignas(16) unsigned char rows[48];
                                const int32_t total = static_cast<int32_t>(get<uint32_t>(model, 0x118) + get<uint32_t>(model, 0x18));
                                if (total > index) {
                                    rt::fn<PointOf>(point_of)(model, index, rows);
                                    if (rec.on())
                                        rec.stub("{\"address\": \"0x029ae440\", \"argc\": 3, \"writes\": [{\"arg\": 2, \"bytes\": \"" +
                                                 hex(rows, 16) + "\"}]}");
                                    point = with_w_of(vec(rows, 0), constant(k_w_one));
                                    store(rows, 0, point);
                                }
                                const __m128 zero = _mm_setzero_ps();
                                const __m128 r0 = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(point), 12));
                                const __m128 r1 = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(_mm_shuffle_ps(point, zero, 0x21)), 12));
                                const __m128 r2 = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(_mm_movehl_ps(zero, point)), 12));
                                store(rows, 0x00, _mm_add_ps(r0, _mm_load_ss(rt::ptr<float>(k_row_0))));
                                store(rows, 0x10, _mm_add_ps(r1, constant(k_row_1)));
                                store(rows, 0x20, _mm_add_ps(r2, constant(k_row_2)));
                                rt::fn<Void2>(draw_axes)(renderer, rows);
                                if (rec.on()) rec.stub("{\"address\": \"0x0135d810\", \"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 48}]}");
                            }
                        }
                    }
                }
                n++;
            } while (n != pairs);
        }
    }

    if (rec.on()) rec.bytes(self, 0x2c68, 4);
    const int32_t mode = get<int32_t>(self, 0x2c68);
    if (mode == 3 || mode == 2) {
        rend = singleton(rend_man, str_rend_man, rec);
        unsigned char *renderer = ptr_at(rend, 0x20);
        if (rec.on()) {
            rec.pointer(rend, 0x20);
            rec.bytes(self, 0x2c30, 16);
            rec.bytes(self, mode == 3 ? 0x2c50 : 0x2c40, 16);
            rec.bytes(self, 0x2c60, 4);
        }
        reset_render_state(renderer, mode == 3 ? k_state : k_state_box, rec);
        alignas(16) unsigned char lo[16], hi[16];
        store(lo, 0, _mm_add_ps(constant(k_box), vec(self, 0x2c30)));
        store(hi, 0, _mm_add_ps(constant(k_box), vec(self, mode == 3 ? 0x2c50 : 0x2c40)));
        rt::fn<DrawBox>(draw_box)(renderer, lo, hi, get<float>(self, 0x2c60));
        if (rec.on()) rec.stub("{\"address\": \"0x02dadec0\", \"argc\": 3, \"argf32\": [0], \"argmem\": [{\"arg\": 1, \"size\": 16}, {\"arg\": 2, \"size\": 16}]}");
    }
    if (ptr_at(self, 0x2c18)) {
        rt::fn<Void1>(debug_end)(ptr_at(self, 0x2c18));
        if (rec.on()) rec.stub("{\"address\": \"0x0163bb50\", \"argc\": 1}");
    }
    finish();
}
