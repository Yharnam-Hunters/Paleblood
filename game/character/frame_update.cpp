// SPDX-License-Identifier: GPL-2.0-or-later
// Character: a per-frame update of a character instance.
//
// character_frame_update_01cbdb20 (0x01cbdb20; called from two character update loops) takes the
// frame time from this->+0xe0 and:
// - fires a pending throw animation event ("W_ThrowAtk" / "W_ThrowDef") on the character's
//   module chain (modules +0x88 -> +0x8 -> +0x3b0 -> +0x30) and clears the request;
// - regenerates a points stat (data module +0x20: current +0x134, clamped to -50..max +0x138,
//   which reads like stamina): rate = byte +0x34 of the parameters (this->+0x58 -> +0x38) * 0.01
//   * this->+0x16c * virtual +0x210; it stores the rate truncated at +0x39c and adds the whole
//   part of rate * frame time to the stat, keeping the fraction at +0x168. A drop is skipped while
//   the data module's +0x200 bit 7 or the debug flag 0x0593e88a is set;
// - passes the frame time to 0x01e5c440 in a time-step descriptor (vtable 0x056efd30 + 0x10) and
//   to 0x01913c10, and makes further module calls (0x01e59250, 0x01e4a490, 0x01cd92b0);
// - copies a float from virtual +0x168's object to an animation object, five parameter words to
//   +0x380..+0x390, and builds and drops a wide string of the character's name with the thread's
//   allocator (thread-local storage), as the original does.
//
// The frame-rate patches replace the frame time with a constant: 1/27 s ("30 FPS++"), 1/54 s
// ("60 FPS++"); "Uncap FPS++" keeps the real one. BB_TARGET_FPS does the same.
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t module_first = 0x01e59250, throw_event = 0x01e19e20, step_consumer = 0x01e5c440,
                   module_second = 0x01e4a490, module_last = 0x01cd92b0, params_update = 0x01913c10;
constexpr uint32_t fatal_error = 0x024b55b0, tls_allocator_init = 0x0247d250, wstring_assign = 0x02a2a310;
constexpr uint32_t wcslen_thunk = 0x02fbf228;
constexpr uint32_t str_throw_def = 0x04d32f68, str_throw_atk = 0x04d32f5d, str_none = 0x04dcfa3c;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd, str_world_chr_dbg = 0x04d35c4a,
                   str_bad_heap = 0x04b37dc0;
constexpr uint32_t k_rate_scale = 0x04d27c5c;   // 0.01
constexpr uint32_t world_chr_man_dbg = 0x0593e880, debug_no_drop = 0x0593e88a;
constexpr uint32_t tls_allocator_offset = 0x057e4568;
constexpr uint32_t step_vtable = 0x056efd30;   // + 0x10
constexpr uint32_t k_dt_30 = 0x3d17b426, k_dt_60 = 0x3c97b426;   // 1/27 s, 1/54 s

template <typename T> T get(const void *p, unsigned off) { T v; std::memcpy(&v, static_cast<const char *>(p) + off, sizeof v); return v; }
template <typename T> void put(void *p, unsigned off, T v) { std::memcpy(static_cast<char *>(p) + off, &v, sizeof v); }
void *ptr_at(const void *p, unsigned off) { return get<void *>(p, off); }

float mul(float a, float b) { return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float add(float a, float b) { return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float sub(float a, float b) { return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b))); }
int32_t truncate(float x) { return _mm_cvttss_si32(_mm_set_ss(x)); }
float bits_to_float(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
uint32_t float_bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

struct Step {
    void *vtable;
    float seconds;
};

// The engine's wide string (wchar16, 8 characters inline) with its allocator.
struct WString {
    uint64_t unused;
    union {
        uint16_t inline_chars[8];
        void *heap;
    };
    uint64_t size, capacity;
    void *allocator;
    uint8_t owns;
};
static_assert(sizeof(WString) == 0x38);

using Void1 = void(void *);
using Void2 = void(void *, const void *);
using Float1 = float(void *);
using Ptr1 = void *(void *);
using Void0 = void();
using ParamsUpdate = void(void *, float);
using AllocatorInfo = void(void *out, void *allocator, int);
using AllocatorFree = void(void *allocator, void *p);
using Assign = void(WString *, const uint16_t *, uint64_t);
using Wcslen = uint64_t(const uint16_t *);
using Fatal = void(const char *, int, const char *, ...);

void *thread_pointer()
{
    void *p;
    asm volatile("movq %%gs:0, %0" : "=r"(p));
    return p;
}

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

std::string guest_of(const void *host)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x",
                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE));
    return b;
}

// Recording: every input the function reads, as buffers named after what they hold.
struct Capture {
    int number = -1;
    std::string buffers, memory, stubs, imports;
    bool on() const { return number >= 0; }
    void buffer(const char *name, unsigned size) { buffers += (buffers.empty() ? "" : ", ") + ("\"" + std::string(name) + "\": {\"size\": " + std::to_string(size) + "}"); }
    void entry(const std::string &e) { memory += (memory.empty() ? "" : ", ") + e; }
    void bytes(const std::string &at, const void *src, size_t n) { entry("{\"addr\": \"" + at + "\", \"bytes\": \"" + hex(src, n) + "\"}"); }
    void pointer(const std::string &at, const char *name) { entry("{\"addr\": \"" + at + "\", \"pointer\": \"" + name + "\"}"); }
    void guest(const std::string &at, const void *host) { entry("{\"addr\": \"" + at + "\", \"guest\": \"" + guest_of(host) + "\"}"); }
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    void import(const std::string &e) { imports += (imports.empty() ? "" : ", ") + e; }
};

std::string at(const char *name, unsigned off) { return "buf:" + std::string(name) + "+" + std::to_string(off); }

}  // namespace

extern "C" void bb_character_frame_update_01cbdb20(unsigned char *self)
{
    Capture cap;
    cap.number = rt_capture_begin("character_frame_update_01cbdb20");
    void **vtable = get<void **>(self, 0x0);

    float dt;
    switch (frame_timing::target_fps()) {
    case frame_timing::Target::fps30: dt = bits_to_float(k_dt_30); break;
    case frame_timing::Target::fps60: dt = bits_to_float(k_dt_60); break;
    default: dt = get<float>(self, 0xe0); break;
    }
    unsigned char *a = static_cast<unsigned char *>(ptr_at(self, 0x58));
    unsigned char *params = static_cast<unsigned char *>(ptr_at(a, 0x38));
    unsigned char *modules = static_cast<unsigned char *>(ptr_at(self, 0x3b0));
    if (cap.on()) {
        cap.buffer("self", 0x3b8);
        cap.buffer("vt", 0x218);
        cap.buffer("a", 0x288);
        cap.buffer("params", 0x88);
        cap.buffer("modules", 0x90);
        cap.pointer(at("self", 0), "vt");
        cap.bytes(at("self", 0xe0), self + 0xe0, 4);
        cap.pointer(at("self", 0x58), "a");
        cap.pointer(at("a", 0x38), "params");
        cap.pointer(at("self", 0x3b0), "modules");
        cap.bytes(at("modules", 0x58), modules + 0x58, 8);
    }
    rt::fn<Void1>(module_first)(ptr_at(modules, 0x58));
    if (cap.on()) cap.stub("{\"address\": \"0x01e59250\", \"argc\": 1}");

    // A pending throw animation event.
    unsigned char *request = static_cast<unsigned char *>(ptr_at(modules, 0x88));
    unsigned char *owner = static_cast<unsigned char *>(ptr_at(ptr_at(ptr_at(request, 0x8), 0x3b0), 0x30));
    if (cap.on()) {
        cap.buffer("request", 0x58);
        cap.buffer("owner1", 0x3b8);
        cap.buffer("owner2", 0x38);
        cap.pointer(at("modules", 0x88), "request");
        cap.pointer(at("request", 0x8), "owner1");
        cap.pointer(at("owner1", 0x3b0), "owner2");
        if (owner) {
            cap.buffer("anim", 0x388);
            cap.pointer(at("owner2", 0x30), "anim");
            cap.bytes(at("anim", 0x384), owner + 0x384, 1);
            if (owner[0x384]) cap.bytes(at("request", 0x53), request + 0x53, 1);
            if (owner[0x384] && request[0x53]) cap.bytes(at("request", 0x54), request + 0x54, 4);
        }
    }
    if (owner && owner[0x384] && request[0x53]) {
        const uint32_t kind = get<uint32_t>(request, 0x54);
        if (kind == 2 || kind == 1) {
            rt::fn<Void2>(throw_event)(owner, rt::ptr<char>(kind == 2 ? str_throw_def : str_throw_atk));
            if (cap.on()) cap.stub("{\"address\": \"0x01e19e20\", \"argc\": 2}");
        }
        request[0x53] = 0;
        put<uint32_t>(request, 0x54, 0);
    }

    // Regeneration of the points stat.
    if (cap.on()) cap.bytes(at("params", 0x5c), params + 0x5c, 1);
    if (!(params[0x5c] & 2)) {
        float rate = mul(mul(static_cast<float>(static_cast<int32_t>(params[0x34])), *rt::ptr<float>(k_rate_scale)),
                         get<float>(self, 0x16c));
        const float factor = reinterpret_cast<Float1 *>(vtable[0x210 / 8])(self);
        if (cap.on()) {
            cap.bytes(at("params", 0x34), params + 0x34, 1);
            cap.bytes(at("self", 0x16c), self + 0x16c, 4);
            cap.guest(at("vt", 0x210), vtable[0x210 / 8]);
            char b[96];
            std::snprintf(b, sizeof b, "{\"address\": \"%s\", \"argc\": 1, \"ret\": \"0x%08x\"}",
                          guest_of(vtable[0x210 / 8]).c_str(), float_bits(factor));
            cap.stub(b);
        }
        rate = mul(rate, factor);
        put<int32_t>(self, 0x39c, truncate(rate));
        if (cap.on()) cap.bytes(at("self", 0x168), self + 0x168, 4);
        const float total = add(mul(dt, rate), get<float>(self, 0x168));
        const int32_t whole = truncate(total);
        put<float>(self, 0x168, sub(total, static_cast<float>(whole)));

        unsigned char *data = static_cast<unsigned char *>(ptr_at(modules, 0x20));
        if (cap.on()) {
            cap.buffer("data", 0x204);
            cap.pointer(at("modules", 0x20), "data");
            cap.bytes(at("data", 0x134), data + 0x134, 4);
        }
        int32_t points = static_cast<int32_t>(static_cast<uint32_t>(get<int32_t>(data, 0x134)) + static_cast<uint32_t>(whole));
        bool skip = false;
        if (whole < 0) {
            if (cap.on()) cap.bytes(at("data", 0x200), data + 0x200, 1);
            if (data[0x200] & 0x80) {
                skip = true;
            } else {
                if (cap.on()) cap.bytes("0x0593e880", rt::ptr<void>(world_chr_man_dbg), 8);
                if (*rt::ptr<uint64_t>(world_chr_man_dbg) == 0) {
                    rt::fn<Fatal>(fatal_error)(rt::ptr<char>(str_singleton_header), 0xb1, rt::ptr<char>(str_singleton_func),
                                               rt::ptr<char>(str_world_chr_dbg));
                    if (cap.on()) cap.stub("{\"address\": \"0x024b55b0\", \"argc\": 4}");
                }
                if (cap.on()) cap.bytes("0x0593e88a", rt::ptr<void>(debug_no_drop), 1);
                skip = *rt::ptr<uint8_t>(debug_no_drop) != 0;
            }
        }
        if (!skip) {
            if (points >= -50) {
                if (cap.on()) cap.bytes(at("data", 0x138), data + 0x138, 4);
                const int32_t max = get<int32_t>(data, 0x138);
                if (max <= points) points = max;
            } else {
                points = -50;
            }
            put<int32_t>(data, 0x134, points);
        }
    }

    Step step{rt::ptr<unsigned char>(step_vtable) + 0x10, dt};
    if (cap.on()) cap.bytes(at("modules", 0x28), modules + 0x28, 8);
    rt::fn<Void2>(step_consumer)(ptr_at(modules, 0x28), &step);
    if (cap.on()) cap.stub("{\"address\": \"0x01e5c440\", \"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 12}]}");
    if (cap.on()) cap.bytes(at("modules", 0x68), modules + 0x68, 8);
    rt::fn<Void1>(module_second)(ptr_at(modules, 0x68));
    if (cap.on()) cap.stub("{\"address\": \"0x01e4a490\", \"argc\": 1}");

    // A float from the object behind virtual +0x168 goes to the animation object.
    void *const getter = vtable[0x168 / 8];
    void *linked = reinterpret_cast<Ptr1 *>(getter)(self);
    if (cap.on()) {
        cap.guest(at("vt", 0x168), getter);
        if (linked) cap.buffer("linked", 16);
        cap.stub("{\"address\": \"" + guest_of(getter) + "\", \"argc\": 1, \"ret\": \"" + (linked ? "buf:linked" : "0x0") + "\"}");
    }
    if (linked) {
        unsigned char *target = static_cast<unsigned char *>(ptr_at(a, 0x280));
        if (cap.on()) {
            cap.buffer("target", 0x84);
            if (target) cap.pointer(at("a", 0x280), "target");
            else cap.bytes(at("a", 0x280), &target, 8);
        }
        if (!target) {
            target = static_cast<unsigned char *>(ptr_at(a, 0x80));
            if (cap.on()) cap.pointer(at("a", 0x80), "target");
        }
        void *again = reinterpret_cast<Ptr1 *>(getter)(self);
        unsigned char *source = static_cast<unsigned char *>(ptr_at(again, 0x8));
        float value = 0.0f;
        if (source) value = get<float>(source, 0x8c);
        if (cap.on()) {
            const char *second = again == linked ? "linked" : "linked2";
            if (again != linked) cap.buffer("linked2", 16);
            cap.stub("{\"address\": \"" + guest_of(getter) + "\", \"argc\": 1, \"ret\": \"buf:" + second + "\"}");
            if (source) {
                cap.buffer("linked_source", 0x90);
                cap.pointer(at(second, 8), "linked_source");
                cap.bytes(at("linked_source", 0x8c), source + 0x8c, 4);
            }
        }
        put<float>(target, 0x80, value);
    }

    // The character's name as a wide string with the thread's allocator, built and dropped.
    unsigned char *anim_state = static_cast<unsigned char *>(ptr_at(self, 0x288));
    if (cap.on()) {
        cap.bytes(at("self", 0x1e1), self + 0x1e1, 1);
        if (!(self[0x1e1] & 0xa0)) {
            if (anim_state) {
                cap.buffer("anim_state", 0xd9);
                cap.pointer(at("self", 0x288), "anim_state");
                cap.bytes(at("anim_state", 0xd8), anim_state + 0xd8, 1);
            } else {
                cap.bytes(at("self", 0x288), &anim_state, 8);
            }
        }
    }
    if (!(self[0x1e1] & 0xa0) && anim_state && anim_state[0xd8] != 6) {
        const int64_t slot = *rt::ptr<int64_t>(tls_allocator_offset);
        unsigned char *tp = static_cast<unsigned char *>(thread_pointer());
        unsigned char *heap = get<unsigned char *>(tp + slot, 0);
        if (cap.on()) {
            // The thread pointer at gs:[0] and the allocator slot below it (variant II TLS).
            cap.bytes("0x057e4568", &slot, 8);
            const unsigned below = slot < 0 ? static_cast<unsigned>(-slot) : 0;
            cap.buffer("tls", below + 8 + (slot > 0 ? static_cast<unsigned>(slot) : 0));
            cap.pointer(at("tls", below), ("tls+" + std::to_string(below)).c_str());
            const unsigned heap_at = static_cast<unsigned>(static_cast<int64_t>(below) + slot);
            if (heap) cap.pointer(at("tls", heap_at), "heap");
        }
        if (!heap) {
            rt::fn<Void0>(tls_allocator_init)();
            if (cap.on()) cap.stub("{\"address\": \"0x0247d250\", \"argc\": 0}");
            tp = static_cast<unsigned char *>(thread_pointer());
            heap = get<unsigned char *>(tp + slot, 0);
        }
        unsigned char *allocator = heap + 0x28;
        void **allocator_vtable = get<void **>(allocator, 0);
        unsigned char info[16] = {};
        reinterpret_cast<AllocatorInfo *>(allocator_vtable[0x20 / 8])(info, allocator, 0);
        if (cap.on()) {
            cap.buffer("heap", 0x30);
            cap.buffer("heap_vt", 0x78);
            cap.pointer(at("heap", 0x28), "heap_vt");
            cap.guest(at("heap_vt", 0x20), allocator_vtable[0x20 / 8]);
            cap.guest(at("heap_vt", 0x70), allocator_vtable[0x70 / 8]);
            cap.stub("{\"address\": \"" + guest_of(allocator_vtable[0x20 / 8]) +
                     "\", \"argc\": 3, \"writes\": [{\"arg\": 0, \"bytes\": \"" + hex(info, 1) + "\"}]}");
        }
        if (!(info[0] & 0x20)) {
            rt::fn<Fatal>(fatal_error)(nullptr, 0x3e, rt::ptr<char>(str_bad_heap));
            if (cap.on()) cap.stub("{\"address\": \"0x024b55b0\", \"argc\": 3}");
        }
        WString name;
        name.unused = 0;
        name.allocator = allocator;
        name.capacity = 7;
        name.size = 0;
        name.inline_chars[0] = 0;
        name.owns = 1;

        unsigned char *character_name = static_cast<unsigned char *>(ptr_at(self, 0x350));
        const uint16_t *text;
        uint64_t length = 0;
        if (!character_name) {
            text = rt::ptr<uint16_t>(str_none);
            length = rt::fn<Wcslen>(wcslen_thunk)(text);
            if (cap.on()) {
                cap.bytes(at("self", 0x350), &character_name, 8);
                cap.import("{\"name\": \"wcslen\", \"argc\": 1, \"ret\": " + std::to_string(length) + "}");
            }
        } else {
            unsigned char *def = static_cast<unsigned char *>(ptr_at(character_name, 0x18));
            const uint32_t *version = static_cast<const uint32_t *>(ptr_at(def, 0x88));
            const uint16_t *const *names = static_cast<const uint16_t *const *>(ptr_at(character_name, 0x20));
            text = *version < 3 ? names[0] : names[1];
            if (text[0] != 0) length = rt::fn<Wcslen>(wcslen_thunk)(text);
            if (cap.on()) {
                cap.buffer("name", 0x28);
                cap.buffer("name_def", 0x90);
                cap.buffer("name_version", 4);
                cap.buffer("names", 16);
                cap.buffer("text", 2);
                cap.pointer(at("self", 0x350), "name");
                cap.pointer(at("name", 0x18), "name_def");
                cap.pointer(at("name_def", 0x88), "name_version");
                cap.bytes(at("name_version", 0), version, 4);
                cap.pointer(at("name", 0x20), "names");
                cap.pointer(at("names", *version < 3 ? 0 : 8), "text");
                cap.bytes(at("text", 0), text, 2);
                if (text[0] != 0) cap.import("{\"name\": \"wcslen\", \"argc\": 1, \"ret\": " + std::to_string(length) + "}");
            }
        }
        rt::fn<Assign>(wstring_assign)(&name, text, length);
        if (cap.on()) {
            // Compared: the first character, size, capacity, allocator and flag the original sets
            // (the rest of the inline buffer is uninitialised); written back: what the real assign
            // left there (pointer or inline text, size, capacity).
            cap.stub("{\"address\": \"0x02a2a310\", \"argc\": 3, \"argmem\": [{\"arg\": 0, \"offset\": 8, \"size\": 2}, {\"arg\": 0, \"offset\": 24, \"size\": 25}], "
                     "\"writes\": [{\"arg\": 0, \"offset\": 8, \"bytes\": \"" +
                     hex(reinterpret_cast<unsigned char *>(&name) + 8, 32) + "\"}]}");
        }
        if (name.capacity >= 8) {
            reinterpret_cast<AllocatorFree *>(get<void **>(name.allocator, 0)[0x70 / 8])(name.allocator, name.heap);
            if (cap.on()) cap.stub("{\"address\": \"" + guest_of(get<void **>(name.allocator, 0)[0x70 / 8]) + "\", \"argc\": 2}");
        }
    }

    if (cap.on()) cap.bytes(at("params", 0x74), params + 0x74, 20);
    for (unsigned i = 0; i < 5; i++) put<uint32_t>(self, 0x380 + 4 * i, get<uint32_t>(params, 0x74 + 4 * i));
    void *extra = ptr_at(self, 0x1f8);
    if (cap.on()) cap.bytes(at("self", 0x1f8), self + 0x1f8, 8);
    if (extra) {
        rt::fn<Void1>(module_last)(extra);
        if (cap.on()) cap.stub("{\"address\": \"0x01cd92b0\", \"argc\": 1}");
    }
    reinterpret_cast<ParamsUpdate *>(rt::ptr<void>(params_update))(a, dt);
    if (cap.on()) {
        cap.stub("{\"address\": \"0x01913c10\", \"argc\": 1, \"argf32\": [0]}");
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x01cbdb20\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:self\"}, ", cap.number);
        std::string json = std::string(head) + "\"buffers\": {" + cap.buffers + "}, \"memory\": [" + cap.memory + "], \"stubs\": [" +
                           cap.stubs + "], \"imports\": [" + cap.imports + "]";
        if (cap.memory.find("buf:tls") != std::string::npos) {
            const int64_t slot = *rt::ptr<int64_t>(tls_allocator_offset);
            json += ", \"gs\": \"buf:tls+" + std::to_string(slot < 0 ? -slot : 0) + "\"";
        }
        json += "}";
        rt_capture_write("character_frame_update_01cbdb20", cap.number, json.c_str());
    }
}
