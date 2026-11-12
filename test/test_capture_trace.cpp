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

/* Native imports for the guest functions at 0x400..0x4c0 (test/capture_trace_guest.S). */
extern "C" {
int32_t trace_stale_counter;
void trace_stale_import(int32_t *out);
void trace_plain_import(int32_t *out);
void trace_far_import(char *p);
}
asm(R"(
    .text
    .globl trace_stale_import, trace_plain_import, trace_far_import
trace_stale_import:
    incl trace_stale_counter(%rip)
    mov trace_stale_counter(%rip), %eax
    mov %eax, (%rdi)
    lea trace_stale_counter(%rip), %rdx
    ret
trace_plain_import:
    movl $21, (%rdi)
    ret
trace_far_import:
    movl $21, (%rdi)
    incb 1024(%rdi)
    ret
)");

unsigned char *rt_image;

namespace {

/* The import use_import calls: writes v * 3 through out and returns 5. */
bool fill_returns_out;   /* read_returned's import gives its first argument back, as memset does */

int64_t fill(int32_t *out, int32_t v)
{
    if (fill_returns_out) return reinterpret_cast<intptr_t>(out);
    *out = v * 3;
    return 5;
}

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
    int32_t filled = 0x55555555;
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

    /* An import: run natively, recorded with its result and what it wrote, and tracing resumes. */
    int32_t filled = 0x55555555;
    r = trace(0x2c0, arguments(reinterpret_cast<uint64_t>(&filled)), log);
    check(!log.incomplete && r.rax == 5 + 21 && filled == 21, "import: result, and tracing resumed after it");
    check(log.import_count == 1 && !std::strcmp(log.imports[0].name, "testFill") && log.imports[0].ret == 5 &&
          log.imports[0].writes == 1 && log.writes[0].arg == 0 && log.writes[0].offset == 0 && log.writes[0].size == 4 &&
          log.write_bytes[log.writes[0].data] == 21, "import: name, result and the byte it changed");
    check(!read_input(log, &filled, 21) && written(log, &filled), "import: what it wrote is not an input");
    for (size_t i = 0; i < log.count; i++)
        check(log.addr[i] < reinterpret_cast<uintptr_t>(image + 0x1800) ||
              log.addr[i] >= reinterpret_cast<uintptr_t>(image + 0x1808), "import: its slot is not an input");
    rt_trace_log_free(&log);

    /* The call's own frame is never dead, even when its private stack lies just below the caller's
       rsp: here the caller's rsp is put right above where the frame was on the previous call. */
    trace(0x380, arguments(), log);
    rt_trace_log_free(&log);
    uint64_t frame;
    std::memcpy(&frame, image + 0x1808, sizeof frame);
    rt_trace_args above = arguments();
    above.caller_rsp = frame + 0x100;
    r = trace(0x380, above, log);
    uint64_t again;
    std::memcpy(&again, image + 0x1808, sizeof again);
    if (again == frame)          /* the private stack came back at the same place, as it usually does */
        check(!log.incomplete && r.rax == 21 && log.import_count == 1 && log.imports[0].writes == 1,
              "import: a write into the call's own frame is scripted, whatever the caller's rsp");
    else
        std::puts("capture trace: private stack moved between calls; frame check skipped");
    rt_trace_log_free(&log);

    /* An import that returns a pointer into a buffer has it scripted as a buffer pointer. */
    alignas(16) int32_t returned[4] = {41, 0, 0, 0};
    fill_returns_out = true;
    rt_trace_args give = arguments(reinterpret_cast<uint64_t>(returned));
    r = trace(0x3c0, give, log);
    fill_returns_out = false;
    const rt_traced_function read_returned = {"capture_trace_read_returned", 0x4003c0, "i32", at(0x3c0)};
    check(!log.incomplete && r.rax == 41 &&
              rt::traced_case(log, give, read_returned, 0, "selftest", {image, image + image_size})
                      .find("\"ret\": \"buf:b0+0x0\"") != std::string::npos,
          "import: a returned pointer into a buffer is a buffer pointer");
    rt_trace_log_free(&log);

    /* A value whose upper bytes were already right changes one byte, but the whole word is
       scripted, so a replay does not keep what its own memory held there. */
    alignas(8) int32_t word[2] = {0, 0};
    r = trace(0x2c0, arguments(reinterpret_cast<uint64_t>(&word[0])), log);
    check(!log.incomplete && word[0] == 21 && log.import_count == 1 && log.imports[0].writes == 1 &&
              log.writes[0].offset == 0 && log.writes[0].size == 8 && log.write_bytes[log.writes[0].data] == 21,
          "import: a partly changed word is scripted whole");
    rt_trace_log_free(&log);

    /* Changes below the caller's rsp are dead to the caller: not scripted as the import's writes. */
    filled = 0x55555555;
    rt_trace_args below = arguments(reinterpret_cast<uint64_t>(&filled));
    below.caller_rsp = reinterpret_cast<uint64_t>(&filled) + sizeof filled;
    r = trace(0x2c0, below, log);
    check(!log.incomplete && filled == 21 && log.import_count == 1 && log.imports[0].writes == 0,
          "import: a change below the caller's rsp is not scripted");
    rt_trace_log_free(&log);

    /* An argument register an earlier import left is not an argument: the second call's rdx
       still points where the first left it, at memory the import changes for itself. */
    alignas(8) int32_t stale_out[2] = {0, 0};
    r = trace(0x400, arguments(reinterpret_cast<uint64_t>(stale_out)), log);
    check(!log.incomplete && static_cast<int32_t>(r.rax) == trace_stale_counter && log.import_count == 2 && log.imports[1].writes == 1 &&
              log.writes[log.imports[1].first_write].arg == 0,
          "import: a register an earlier import left is not scripted through");
    rt_trace_log_free(&log);

    /* Two registers pointing at the same bytes: scripted once, through the first. */
    alignas(8) int32_t same[2] = {0, 0};
    r = trace(0x440, arguments(reinterpret_cast<uint64_t>(same)), log);
    check(!log.incomplete && same[0] == 21 && log.import_count == 1 && log.imports[0].writes == 1 &&
              log.writes[0].arg == 0,
          "import: bytes two registers point at are scripted once");
    rt_trace_log_free(&log);

    /* A change far past what the import wrote, on bytes the traced code never touches (another
       thread's), is not the import's write; on bytes the traced code read, it is scripted. */
    alignas(16) static char far_bytes[2048];
    r = trace(0x480, arguments(reinterpret_cast<uint64_t>(far_bytes)), log);
    check(!log.incomplete && far_bytes[1024] == 1 && log.import_count == 1 && log.imports[0].writes == 1 &&
              log.writes[0].offset == 0,
          "import: a far change on untouched bytes is not scripted");
    rt_trace_log_free(&log);
    far_bytes[0] = 0;
    r = trace(0x4c0, arguments(reinterpret_cast<uint64_t>(far_bytes)), log);
    check(!log.incomplete && r.rax == 1 && far_bytes[1024] == 2 && log.import_count == 1 && log.imports[0].writes == 2 &&
              log.writes[1].offset == 1024,
          "import: a far change on bytes the traced code read is scripted");
    rt_trace_log_free(&log);

    /* Two fields 4 KiB apart, through one pointer: one buffer, so the replay keeps the distance. */
    alignas(16) static char object[0x1010];
    rt_trace_args object_args = arguments(reinterpret_cast<uint64_t>(object));
    trace(0x500, object_args, log);
    const rt_traced_function two_fields = {"capture_trace_two_fields", 0x400500, "void", at(0x500)};
    const std::string object_case = rt::traced_case(log, object_args, two_fields, 0, "selftest", {image, image + image_size});
    check(!log.incomplete && log.link_count == 1 && object_case.find("\"rdi\": \"buf:b0+0x0\"") != std::string::npos &&
              object_case.find("\"b1\"") == std::string::npos && object_case.find("\"size\": 4112") != std::string::npos,
          "a far field through a pointer is in the pointer's buffer");
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
    /* Bytes of a read-only segment are not inputs: the verifier maps the same executable. */
    const rt_code_range globals = {image + 0x1000, image + 0x1800};
    rt_capture_set_constant(&globals, 1);
    check(rt::traced_case(log, args, scale, 0, "selftest", whole).find("\"addr\": \"0x00401000\"") == std::string::npos,
          "case: a read-only global is left out");
    rt_capture_set_constant(nullptr, 0);
    log.incomplete = "forced";
    check(rt::traced_case(log, args, scale, 0, "selftest", whole).empty(), "incomplete trace: no case");
    log.incomplete = nullptr;
    const rt_traced_function wrong = {"capture_trace_scale", 0x400000, "u32", at(0x000)};
    check(rt::traced_case(log, args, wrong, 0, "selftest", whole).empty(), "unknown return class: no case");
    rt_trace_log_free(&log);

    /* A pointer argument just before the only bytes it reaches still points into their buffer. */
    alignas(16) unsigned char record[0x80] = {};
    rt_trace_args store = arguments(reinterpret_cast<uint64_t>(record), 9);
    trace(0x300, store, log);
    const rt_traced_function store_field = {"capture_trace_store_field", 0x400300, "void", at(0x300)};
    check(rt::traced_case(log, store, store_field, 0, "selftest", whole).find("\"rdi\": \"buf:b0+0x0\"") != std::string::npos,
          "case: a pointer before the bytes it reaches is a buffer pointer");
    rt_trace_log_free(&log);
    /* A table base far below the one entry read keeps its distance to it. */
    alignas(16) static unsigned char table[0x1010] = {};
    rt_trace_args far = arguments(reinterpret_cast<uint64_t>(table));
    trace(0x340, far, log);
    const rt_traced_function read_far = {"capture_trace_read_far", 0x400340, "i32", at(0x340)};
    const std::string far_case = rt::traced_case(log, far, read_far, 0, "selftest", whole);
    check(far_case.find("\"rdi\": \"buf:b0+0x0\"") != std::string::npos && far_case.find("\"size\": 4112") != std::string::npos,
          "case: a table base far below the entry read is a buffer pointer");
    rt_trace_log_free(&log);

    if (!failures) std::puts("capture trace self-test: traced calls, inputs and fail-closed cases passed");
    return failures ? 1 : 0;
}

int emit(const char *dir)
{
    setenv("BB_CAPTURE_DIR", dir, 1);
    setenv("BB_CAPTURE_RUN", "synthetic", 1);
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
        {{"capture_trace_use_import", 0x4002c0, "i32", at(0x2c0)}, arguments(reinterpret_cast<uint64_t>(&f.filled))},
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
    /* Bind the import, as a loader would, and list it for the tracer. The image file the replay
       builds from is written before binding (emit), so its slot holds 0 for the verifier to bind. */
    static const rt_trace_import import_table[] = {{image + 0x1800, "testFill"},
                                                   {image + 0x1810, "testStale"},
                                                   {image + 0x1818, "testPlain"},
                                                   {image + 0x1820, "testFar"}};
    if (!rt_trace_available()) {
        std::puts("capture trace: built without Capstone; nothing to test");
        return 0;
    }
    const auto bind = [] {
        const uint64_t targets[] = {reinterpret_cast<uintptr_t>(fill), reinterpret_cast<uintptr_t>(trace_stale_import),
                                    reinterpret_cast<uintptr_t>(trace_plain_import),
                                    reinterpret_cast<uintptr_t>(trace_far_import)};
        std::memcpy(image + 0x1800, &targets[0], sizeof targets[0]);
        std::memcpy(image + 0x1810, &targets[1], 3 * sizeof targets[0]);
        rt_trace_set_imports(import_table, 4);
    };
    if (argc == 2 && !std::strcmp(argv[1], "--self-test")) return bind(), self_test();
    if (argc == 3 && !std::strcmp(argv[1], "--image")) {
        FILE *out = std::fopen(argv[2], "wb");
        return out && std::fwrite(image, 1, image_size, out) == image_size && !std::fclose(out) ? 0 : 1;
    }
    if (argc == 3 && !std::strcmp(argv[1], "--emit")) {
        const std::string path = std::string(argv[2]) + "/image.bin";
        FILE *out = std::fopen(path.c_str(), "wb");
        if (!out || std::fwrite(image, 1, image_size, out) != image_size || std::fclose(out)) return 1;
        bind();
        return emit(argv[2]);
    }
    std::fprintf(stderr, "usage: %s --self-test | --emit DIR\n", argv[0]);
    return 2;
}
