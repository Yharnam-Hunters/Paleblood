// SPDX-License-Identifier: GPL-2.0-or-later
// Physics: the constructor of a physics instance (vtable 0x0578d440) over a set of bodies in the
// physics world.
//
// physics_instance_construct_01c0c2b0 (0x01c0c2b0; called by two creation paths) fills the object
// at `self` from its arguments (ids, owner pointers, an allocator, an offset vector) and builds:
// - the body set (0x00ae8700 on a block from the thread's allocator), moving each active body
//   (flags +0x40 & 3 in the world's body table) by the offset vector (0x00f67880);
// - a vector of ids (0x02bc1ca0 reserves 0x20) and per-body and per-constraint back-links (16 bytes
//   each, from the given allocator) that the world's body and constraint tables point at;
// - a copy of the set's shape table (0x30 bytes per entry, through the heap object 0x058018b0)
//   handed to a shape object (0x00ac46a0), a controller (0x00ae3fc0) whose frame step is 1/30 s
//   and whose transform comes from the set's root body (0x0083d6d0), and two zeroed flag arrays;
// - unless the physics manager (0x0593d700) is in one of its two special modes: each active body's
//   four 16-bit motion values rescaled (x 0.2, the last one unscaled, then x 1.00390625, kept in
//   16 bits), the world's listeners told about each body (the list at world +0x538: listener
//   +0x18, or removed through +0x8 when flagged), and 0x00ae6180(1.5, root);
// - a listener object of its own (vtable 0x0578d1b0, mode 0x2f), a small object pair
//   (0x0578d140 / 0x0578d180) and a tracker (0x00ae6d00); last, it collects the distinct motion
//   types above 4 of its bodies into the id vector (0x02bc1bb0).
//
// "60 FPS++" (and "90 FPS++", "60FPS (no deltatime)") make the controller's step 1/60 s;
// BB_TARGET_FPS=60 does too.
#include <immintrin.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/guest.h"
#include "runtime/recorder.hpp"

namespace {

constexpr uint32_t body_set_init = 0x00ae8700, move_body = 0x00f67880, ids_reserve = 0x02bc1ca0, ids_push = 0x02bc1bb0,
                   shape_init = 0x00ac46a0, controller_init = 0x00ae3fc0, transform_of = 0x0083d6d0,
                   root_update = 0x00ae6180, shape_summary = 0x00ac5e00, tracker_args = 0x00ae6dc0, tracker_init = 0x00ae6d00;
constexpr uint32_t fatal_error = 0x024b55b0, memset_thunk = 0x02fbe688;
constexpr uint32_t physics_manager = 0x0593d700, heap_ids = 0x05940450, heap_flags = 0x05940408, shape_heap = 0x058018b0;
constexpr uint32_t tls_allocator_offset = 0x057e40f8, body_set_arg = 0x057e4118;
constexpr uint32_t vt_instance = 0x0578d440, vt_owner = 0x0574f2a0, vt_listener = 0x0578d1b0, vt_link = 0x056b3380,
                   vt_pair_a = 0x0578d140, vt_pair_b = 0x0578d180;
constexpr uint32_t str_bad_heap = 0x04b37dc0;
constexpr uint32_t k_identity_w = 0x04d00b80, k_motion_scale = 0x04d00b90, k_axes = 0x02fd5420, k_motion_gain = 0x02fd5540,
                   k_root_value = 0x04d275d4;

template <typename T> T get(const void *p, uint64_t off) { T v; std::memcpy(&v, static_cast<const char *>(p) + off, sizeof v); return v; }
template <typename T> void put(void *p, uint64_t off, T v) { std::memcpy(static_cast<char *>(p) + off, &v, sizeof v); }
unsigned char *ptr_at(const void *p, uint64_t off) { return get<unsigned char *>(p, off); }
__m128 vec(const void *p, uint64_t off) { return _mm_load_ps(reinterpret_cast<const float *>(static_cast<const char *>(p) + off)); }
void store(void *p, uint64_t off, __m128 v) { _mm_store_ps(reinterpret_cast<float *>(static_cast<char *>(p) + off), v); }
__attribute__((target("sse4.1"))) __m128 with_w_of(__m128 a, __m128 b) { return _mm_blend_ps(a, b, 8); }
template <typename F> F *method(const void *object, unsigned slot) { return reinterpret_cast<F *>(get<void **>(object, 0)[slot / 8]); }

using Alloc1 = void *(void *, uint64_t);
using Alloc2 = void *(void *, uint64_t, uint64_t);
using HeapInfo = void(void *out, void *heap, int);
using ShapeAlloc = void *(void *heap, int32_t *bytes);
using ShapeFree = void(void *heap, void *p, int32_t bytes);
using BodySetInit = void(void *, void *, void *, void *, int, int, int);
using MoveBody = void(void *, uint32_t, void *, int);
using Void1 = void(void *);
using Void2 = void(void *, void *);
using Ids = void(void *, int);
using ShapeInit = void(void *, int, void *, void *);
using ControllerInit = void(void *, void *, void *, void *);
using RootUpdate = void(void *, float);
using Notify = void(void *, void *, uint32_t);
using SetMode = void(void *, int);
using Memset = void *(void *, int, uint64_t);
using Fatal = void(const char *, int, const char *, ...);

struct ShapeArray {
    void *data;
    int32_t size;
    uint32_t capacity;   // bit 31: not owned
};

unsigned char *thread_pointer()
{
    unsigned char *p;
    asm volatile("movq %%gs:0, %0" : "=r"(p));
    return p;
}

struct Context {
    rt::Recorder rec;
    bool tls_recorded = false;

    // The thread's allocator (thread-local storage, read again on each use as the original does).
    void *thread_allocator()
    {
        const int64_t slot = *rt::ptr<int64_t>(tls_allocator_offset);
        unsigned char *tp = thread_pointer();
        unsigned char *holder = get<unsigned char *>(tp + slot, 0);
        void *allocator = ptr_at(holder, 0x58);
        if (rec.on() && !tls_recorded) {
            tls_recorded = true;
            rec.global_bytes(tls_allocator_offset, 8);
            const unsigned below = slot < 0 ? static_cast<unsigned>(-slot) : 0;
            unsigned char *base = tp - below;
            rec.name(base, below + 8 + (slot > 0 ? static_cast<unsigned>(slot) : 0));
            link(base, below, base, below);
            rec.pointer(base, static_cast<unsigned>(static_cast<int64_t>(below) + slot));
            rec.pointer(holder, 0x58);
            rec.pointer(allocator, 0);
            gs = rec.at(base, below, 0);
        }
        return allocator;
    }
    void *allocate(uint64_t bytes)
    {
        void *a = thread_allocator();
        void *p = method<Alloc1>(a, 0x10)(a, bytes);
        if (rec.on()) {
            slot(a, 0x10);
            rec.name(p, static_cast<unsigned>(bytes));
            stub(get<void **>(a, 0)[2], "\"argc\": 2, \"ret\": \"" + rec.value(p) + "\"");
        }
        return p;
    }
    // A virtual method's slot: recorded when the vtable is not in the image (an image vtable is
    // already there in the harness).
    void slot(const void *object, unsigned off)
    {
        const void *vtable = get<void *>(object, 0);
        if (!rt::in_image(vtable)) rec.pointer(vtable, off);
    }
    void stub(const void *function, const std::string &rest)
    {
        rec.stub("{\"address\": \"" + rt::guest_hex(rt::guest_of(function)) + "\", " + rest + "}");
    }
    void stub(uint32_t address, const std::string &rest) { rec.stub("{\"address\": \"" + rt::guest_hex(address) + "\", " + rest + "}"); }
    // A memory entry: the pointer at base+off is target+toff.
    void link(const void *base, unsigned off, const void *target, uint64_t toff)
    {
        if (rt::in_image(target) && toff == 0) {
            rec.memory += (rec.memory.empty() ? "" : ", ") + ("{\"addr\": \"" + rec.at(base, off, 8) + "\", \"guest\": \"" +
                                                             rt::guest_hex(rt::guest_of(target)) + "\"}");
            return;
        }
        rec.memory += (rec.memory.empty() ? "" : ", ") + ("{\"addr\": \"" + rec.at(base, off, 8) + "\", \"pointer\": \"" +
                                                         rec.name(target) + "+" + std::to_string(toff) + "\"}");
    }
    // A stub write of a pointer (into a recorded buffer, or the image, or null) at arg+off.
    std::string write_pointer(int arg, unsigned off, const void *p, uint64_t tag = 0)
    {
        const std::string head = "{\"arg\": " + std::to_string(arg) + ", \"offset\": " + std::to_string(off) + ", ";
        const uintptr_t raw = reinterpret_cast<uintptr_t>(p);
        if (!raw) return head + "\"bytes\": \"0000000000000000\"}";
        if (rt::in_image(p)) return head + "\"guest\": \"" + rt::guest_hex(rt::guest_of(p)) + "\"}";
        return head + "\"pointer\": \"" + rec.name(p) + "+" + std::to_string(tag) + "\"}";
    }
    std::string gs;
};

}  // namespace

extern "C" void bb_physics_instance_construct_01c0c2b0(unsigned char *self, void *owner, void *set_source, void *owner_b,
                                                       void *owner_c, void *user, uint16_t id, void *parent,
                                                       unsigned char *allocator, const float *offset)
{
    Context cx;
    rt::Recorder &rec = cx.rec;
    rec.begin("physics_instance_construct_01c0c2b0");
    if (rec.on()) {
        rec.name(self, 0x1f0);
        rec.bytes(self, 0xa, 1);
        rec.pointer(allocator, 0);
        rec.bytes(offset, 0, 16);
    }

    put<uint16_t>(self, 0x8, id);
    self[0xa] &= 0xc0;
    put<void *>(self, 0x10, parent);
    put<uint64_t>(self, 0x18, 0);
    put<void *>(self, 0x0, rt::ptr<void>(vt_instance));
    put<void *>(self, 0x20, owner);
    put<void *>(self, 0x28, owner_b);
    put<void *>(self, 0x30, owner_c);

    unsigned char *manager = *rt::ptr<unsigned char *>(physics_manager);
    unsigned char *world = ptr_at(ptr_at(manager, 0x28), 0x8);
    if (rec.on()) {
        rec.global_pointer(physics_manager);
        rec.pointer(manager, 0x28);
        rec.pointer(ptr_at(manager, 0x28), 0x8);
    }

    // The body set, and each active body moved by the offset.
    unsigned char *set = static_cast<unsigned char *>(cx.allocate(0x58));
    reinterpret_cast<BodySetInit *>(rt::ptr<void>(body_set_init))(set, set_source, world,
                                                                   *rt::ptr<unsigned char *>(body_set_arg) + 0x1d0, 0, 0, 5);
    if (rec.on()) {
        std::string w = cx.write_pointer(0, 0x10, ptr_at(set, 0x10)) + ", " + cx.write_pointer(0, 0x20, ptr_at(set, 0x20)) + ", " +
                        cx.write_pointer(0, 0x30, ptr_at(set, 0x30)) + ", " + cx.write_pointer(0, 0x40, ptr_at(set, 0x40)) +
                        ", {\"arg\": 0, \"offset\": 40, \"bytes\": \"" + rt::hex_bytes(set + 0x28, 4) + "\"}" +
                        ", {\"arg\": 0, \"offset\": 56, \"bytes\": \"" + rt::hex_bytes(set + 0x38, 4) + "\"}";
        cx.stub(body_set_init, "\"argc\": 6, \"writes\": [" + w + "]");
    }
    const int32_t bodies = get<int32_t>(set, 0x28);
    for (int32_t i = 0; i < bodies; i++) {
        const unsigned char *indices = ptr_at(set, 0x20);
        const int64_t index = get<int32_t>(indices, 4 * static_cast<uint64_t>(i));
        unsigned char *table = ptr_at(world, 0x20);
        if (rec.on()) {
            rec.bytes(indices, 4 * i, 4);
            rec.pointer(world, 0x20);
            rec.bytes(table, index * 0x90 + 0x40, 1);
        }
        if (table[index * 0x90 + 0x40] & 3) {
            const __m128 v = _mm_load_ps(offset);
            const __m128 xyz = _mm_movelh_ps(_mm_unpacklo_ps(v, _mm_shuffle_ps(v, v, 1)),
                                             _mm_move_ss(_mm_setzero_ps(), _mm_movehl_ps(v, v)));
            alignas(16) unsigned char moved[16];
            store(moved, 0, _mm_add_ps(xyz, vec(table, index * 0x90 + 0x30)));
            if (rec.on()) rec.bytes(table, index * 0x90 + 0x30, 16);
            reinterpret_cast<MoveBody *>(rt::ptr<void>(move_body))(world, get<uint32_t>(indices, 4 * static_cast<uint64_t>(i)),
                                                                    moved, 1);
            if (rec.on()) cx.stub(move_body, "\"argc\": 4, \"argmem\": [{\"arg\": 2, \"size\": 16}]");
        }
    }
    put<void *>(self, 0x38, set);
    put<void *>(self, 0x40, rt::ptr<void>(vt_owner));

    void *ids_heap = *rt::ptr<void *>(heap_ids);
    unsigned char info[16] = {};
    method<HeapInfo>(ids_heap, 0x20)(info, ids_heap, 0);
    if (rec.on()) {
        rec.global_pointer(heap_ids);
        rec.pointer(ids_heap, 0);
        cx.slot(ids_heap, 0x20);
        cx.stub(get<void **>(ids_heap, 0)[4], "\"argc\": 3, \"writes\": [{\"arg\": 0, \"bytes\": \"" + rt::hex_bytes(info, 1) + "\"}]");
    }
    if (!(info[0] & 0x20)) {
        rt::fn<Fatal>(fatal_error)(nullptr, 0x3e, rt::ptr<char>(str_bad_heap));
        if (rec.on()) cx.stub(fatal_error, "\"argc\": 3");
    }
    put<uint64_t>(self, 0x60, 0);
    put<uint64_t>(self, 0x58, 0);
    put<uint64_t>(self, 0x50, 0);
    put<void *>(self, 0x68, ids_heap);
    put<uint64_t>(self, 0x58, 0);
    rt::fn<Ids>(ids_reserve)(self + 0x48, 0x20);
    if (rec.on()) {
        const std::string w = cx.write_pointer(0, 0x8, ptr_at(self, 0x50)) + ", " +
                              cx.write_pointer(0, 0x10, ptr_at(self, 0x58), ptr_at(self, 0x58) - ptr_at(self, 0x50)) + ", " +
                              cx.write_pointer(0, 0x18, ptr_at(self, 0x60), ptr_at(self, 0x60) - ptr_at(self, 0x50));
        if (ptr_at(self, 0x50)) rec.name(ptr_at(self, 0x50), static_cast<unsigned>(ptr_at(self, 0x60) - ptr_at(self, 0x50)));
        cx.stub(ids_reserve, "\"argc\": 2, \"writes\": [" + w + "]");
    }

    put<uint64_t>(self, 0x78, 0);
    put<uint64_t>(self, 0x70, 0);
    put<void *>(self, 0x80, user);
    put<uint32_t>(self, 0xa0, 0);
    put<uint64_t>(self, 0x98, 0);
    put<uint64_t>(self, 0x90, 0);
    put<uint64_t>(self, 0x88, 0);
    put<uint32_t>(self, 0xa4, 0x80000000u);
    put<uint32_t>(self, 0xa8, 0);
    put<uint32_t>(self, 0xac, 0);
    self[0xb0] = 0;
    put<uint64_t>(self, 0xb8, 0);
    put<uint32_t>(self, 0xc0, 0xffffffffu);
    put<uint32_t>(self, 0xc4, 0);
    put<uint32_t>(self, 0xc8, 0x3f800000u);
    put<uint32_t>(self, 0xcc, 0x3b5a740fu);
    put<uint32_t>(self, 0xd0, 0);
    put<uint32_t>(self, 0xd4, 0);
    self[0xd8] = 0;
    self[0xd9] = 1;
    self[0xda] = 0;
    self[0xdb] = 0;
    self[0xdc] = 0;
    put<uint64_t>(self, 0x110, 0);
    put<uint32_t>(self, 0x118, 0);
    for (unsigned off : {0x140u, 0x138u, 0x130u, 0x128u, 0x120u}) put<uint64_t>(self, off, 0);
    put<uint32_t>(self, 0x100, 0);
    for (unsigned off : {0xf8u, 0xf0u, 0xe8u, 0xe0u}) put<uint64_t>(self, off, 0);
    put<uint32_t>(self, 0x144, 0x80000000u);
    _mm_storeu_ps(reinterpret_cast<float *>(self + 0x150), _mm_load_ps(rt::ptr<float>(k_identity_w)));
    put<uint32_t>(self, 0x160, 0);
    put<uint32_t>(self, 0x1b0, 0x7fffffffu);
    put<uint64_t>(self, 0x1b8, 0);
    put<uint32_t>(self, 0x1c0, 0);
    self[0x1e4] = 0;
    put<uint32_t>(self, 0x1e0, 0);
    for (unsigned off : {0x1d8u, 0x1d0u, 0x1c8u}) put<uint64_t>(self, off, 0);
    put<uint64_t>(self, 0x1e8, ~0ull);
    self[0xb5] = 0;
    put<uint32_t>(self, 0xb1, 0);

    // Back-links: one 16-byte record per body and per constraint.
    world = ptr_at(ptr_at(*rt::ptr<unsigned char *>(physics_manager), 0x28), 0x8);
    const int64_t body_count = get<int32_t>(set, 0x28);
    const int32_t constraint_count = get<int32_t>(set, 0x38);
    unsigned char *links = nullptr;
    if (static_cast<int32_t>(static_cast<uint32_t>(constraint_count) + static_cast<uint32_t>(body_count)) != 0) {
        const uint64_t bytes = static_cast<uint64_t>(static_cast<int64_t>(
                                   static_cast<int32_t>(static_cast<uint32_t>(constraint_count) + static_cast<uint32_t>(body_count))))
                               << 4;
        links = static_cast<unsigned char *>(method<Alloc1>(allocator, 0x50)(allocator, bytes));
        put<void *>(self, 0x70, links);
        rt::fn<Memset>(memset_thunk)(links, 0, bytes);
        if (rec.on()) {
            rec.name(links, static_cast<unsigned>(bytes));
            cx.slot(allocator, 0x50);
            cx.stub(get<void **>(allocator, 0)[10], "\"argi\": [0, 1], \"ret\": \"" + rec.value(links) + "\"");
            rec.import("{\"name\": \"memset\", \"argc\": 3, \"ret\": \"" + rec.value(links) + "\"}");
        }
        links = ptr_at(self, 0x70);
    }
    put<uintptr_t>(self, 0x78, reinterpret_cast<uintptr_t>(links) + static_cast<uintptr_t>(body_count) * 16);
    if (body_count > 0) {
        for (int64_t i = 0;; i++) {
            unsigned char *record = ptr_at(self, 0x70) + 16 * i;
            put<void *>(record, 0, self);
            put<uint16_t>(record, 8, static_cast<uint16_t>(i));
            if (rec.on()) rec.bytes(ptr_at(self, 0x70), 16 * i + 10, 1);
            record[10] = static_cast<uint8_t>((record[10] & 0xc2) | 4);
            const unsigned char *indices = ptr_at(set, 0x20);
            const int64_t index = get<int32_t>(indices, 4 * i);
            if (rec.on()) {
                rec.bytes(indices, 4 * i, 4);
                rec.pointer(world, 0x20);
                rec.name(ptr_at(world, 0x20), static_cast<unsigned>(index * 0x90 + 0x90));
            }
            put<void *>(ptr_at(world, 0x20), index * 0x90 + 0x88, record);
            if (static_cast<int32_t>(body_count - 1) == static_cast<int32_t>(i)) break;
        }
    }
    if (constraint_count > 0) {
        for (int64_t j = 0; static_cast<int32_t>(j) != constraint_count; j++) {
            unsigned char *record = ptr_at(self, 0x78) + 16 * j;
            put<void *>(record, 0, self);
            put<uint16_t>(record, 8, static_cast<uint16_t>(j));
            if (rec.on()) rec.bytes(ptr_at(self, 0x78), 16 * j + 10, 1);
            record[10] = static_cast<uint8_t>((record[10] & 0xc2) | 0xc);
            const unsigned char *cindices = ptr_at(set, 0x30);
            const uint64_t c = get<uint32_t>(cindices, 4 * j);
            if (rec.on()) {
                rec.bytes(cindices, 4 * j, 4);
                rec.pointer(world, 0x128);
                rec.name(ptr_at(world, 0x128), static_cast<unsigned>(c * 0x38 + 0x38));
            }
            put<void *>(ptr_at(world, 0x128), c * 0x38 + 0x30, record);
        }
    }

    // The shape table, copied for the shape object.
    unsigned char *shapes = ptr_at(ptr_at(self, 0x38), 0x40);
    const int32_t shape_count = get<int32_t>(shapes, 0x40);
    if (rec.on()) rec.bytes(shapes, 0x40, 4);
    void *heap = rt::ptr<void>(shape_heap);
    ShapeArray copy{nullptr, 0, 0x80000000u};
    int32_t granted = 0;
    void *data = nullptr;
    if (rec.on()) {
        rec.pointer(heap, 0);
        cx.slot(heap, 0x20);
        cx.slot(heap, 0x28);
    }
    auto shape_alloc = [&](int32_t count) {
        int32_t bytes = count * 0x30;
        void *p = method<ShapeAlloc>(heap, 0x20)(heap, &bytes);
        if (rec.on()) {
            rec.name(p, static_cast<unsigned>(bytes));
            cx.stub(get<void **>(heap, 0)[4], "\"argc\": 2, \"ret\": \"" + rec.value(p) + "\", \"writes\": [{\"arg\": 1, \"bytes\": \"" +
                                                  rt::hex_bytes(&bytes, 4) + "\"}]");
        }
        granted = bytes / 0x30;
        return p;
    };
    if (shape_count != 0) data = shape_alloc(shape_count);
    uint32_t capacity = granted ? static_cast<uint32_t>(granted) : 0x80000000u;
    copy = {data, shape_count, capacity};
    int32_t want = get<int32_t>(shapes, 0x40);
    if (static_cast<int32_t>(capacity & 0x3fffffff) < want) {
        if (static_cast<int32_t>(capacity) >= 0) {
            method<ShapeFree>(heap, 0x28)(heap, data, static_cast<int32_t>((capacity & 0x3fffffff) * 0x30));
            if (rec.on()) cx.stub(get<void **>(heap, 0)[5], "\"argc\": 3");
            want = get<int32_t>(shapes, 0x40);
        }
        data = shape_alloc(want);
        copy.data = data;
        copy.capacity = static_cast<uint32_t>(granted);
        want = get<int32_t>(shapes, 0x40);
    }
    copy.size = want;
    if (want > 0) {
        const unsigned char *source = ptr_at(shapes, 0x38);
        if (rec.on()) {
            rec.pointer(shapes, 0x38);
            rec.bytes(source, 0, static_cast<unsigned>(want) * 0x30);
        }
        for (int32_t n = 0; n < want; n++)
            for (unsigned k = 0; k < 0x30; k += 16) store(data, 0x30 * static_cast<uint64_t>(n) + k, vec(source, 0x30 * static_cast<uint64_t>(n) + k));
    }
    void *shape = cx.allocate(0x50);
    rt::fn<ShapeInit>(shape_init)(shape, 0, shapes, &copy);
    if (rec.on())
        cx.stub(shape_init, "\"argc\": 4, \"argmem\": [{\"arg\": 3, \"size\": 16}], \"writes\": [" + cx.write_pointer(3, 0, copy.data) +
                                ", {\"arg\": 3, \"offset\": 8, \"bytes\": \"" + rt::hex_bytes(&copy.size, 8) + "\"}]");
    put<void *>(self, 0x88, shape);
    copy.size = 0;
    if (static_cast<int32_t>(copy.capacity) >= 0) {
        method<ShapeFree>(heap, 0x28)(heap, copy.data, static_cast<int32_t>((copy.capacity & 0x3fffffff) * 0x30));
        if (rec.on()) cx.stub(get<void **>(heap, 0)[5], "\"argc\": 3");
    }

    // The controller, with its frame step and the root body's transform.
    unsigned char *controller = static_cast<unsigned char *>(cx.allocate(0xe0));
    const uint32_t params[12] = {0x3e2e147b, 0, 0x3f800000, 0x3f19999a, 0x3d4ccccd, 0x3fb33333,
                                 0x3fe66666, 0x3dcccccd, 0x3e99999a, 0x3e99999a, 0x3cf5c28f, 0x3dcccccd};
    rt::fn<ControllerInit>(controller_init)(controller, ptr_at(self, 0x38), world, const_cast<uint32_t *>(params));
    if (rec.on()) cx.stub(controller_init, "\"argc\": 3, \"argmem\": [{\"arg\": 3, \"size\": 48}]");
    put<void *>(self, 0x90, controller);
    unsigned char *root = ptr_at(ptr_at(ptr_at(self, 0x38), 0x10), 0x40);
    if (rec.on()) {
        rec.pointer(ptr_at(self, 0x38), 0x10);
        rec.pointer(ptr_at(ptr_at(self, 0x38), 0x10), 0x40);
        rec.bytes(root, 0x30, 16);
    }
    alignas(16) unsigned char frame[64], transform[48];
    store(frame, 0x00, _mm_load_ps(rt::ptr<float>(k_axes)));
    store(frame, 0x10, _mm_load_ps(rt::ptr<float>(k_axes + 0x10)));
    store(frame, 0x20, _mm_load_ps(rt::ptr<float>(k_axes + 0x20)));
    store(frame, 0x30, _mm_add_ps(_mm_setzero_ps(), vec(root, 0x30)));
    rt::fn<Void2>(transform_of)(transform, frame);
    if (rec.on())
        cx.stub(transform_of, "\"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 64}], \"writes\": [{\"arg\": 0, \"bytes\": \"" +
                                  rt::hex_bytes(transform, 48) + "\"}]");
    for (unsigned k = 0; k < 48; k += 16) store(controller, 0x80 + k, vec(transform, k));
    put<uint32_t>(controller, 0x60, frame_timing::fixed_step_bits());

    // Two zeroed flag arrays.
    void *flags_heap = *rt::ptr<void *>(heap_flags);
    if (rec.on()) {
        rec.global_pointer(heap_flags);
        rec.pointer(flags_heap, 0);
        cx.slot(flags_heap, 0x58);
    }
    const int64_t per_body = get<int32_t>(set, 0x28);
    void *body_flags = method<Alloc2>(flags_heap, 0x58)(flags_heap, static_cast<uint64_t>(per_body), 1);
    put<void *>(self, 0xb8, body_flags);
    rt::fn<Memset>(memset_thunk)(body_flags, 0, static_cast<uint64_t>(per_body));
    if (rec.on()) {
        rec.name(body_flags, static_cast<unsigned>(per_body > 0 ? per_body : 1));
        cx.stub(get<void **>(flags_heap, 0)[11], "\"argc\": 3, \"ret\": \"" + rec.value(body_flags) + "\"");
        rec.import("{\"name\": \"memset\", \"argc\": 3, \"ret\": \"" + rec.value(body_flags) + "\"}");
    }
    const int64_t per_shape = get<int32_t>(ptr_at(ptr_at(self, 0x38), 0x40), 0x30);
    if (rec.on()) rec.bytes(shapes, 0x30, 4);
    if (per_shape > 0) {
        void *shape_flags = method<Alloc1>(flags_heap, 0x50)(flags_heap, static_cast<uint64_t>(per_shape));
        put<void *>(self, 0x1b8, shape_flags);
        rt::fn<Memset>(memset_thunk)(shape_flags, 0, static_cast<uint64_t>(per_shape));
        if (rec.on()) {
            cx.slot(flags_heap, 0x50);
            rec.name(shape_flags, static_cast<unsigned>(per_shape));
            cx.stub(get<void **>(flags_heap, 0)[10], "\"argi\": [0, 1], \"ret\": \"" + rec.value(shape_flags) + "\"");
            rec.import("{\"name\": \"memset\", \"argc\": 3, \"ret\": \"" + rec.value(shape_flags) + "\"}");
        }
    }

    // Motion values and listeners, unless the manager is in a special mode.
    auto special = [&] {
        unsigned char *m = *rt::ptr<unsigned char *>(physics_manager);
        if (rec.on()) rec.bytes(m, 0x274, 2);
        return m[0x274] || m[0x275];
    };
    if (!special()) {
        const int32_t count = get<int32_t>(ptr_at(self, 0x38), 0x28);
        bool call_root = true;
        if (count > 0) {
            const __m128 gain = _mm_load_ps(rt::ptr<float>(k_motion_gain));
            unsigned char *owner_set = ptr_at(self, 0x38);
            unsigned char **head = reinterpret_cast<unsigned char **>(world + 0x538);
            for (int32_t i = 0; i != count; i++) {
                const unsigned char *indices = ptr_at(owner_set, 0x20);
                const int64_t index = get<int32_t>(indices, 4 * static_cast<uint64_t>(i));
                unsigned char *table = ptr_at(world, 0x20);
                if (rec.on()) {
                    rec.bytes(indices, 4 * i, 4);
                    rec.bytes(table, index * 0x90 + 0x40, 1);
                }
                if (!(table[index * 0x90 + 0x40] & 3)) continue;
                const int64_t motion = get<int32_t>(table, index * 0x90 + 0x68);
                unsigned char *motions = ptr_at(world, 0xe0);
                if (rec.on()) {
                    rec.bytes(table, index * 0x90 + 0x68, 4);
                    rec.pointer(world, 0xe0);
                    rec.bytes(motions, motion * 0x80 + 0x20, 8);
                }
                const __m128i raw = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(motions + motion * 0x80 + 0x20));
                const __m128 widened = _mm_castsi128_ps(_mm_unpacklo_epi16(_mm_setzero_si128(), raw));
                const __m128 scaled = _mm_mul_ps(with_w_of(_mm_mul_ps(widened, _mm_load_ps(rt::ptr<float>(k_motion_scale))), widened), gain);
                const __m128i packed = _mm_srai_epi32(_mm_castps_si128(scaled), 16);
                _mm_storel_epi64(reinterpret_cast<__m128i *>(motions + motion * 0x80 + 0x20), _mm_packs_epi32(packed, packed));

                // Tell the world's listeners; drop the ones flagged for removal.
                const uintptr_t first = reinterpret_cast<uintptr_t>(*head);
                const uint32_t body = get<uint32_t>(indices, 4 * static_cast<uint64_t>(i));
                if (rec.on()) {
                    if (first & ~uintptr_t{3}) cx.link(world, 0x538, reinterpret_cast<void *>(first & ~uintptr_t{3}), first & 3);
                    else rec.bytes(world, 0x538, 8);
                }
                *head = reinterpret_cast<unsigned char *>((first & ~uintptr_t{3}) | 1);
                uintptr_t *prev = reinterpret_cast<uintptr_t *>(head);
                unsigned char *node = reinterpret_cast<unsigned char *>(first & ~uintptr_t{3});
                while (node) {
                    const uintptr_t link_raw = get<uintptr_t>(node, 8);
                    unsigned char *next = reinterpret_cast<unsigned char *>(link_raw & ~uintptr_t{3});
                    if (rec.on()) {
                        rec.pointer(node, 0);
                        cx.slot(node, 0x18);
                        cx.slot(node, 0x8);
                        if (next) cx.link(node, 8, next, link_raw & 3);
                        else rec.bytes(node, 8, 8);
                    }
                    bool remove = (link_raw & 3) != 0;
                    if (!remove) {
                        method<Notify>(node, 0x18)(node, world, body);
                        if (rec.on())
                            cx.stub(get<void **>(node, 0)[3], "\"argc\": 3, \"writes\": [" +
                                                                  cx.write_pointer(0, 8, reinterpret_cast<void *>(get<uintptr_t>(node, 8) & ~uintptr_t{3}),
                                                                                   get<uintptr_t>(node, 8) & 3) + "]");
                        remove = (node[8] & 3) != 0;
                        if (!remove) {
                            prev = reinterpret_cast<uintptr_t *>(node + 8);
                            node = next;
                            continue;
                        }
                    }
                    void *destroy = get<void **>(node, 0)[1];
                    method<Void1>(node, 0x8)(node);
                    if (rec.on()) cx.stub(destroy, "\"argc\": 1");
                    *prev = (*prev & 3) | reinterpret_cast<uintptr_t>(next);
                    node = next;
                }
                *reinterpret_cast<uint8_t *>(head) &= 0xfc;
            }
            call_root = !special();
        }
        if (call_root) {
            void *root_body = ptr_at(ptr_at(self, 0x38), 0x10);
            rt::fn<RootUpdate>(root_update)(root_body, *rt::ptr<float>(k_root_value));
            if (rec.on()) cx.stub(root_update, "\"argc\": 1, \"argf32\": [0]");
        }
    }

    // The instance's own listener.
    alignas(16) unsigned char summary[32];
    rt::fn<Void2>(shape_summary)(ptr_at(self, 0x88), summary);
    if (rec.on())
        cx.stub(shape_summary, "\"argc\": 1, \"writes\": [{\"arg\": 1, \"bytes\": \"" + rt::hex_bytes(summary, 32) + "\"}]");
    flags_heap = *rt::ptr<void *>(heap_flags);
    unsigned char *listener = static_cast<unsigned char *>(method<Alloc2>(flags_heap, 0x58)(flags_heap, 0x38, 8));
    if (rec.on()) {
        if (listener) rec.name(listener, 0x38);
        cx.stub(get<void **>(flags_heap, 0)[11], "\"argc\": 3, \"ret\": \"" + rec.value(listener) + "\"");
    }
    unsigned char *own = nullptr;
    if (listener) {
        put<uint16_t>(listener, 0x8, id);
        listener[0xa] &= 0xc0;
        put<void *>(listener, 0x10, parent);
        put<uint64_t>(listener, 0x18, 0);
        put<uint64_t>(listener, 0x30, 0);
        put<uint64_t>(listener, 0x28, 0);
        put<uint64_t>(listener, 0x20, 0);
        put<void *>(listener, 0x28, listener);
        put<uint16_t>(listener, 0x30, 0);
        listener[0x32] = 8;
        put<void *>(listener, 0x0, rt::ptr<void>(vt_listener));
        unsigned char *link_object = static_cast<unsigned char *>(cx.allocate(0x50));
        put<uint32_t>(link_object, 0x8, 0xffff0001u);
        put<uint64_t>(link_object, 0x10, 0);
        put<void *>(link_object, 0x0, rt::ptr<unsigned char>(vt_link) + 0x10);
        store(link_object, 0x30, vec(summary, 0));
        store(link_object, 0x40, vec(summary, 16));
        put<uint32_t>(link_object, 0x20, 0);
        put<void *>(link_object, 0x18, listener + 0x28);
        put<void *>(listener, 0x20, link_object);
        own = listener;
    }
    put<void *>(self, 0x128, own);
    method<SetMode>(own, 0x30)(own, 0x2f);
    if (rec.on()) cx.stub(get<void **>(own, 0)[6], "\"argc\": 2");

    // The object pair and the tracker.
    unsigned char *pair = static_cast<unsigned char *>(cx.allocate(0x18));
    put<uint32_t>(pair, 0x8, 0xffff0001u);
    put<void *>(pair, 0x0, rt::ptr<void>(vt_pair_a));
    put<void *>(pair, 0x10, rt::ptr<void>(vt_pair_b));
    put<void *>(self, 0x120, pair ? pair + 0x10 : nullptr);
    alignas(16) unsigned char tracker_setup[64] = {};
    rt::fn<Void1>(tracker_args)(tracker_setup);
    if (rec.on()) cx.stub(tracker_args, "\"argc\": 1");
    put<void *>(tracker_setup, 0, ptr_at(ptr_at(self, 0x38), 0x40));
    put<void *>(tracker_setup, 8, ptr_at(self, 0x120));
    void *tracker = cx.allocate(0x40);
    rt::fn<Void2>(tracker_init)(tracker, tracker_setup);
    if (rec.on()) cx.stub(tracker_init, "\"argc\": 1, \"argmem\": [{\"arg\": 1, \"size\": 16}]");
    put<void *>(self, 0x130, tracker);

    // The distinct motion types above 4, in the id vector.
    unsigned char *owner_set = ptr_at(self, 0x38);
    const int32_t count = get<int32_t>(owner_set, 0x28);
    if (count != 0) {
        unsigned char *w = ptr_at(ptr_at(*rt::ptr<unsigned char *>(physics_manager), 0x28), 0x8);
        for (int64_t i = 0; static_cast<int32_t>(i) != count; i++) {
            const int64_t index = get<int32_t>(ptr_at(owner_set, 0x20), 4 * i);
            const int64_t motion = get<int32_t>(ptr_at(w, 0x20), index * 0x90 + 0x68);
            if (rec.on()) {
                rec.bytes(ptr_at(owner_set, 0x20), 4 * i, 4);
                rec.bytes(ptr_at(w, 0x20), index * 0x90 + 0x68, 4);
                rec.pointer(w, 0xe0);
                rec.bytes(ptr_at(w, 0xe0), motion * 0x80 + 0x38, 2);
            }
            uint32_t type = get<uint16_t>(ptr_at(w, 0xe0), motion * 0x80 + 0x38);
            if (type < 5) continue;
            const uint32_t *it = get<const uint32_t *>(self, 0x50);
            const uint32_t *end = get<const uint32_t *>(self, 0x58);
            for (;;) {
                if (it == end) {
                    rt::fn<Void2>(ids_push)(self + 0x48, &type);
                    if (rec.on()) {
                        const std::string wr = cx.write_pointer(0, 0x8, ptr_at(self, 0x50)) + ", " +
                                               cx.write_pointer(0, 0x10, ptr_at(self, 0x58), ptr_at(self, 0x58) - ptr_at(self, 0x50)) + ", " +
                                               cx.write_pointer(0, 0x18, ptr_at(self, 0x60), ptr_at(self, 0x60) - ptr_at(self, 0x50));
                        rec.name(ptr_at(self, 0x50), static_cast<unsigned>(ptr_at(self, 0x60) - ptr_at(self, 0x50)));
                        cx.stub(ids_push, "\"argc\": 1, \"argmem\": [{\"arg\": 1, \"size\": 4}], \"writes\": [" + wr + "]");
                    }
                    break;
                }
                if (rec.on()) rec.bytes(get<const uint32_t *>(self, 0x50), static_cast<unsigned>(4 * (it - get<const uint32_t *>(self, 0x50))), 4);
                const uint32_t v = *it++;
                if (v == type) break;
            }
        }
    }

    if (rec.on()) {
        char args[512];
        std::snprintf(args, sizeof args,
                      "\"rdi\": \"buf:%s\", \"rsi\": \"0x%llx\", \"rdx\": \"0x%llx\", \"rcx\": \"0x%llx\", \"r8\": \"0x%llx\", \"r9\": \"0x%llx\"",
                      rec.name(self).c_str(), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owner)),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(set_source)),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owner_b)),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owner_c)),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(user)));
        char stack[256];
        std::snprintf(stack, sizeof stack, "\"stack\": [\"0x%x\", \"0x%llx\", \"%s\", \"%s\"]", id,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(parent)), rec.value(allocator).c_str(),
                      rec.value(offset).c_str());
        rec.write("0x01c0c2b0", args, "void", std::string(stack) + (cx.gs.empty() ? "" : ", \"gs\": \"" + cx.gs + "\""));
    }
}
