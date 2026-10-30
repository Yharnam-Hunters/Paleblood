/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Tracing a call into a case (runtime/trace.c, runtime/capture_trace.cpp) on synthetic guest
   code (test/capture_trace_guest.S) mapped as an image.

   --self-test  traces each guest function, checks the call's result and what the trace saw, and
                checks that what a case cannot express gives no case.
   --emit DIR   writes the image (DIR/image.bin) and one case per function under DIR, for
                test/capture_trace_integration.py to replay with tools/verify.py. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <asm/prctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "runtime/capture.h"
#include "runtime/capture_trace.h"
#include "runtime/guest.h"
#include "runtime/trace.h"

extern "C" const unsigned char capture_trace_guest_start[], capture_trace_guest_end[];

unsigned char *rt_image;

namespace {

struct Inner { int32_t pad, value; };
struct Outer { int64_t pad; Inner *inner; };

unsigned char *image;
size_t image_size;
int failures;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "capture trace: FAILED: %s\n", what);
        ++failures;
    }
}

const void *at(uint32_t offset) { return image + offset; }

rt_trace_args arguments(uint64_t a = 0, uint64_t b = 0, uint64_t c = 0)
{
    rt_trace_args args;
    std::memset(&args, 0, sizeof args);
    args.gpr[0] = a;
    args.gpr[1] = b;
    args.gpr[2] = c;
    return args;
}

/* Traces the guest function at `offset`; the log is left in `log` for the caller to free. */
rt_trace_result trace(uint32_t offset, const rt_trace_args &args, rt_trace_log &log)
{
    const rt_code_range code = {image, image + image_size};
    rt_trace_result result;
    std::memset(&result, 0, sizeof result);
    check(rt_trace_call(at(offset), &args, &code, 1, &result, &log) == 0, "the traced call runs");
    return result;
}

bool read_input(const rt_trace_log &log, const void *p, uint8_t value)
{
    for (size_t i = 0; i < log.count; i++)
        if (log.addr[i] == reinterpret_cast<uintptr_t>(p)) return (log.flags[i] & RT_TRACE_READ) && log.value[i] == value;
    return false;
}

bool written(const rt_trace_log &log, const void *p)
{
    for (size_t i = 0; i < log.count; i++)
        if (log.addr[i] == reinterpret_cast<uintptr_t>(p)) return log.flags[i] & RT_TRACE_WRITTEN;
    return false;
}

/* The objects each case uses, kept alive while it is traced and written. */
struct Fixture {
    Inner inner{0, 7};
    Outer outer{0, &inner};
    int32_t out = 0x11223344;
    float factor = 2.5f;
    int32_t counter = 40;
    char src[24] = "trace this to the case";
    char dst[24] = {};
};

int self_test()
{
    Fixture f;
    rt_trace_log log;

    rt_trace_result r = trace(0x000, arguments(reinterpret_cast<uint64_t>(&f.out), reinterpret_cast<uint64_t>(&f.outer), 6), log);
    check(!log.incomplete && static_cast<int32_t>(r.rax) == 7 * 6 + 1000 && f.out == 1042, "scale: result");
    check(read_input(log, &f.inner.value, 7) && read_input(log, image + 0x1000, 0xe8), "scale: inputs through the pointer and the global");
    check(written(log, &f.out) && log.qword_count == 1 && log.qword[0] == reinterpret_cast<uintptr_t>(&f.outer.inner),
          "scale: output written, one pointer input");
    rt_trace_log_free(&log);

    rt_trace_args stack = arguments(5);
    stack.stack[0] = 3;
    stack.stack[1] = 4;
    r = trace(0x040, stack, log);
    check(!log.incomplete && r.rax == 3 * 10 + 4 + 5 && log.stack_slots == 2, "stack arguments: result and count");
    rt_trace_log_free(&log);

    rt_trace_args floats = arguments(reinterpret_cast<uint64_t>(&f.factor));
    const float x = 4.0f;
    std::memcpy(floats.xmm[0], &x, sizeof x);
    r = trace(0x080, floats, log);
    float y;
    std::memcpy(&y, r.xmm0, sizeof y);
    check(!log.incomplete && y == 10.0f, "float argument and result");
    rt_trace_log_free(&log);

    r = trace(0x0c0, arguments(reinterpret_cast<uint64_t>(&f.counter)), log);
    check(!log.incomplete && r.rax == 42 && f.counter == 42 && read_input(log, &f.counter, 40), "guest call: traced into, first value kept");
    rt_trace_log_free(&log);

    r = trace(0x140, arguments(reinterpret_cast<uint64_t>(f.dst), reinterpret_cast<uint64_t>(f.src), sizeof f.src), log);
    check(!log.incomplete && !std::memcmp(f.dst, f.src, sizeof f.src) && read_input(log, &f.src[23], 0) &&
          written(log, &f.dst[0]) && written(log, &f.dst[23]), "rep movsb: whole ranges");
    rt_trace_log_free(&log);

    /* What a case cannot express: the call still runs to the end, but there is no case. */
    trace(0x180, arguments(), log);
    check(log.incomplete != nullptr, "xmm2 read: incomplete");
    rt_trace_log_free(&log);
    /* The call reads gs:[0], as guest code reads its thread pointer: give it one, as the
       runtime does, so the untraced rest of the call can run. */
    static uint64_t thread_block[2] = {0x1234, 0};
    syscall(SYS_arch_prctl, ARCH_SET_GS, reinterpret_cast<unsigned long>(thread_block));
    r = trace(0x1c0, arguments(), log);
    syscall(SYS_arch_prctl, ARCH_SET_GS, 0ul);
    check(log.incomplete != nullptr && r.rax == 0x1234, "thread-local access: incomplete, and the call still ran");
    rt_trace_log_free(&log);
    static int native_calls;
    void (*native)() = [] { ++native_calls; };
    r = trace(0x200, arguments(reinterpret_cast<uint64_t>(&native)), log);
    check(log.incomplete != nullptr && native_calls == 1, "native call: incomplete, and the call still ran");
    rt_trace_log_free(&log);
    setenv("BB_TRACE_STEP_LIMIT", "100", 1);
    trace(0x240, arguments(), log);
    check(log.incomplete != nullptr, "instruction limit: incomplete");
    rt_trace_log_free(&log);
    unsetenv("BB_TRACE_STEP_LIMIT");

    /* A complete trace becomes a case; an incomplete one never does. */
    const rt_code_range whole = {image, image + image_size};
    rt_trace_args args = arguments(reinterpret_cast<uint64_t>(&f.out), reinterpret_cast<uint64_t>(&f.outer), 6);
    trace(0x000, args, log);
    const rt_traced_function scale = {"capture_trace_scale", 0x400000, "i32", at(0x000)};
    const std::string json = rt::traced_case(log, args, scale, 0, "selftest", whole);
    check(json.find("\"rsi\": \"buf:b") != std::string::npos && json.find("\"pointer\": \"b") != std::string::npos &&
          json.find("\"addr\": \"0x00401000\", \"bytes\": \"e8030000\"") != std::string::npos, "case: buffers, pointer and global");
    log.incomplete = "forced";
    check(rt::traced_case(log, args, scale, 0, "selftest", whole).empty(), "incomplete trace: no case");
    log.incomplete = nullptr;
    const rt_traced_function wrong = {"capture_trace_scale", 0x400000, "u32", at(0x000)};
    check(rt::traced_case(log, args, wrong, 0, "selftest", whole).empty(), "unknown return class: no case");
    rt_trace_log_free(&log);

    if (!failures) std::puts("capture trace self-test: traced calls, inputs and fail-closed cases passed");
    return failures ? 1 : 0;
}

int emit(const char *dir)
{
    setenv("BB_CAPTURE_DIR", dir, 1);
    setenv("BB_CAPTURE_RUN", "synthetic", 1);
    std::string path = std::string(dir) + "/image.bin";
    FILE *out = std::fopen(path.c_str(), "wb");
    if (!out || std::fwrite(image, 1, image_size, out) != image_size || std::fclose(out)) return 1;

    Fixture f;
    const rt_code_range whole = {image, image + image_size};
    struct Call { rt_traced_function function; rt_trace_args args; };
    rt_trace_args stack = arguments(5);
    stack.stack[0] = 3;
    stack.stack[1] = 4;
    rt_trace_args floats = arguments(reinterpret_cast<uint64_t>(&f.factor));
    const float x = 4.0f;
    std::memcpy(floats.xmm[0], &x, sizeof x);
    const Call calls[] = {
        {{"capture_trace_scale", 0x400000, "i32", at(0x000)},
         arguments(reinterpret_cast<uint64_t>(&f.out), reinterpret_cast<uint64_t>(&f.outer), 6)},
        {{"capture_trace_stack_args", 0x400040, "i64", at(0x040)}, stack},
        {{"capture_trace_scale_float", 0x400080, "f32", at(0x080)}, floats},
        {{"capture_trace_twice", 0x4000c0, "i32", at(0x0c0)}, arguments(reinterpret_cast<uint64_t>(&f.counter))},
        {{"capture_trace_copy", 0x400140, "void", at(0x140)},
         arguments(reinterpret_cast<uint64_t>(f.dst), reinterpret_cast<uint64_t>(f.src), sizeof f.src)},
    };
    for (const Call &c : calls) {
        const int number = rt_capture_begin(c.function.function);
        rt_trace_result result;
        if (number < 0 || rt_capture_traced_call(&c.function, number, &c.args, whole, &whole, 1, &result) != 1) {
            std::fprintf(stderr, "capture trace: no case for %s\n", c.function.function);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    image_size = static_cast<size_t>(capture_trace_guest_end - capture_trace_guest_start);
    image = static_cast<unsigned char *>(mmap(nullptr, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (image == MAP_FAILED) return 1;
    std::memcpy(image, capture_trace_guest_start, image_size);
    rt_image = image;
    if (!rt_trace_available()) {
        std::puts("capture trace: built without Capstone; nothing to test");
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "--self-test")) return self_test();
    if (argc == 3 && !std::strcmp(argv[1], "--emit")) return emit(argv[2]);
    std::fprintf(stderr, "usage: %s --self-test | --emit DIR\n", argv[0]);
    return 2;
}
