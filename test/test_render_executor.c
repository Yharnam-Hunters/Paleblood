/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The draw executor (runtime/renderer/render_executor.c) on a GPU: guest memory is a shared memory
   file mapped below 2^40 (where the runtime keeps GPU-visible memory), holding a vertex and a pixel
   shader (gcn_programs.h) and the triangle's corners and colours. A command buffer's calls are
   recorded as the GPU library's replacements will record them (render target, viewport, shader
   register blocks, user data, an auto-index draw), the executor runs them, and every pixel of the
   render target is checked.

   The triangle covers the 16 x 16 target with red (x + 0.5) / 16 and green (y + 0.5) / 16 at pixel
   (x, y), blue 0.25 and alpha 1, as in test_gcn_draw. Without a suitable Vulkan device the test
   reports itself skipped (exit 77). */
#define _GNU_SOURCE /* memfd_create, MAP_FIXED_NOREPLACE (render_fixture.h) */
#include "runtime/render_executor.h"

#include "render_fixture.h"

#include <stdlib.h>

/* Every pixel of the target holds the triangle's colour. */
static void check_triangle(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    const int read = rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error);
    if (read) fprintf(stderr, "read: %s\n", error);
    CHECK(read == 0);
    if (!read) check_triangle_pixels(bgra);
}

/* Records the triangle with these registers set before its draw, and runs it. */
static int run_with_registers(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int count,
                              uint32_t instances, char *error, size_t error_size)
{
    rt_render_forget();
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < count; i++, offset += 0x10)
        CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    if (instances) {
        CHECK(rt_render_record_instances(buffer, offset, instances, error, error_size) == 0);
        offset += 0x10;
    }
    return rt_executor_run(x, buffer, record_triangle_draw(buffer, offset), error, error_size);
}

int main(void)
{
    const int fd = map_guest_memory();
    if (fd < 0) {
        fprintf(stderr, "cannot map the guest memory at %#llx\n", GUEST_BASE);
        return 1;
    }
    rt_executor *x = NULL;
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE};
    if (rt_executor_open(&memory, &x, error, sizeof error)) {
        fprintf(stderr, "skipped: %s\n", error);
        return SKIPPED;
    }
    enum { BUFFER = 0xc0de0000, SECOND = 0xc0df0000, THIRD = 0xc0e00000 };

    /* A triangle into an R8G8B8A8 target (SWAP_STD), through guest addresses and the pool's GPU buffer. */
    rt_render_forget();
    uint32_t size = record_draw(BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const int ran = rt_executor_run(x, BUFFER, size, error, sizeof error);
    if (ran) fprintf(stderr, "run: %s\n", error);
    CHECK(ran == 0);
    check_triangle(x, at(TARGET));

    /* The same into a B8G8R8A8 target (SWAP_ALT): the display's pixels are the same. */
    size = record_draw(SECOND, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_ALT, PERSP_CENTER);
    CHECK(rt_executor_run(x, SECOND, size, error, sizeof error) == 0);
    check_triangle(x, at(OTHER_TARGET));

    /* What is not drawn yet stops with the reason. */
    size = record_draw(THIRD, at(TARGET) + 0x400000, COLOR_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "not drawn yet"));
    size = record_draw(THIRD, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | FRONT_FACE);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "pixel-shader inputs"));
    /* The library's registers: the normal state draws as before, a target masked off draws nothing,
       and what is not drawn yet (blending, depth, other primitives, instances) stops by name. */
    enum { FOURTH = 0xc0e10000 };
    const uint32_t normal[][2] = {{0xa1e0, 0}, {0xa200, 0}, {0xa202, 0x1u << 4 | 0xccu << 16}, {0xa08e, 0xf}, {0xc242, 4}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 5, 1, error, sizeof error) == 0);
    check_triangle(x, at(TARGET));
    const uint32_t masked[][2] = {{0xa08e, 0xf0}};                   /* only target 1 writes */
    CHECK(run_with_registers(x, FOURTH, at(TARGET) + 0x400000, masked, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(TARGET) + 0x400000, bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    const uint32_t blended[][2] = {{0xa1e0, 0x40000000}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), blended, 1, 0, error, sizeof error) == -1 && strstr(error, "blending"));
    const uint32_t depth[][2] = {{0xa200, 0x2}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), depth, 1, 0, error, sizeof error) == -1 && strstr(error, "depth or stencil"));
    const uint32_t strips[][2] = {{0xc242, 5}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), strips, 1, 0, error, sizeof error) == -1 && strstr(error, "primitive type 5"));
    const uint32_t partial[][2] = {{0xa08e, 0x7}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), partial, 1, 0, error, sizeof error) == -1 && strstr(error, "write mask"));
    const uint32_t resolve[][2] = {{0xa202, 0x3u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), resolve, 1, 0, error, sizeof error) == -1 && strstr(error, "colour mode 3"));
    const uint32_t unknown[][2] = {{0xa1e1, 0}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), unknown, 1, 0, error, sizeof error) == -1 && strstr(error, "0xa1e1"));
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 0, 2, error, sizeof error) == -1 && strstr(error, "2 instances"));
    CHECK(rt_executor_read_target(x, at(0x300000), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(TARGET), bgra, SIZE + 1, 1, error, sizeof error) == -1);
    /* No pixel shader (a depth-only pass, placed as the runtime's sceGnmSetPsShader places it): a
       draw with it stops by name. */
    rt_render_forget();
    uint32_t offset = record_setup(FOURTH, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
    CHECK(rt_render_record_pixel_shader_at(FOURTH + offset, no_pixel_shader) == 0);
    CHECK(rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "no pixel shader"));

    rt_executor_close(x);
    rt_render_forget();
    munmap((void *)(uintptr_t)GUEST_BASE, GUEST_SIZE);
    close(fd);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
