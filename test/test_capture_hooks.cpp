/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Recording hooked calls (runtime/capture_hooks.c): the synthetic guest code of
   test/capture_trace_guest.S is mapped as an image, hooked through bbgame_init and the
   version-1 host installer (runtime/host_hooks.c), and called through the image as the game
   calls it. A sampled call must run the original traced and return its result; any other call
   must reach the replacement; a traced original that calls another hooked function must follow
   that function's original code; a function whose loop jumps back into its first bytes must not
   be recorded at all.

   --self-test  checks those behaviours, recording into a temporary directory.
   --emit DIR   records into DIR (with DIR/image.bin, the image before hooking) for
                test/capture_trace_integration.py. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime/host.h"
#include "runtime/host_hooks.h"
#include "runtime/hooks.h"
#include "runtime/trace.h"

extern "C" const unsigned char capture_trace_guest_start[], capture_trace_guest_end[];

namespace {

struct Inner { int32_t pad, value; };
struct Outer { int64_t pad; Inner *inner; };

int scale_calls, twice_calls, increment_calls, copy_calls, call_native_calls, count_down_calls;

}  // namespace

extern "C" {
static int32_t scale_replacement(int32_t *out, const Outer *s, int32_t factor)
{
    ++scale_calls;
    *out = s->inner->value * factor + 1000;
    return *out;
}
static int32_t increment_replacement(int32_t *p) { ++increment_calls; return ++*p; }
static int32_t twice_replacement(int32_t *p) { ++twice_calls; ++*p; return ++*p; }
static void copy_replacement(char *dst, const char *src, size_t n) { ++copy_calls; std::memcpy(dst, src, n); }
static void call_native_replacement(void (**fn)()) { ++call_native_calls; (*fn)(); }
static int32_t count_down_replacement(int32_t) { ++count_down_calls; return 7; }

/* The table plugin.c installs, sorted by address. */
extern const rt_hook_entry game_hooks[];
const rt_hook_entry game_hooks[] = {
    {0x00400000u, 0x40u, "bb_capture_trace_scale", reinterpret_cast<rt_hook_fn>(scale_replacement), "i32"},
    {0x004000c0u, 0x40u, "bb_capture_trace_twice", reinterpret_cast<rt_hook_fn>(twice_replacement), "i32"},
    {0x00400100u, 0x40u, "bb_capture_trace_increment", reinterpret_cast<rt_hook_fn>(increment_replacement), "i32"},
    {0x00400140u, 0x40u, "bb_capture_trace_copy", reinterpret_cast<rt_hook_fn>(copy_replacement), "void"},
    {0x00400200u, 0x40u, "bb_capture_trace_call_native", reinterpret_cast<rt_hook_fn>(call_native_replacement), "void"},
    {0x00400280u, 0x40u, "bb_capture_trace_count_down", reinterpret_cast<rt_hook_fn>(count_down_replacement), "i32"},
};
extern const unsigned game_hook_count;
const unsigned game_hook_count = sizeof game_hooks / sizeof game_hooks[0];
}

namespace {

int failures;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "capture hooks: FAILED: %s\n", what);
        ++failures;
    }
}

bool exists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

int run(const std::string &dir, bool self_test)
{
    const size_t size = static_cast<size_t>(capture_trace_guest_end - capture_trace_guest_start);
    auto *image = static_cast<unsigned char *>(mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (image == MAP_FAILED) return 1;
    std::memcpy(image, capture_trace_guest_start, size);
    FILE *out = std::fopen((dir + "/image.bin").c_str(), "wb");
    if (!out || std::fwrite(image, 1, size, out) != size || std::fclose(out)) return 1;

    /* increment and count_down are not wanted: called directly, they reach their replacements. */
    setenv("BB_CAPTURE_DIR", dir.c_str(), 1);
    setenv("BB_CAPTURE_RUN", "hooks", 1);
    setenv("BB_CAPTURE_TRACE", "1", 1);
    setenv("BB_CAPTURE_ONLY", "capture_trace_scale,capture_trace_twice,capture_trace_copy,capture_trace_call_native,capture_trace_count_down", 1);
    rt_host host = {RT_HOST_API_VERSION, image, size, nullptr};
    if (rt_host_hooks_initialize(&host) || bbgame_init(&host)) {
        std::fprintf(stderr, "capture hooks: bbgame_init failed\n");
        return 1;
    }

    Inner inner{0, 7};
    Outer outer{0, &inner};
    int32_t result = 0;
    auto scale = reinterpret_cast<int32_t (*)(int32_t *, const Outer *, int32_t)>(image + 0x000);
    check(scale(&result, &outer, 6) == 1042 && result == 1042 && scale_calls == 0, "scale: sampled call runs the original traced");

    int32_t counter = 40;
    auto twice = reinterpret_cast<int32_t (*)(int32_t *)>(image + 0x0c0);
    check(twice(&counter) == 42 && counter == 42 && twice_calls == 0 && increment_calls == 0,
          "twice: traced, and its call to the hooked increment follows increment's original");

    auto increment = reinterpret_cast<int32_t (*)(int32_t *)>(image + 0x100);
    check(increment(&counter) == 43 && increment_calls == 1, "increment: unwanted, so the replacement runs");

    char src[24] = "hooked and traced, too!", dst[24] = {};
    auto copy = reinterpret_cast<void (*)(char *, const char *, size_t)>(image + 0x140);
    copy(dst, src, sizeof src);
    check(!std::memcmp(dst, src, sizeof src) && copy_calls == 0, "copy: traced");

    auto count_down = reinterpret_cast<int32_t (*)(int32_t)>(image + 0x280);
    check(count_down(3) == 7 && count_down_calls == 1, "count_down: cannot be recorded, so the replacement runs");

    /* Its trace always leaves for native code, so it never gives a case: after three tries the
       function is no longer traced, and its replacement runs. */
    static int native_calls;
    void (*native)() = [] { ++native_calls; };
    auto call_native = reinterpret_cast<void (*)(void (**)())>(image + 0x200);
    for (int i = 0; i < 4; i++) call_native(&native);
    check(native_calls == 4 && call_native_calls == 1, "call_native: traced three times without a case, then replaced");

    if (self_test) {
        check(exists(dir + "/capture_trace_scale/hooks_0000.json"), "scale: case written");
        check(exists(dir + "/capture_trace_twice/hooks_0000.json"), "twice: case written");
        check(exists(dir + "/capture_trace_copy/hooks_0000.json"), "copy: case written");
        check(!exists(dir + "/capture_trace_increment") && !exists(dir + "/capture_trace_count_down") &&
                  !exists(dir + "/capture_trace_call_native"),
              "no case for an unwanted, unrecordable or never complete function");
        unsetenv("BB_CAPTURE_ONLY");
        for (int i = 0; i < 3; i++) scale(&result, &outer, i);
        check(scale_calls == 0 && exists(dir + "/capture_trace_scale/hooks_0003.json"), "each sampled call adds a case");
        if (!failures) std::puts("capture hooks self-test: sampled calls traced, others replaced, nested hooks followed");
    }
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char **argv)
{
    if (!rt_trace_available()) {
        std::puts("capture hooks: built without Capstone; nothing to test");
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--self-test")) {
        char dir[] = "/tmp/pb-capture-hooks-XXXXXX";
        if (!mkdtemp(dir)) return 1;
        const int rc = run(dir, true);
        const std::string cleanup = std::string("rm -rf '") + dir + "'";
        if (std::system(cleanup.c_str())) return 1;
        return rc;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--emit")) return run(argv[2], false);
    std::fprintf(stderr, "usage: %s --self-test | --emit DIR\n", argv[0]);
    return 2;
}
