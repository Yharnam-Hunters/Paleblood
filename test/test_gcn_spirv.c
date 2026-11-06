/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shader translator (runtime/renderer/gcn_spirv.c) without a GPU: each test program becomes a
   SPIR-V module with the right header, which spirv-val (when installed) accepts for Vulkan 1.2;
   an instruction not handled yet stops translation and is named. test_gcn_compute runs the
   modules on a GPU.

   For macOS, each module is also turned into Metal Shading Language 2.3 with SPIRV-Cross (when
   installed), and that is compiled with Apple's Metal compiler where there is one (xcrun metal, on
   macOS). With PB_REQUIRE_METAL set (the macOS CI job) a missing tool is a failure, not a skip. */
#include "runtime/gcn_spirv.h"
#include "runtime/rect_list.h"

#include "gcn_programs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define PROGRAM(code) code, sizeof code / sizeof code[0]

static const gcn_compute_layout layout = {.local_size = {64, 1, 1}, .user_sgprs = 8, .workgroup_id_x_sgpr = -1};

/* Runs spirv-val on the module: 1 valid, 0 invalid, -1 when spirv-val is not installed. */
static int validate(const uint32_t *spirv, size_t words, const char *name)
{
    if (system("spirv-val --version > /dev/null 2>&1") != 0) return -1;
    char path[512];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/pb-gcn-%s.spv", tmp ? tmp : "/tmp", name);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(spirv, sizeof *spirv, words, f);
    fclose(f);
    char command[700];
    snprintf(command, sizeof command, "spirv-val --target-env vulkan1.2 %s", path);
    const int ok = system(command) == 0;
    remove(path);
    return ok;
}

static int installed(const char *command)
{
    char check[256];
    snprintf(check, sizeof check, "%s > /dev/null 2>&1", command);
    return system(check) == 0;
}

/* The module as Metal: converted by SPIRV-Cross, then compiled by Apple's compiler when present.
   1 converted (and compiled), 0 failed, -1 a tool is missing. */
static int to_metal(const uint32_t *spirv, size_t words, const char *name)
{
    if (!installed("spirv-cross --help")) return -1;
    char spv[512], metal[520], air[520], command[1700];
    const char *tmp = getenv("TMPDIR");
    snprintf(spv, sizeof spv, "%s/pb-gcn-msl-%s.spv", tmp ? tmp : "/tmp", name);
    snprintf(metal, sizeof metal, "%s.metal", spv);
    snprintf(air, sizeof air, "%s.air", spv);
    FILE *f = fopen(spv, "wb");
    if (!f) return 0;
    fwrite(spirv, sizeof *spirv, words, f);
    fclose(f);
    snprintf(command, sizeof command, "spirv-cross --msl --msl-version 20300 %s --output %s", spv, metal);
    int ok = system(command) == 0;
    int compiled = -1;
    if (ok && installed("xcrun -sdk macosx metal --version")) {
        snprintf(command, sizeof command, "xcrun -sdk macosx metal -std=macos-metal2.3 -c %s -o %s", metal, air);
        compiled = system(command) == 0;
        ok = compiled;
    }
    remove(spv), remove(metal), remove(air);
    if (ok && compiled < 0 && getenv("PB_REQUIRE_METAL")) return -1; /* converted, but no compiler */
    return ok;
}

typedef enum { COMPUTE, VERTEX, PIXEL } stage;

static const gcn_vertex_layout vertex_layout = {.user_sgprs = 8};
/* PA_CL_VS_OUT_CNTL values: clip distances 0 and 1 in the first distance vector (the game's
   shaders all use this one); the same with the misc vector first; a cull distance instead. */
enum { CLIP_DISTANCES_0_1 = 0x400003, MISC_VECTOR_FIRST = 0x600003, CULL_DISTANCE_0 = 0x400100 };
static const gcn_pixel_layout pixel_layout = {.user_sgprs = 8, .push_constant_offset = 64};

static uint32_t output_control; /* the vertex layout's, for the next translation */

static int translate(stage s, const uint32_t *code, size_t dwords, uint32_t **spirv, size_t *words, char *error, size_t error_size)
{
    gcn_vertex_layout vertex = vertex_layout;
    vertex.output_control = output_control;
    if (s == VERTEX) return gcn_translate_vertex(code, dwords, &vertex, spirv, words, error, error_size);
    if (s == PIXEL) return gcn_translate_pixel(code, dwords, &pixel_layout, spirv, words, error, error_size);
    return gcn_translate_compute(code, dwords, &layout, spirv, words, error, error_size);
}

/* The program translates, as the stage given, to a module spirv-val and SPIRV-Cross accept. */
static void translates_as(stage s, const uint32_t *code, size_t dwords, const char *name)
{
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256];
    const int result = translate(s, code, dwords, &spirv, &words, error, sizeof error);
    if (result) fprintf(stderr, "  %s: %s\n", name, error);
    CHECK(result == 0 && spirv && words > 5);
    if (!spirv) return;
    CHECK(spirv[0] == 0x07230203u && spirv[1] == 0x00010500u); /* magic, SPIR-V 1.5 */
    const int valid = validate(spirv, words, name);
    if (valid < 0) fprintf(stderr, "  %s: spirv-val not installed; validation skipped\n", name);
    else CHECK(valid == 1);
    const int metal = to_metal(spirv, words, name);
    if (metal < 0 && getenv("PB_REQUIRE_METAL")) CHECK(!"SPIRV-Cross and the Metal compiler are required here");
    else if (metal < 0) fprintf(stderr, "  %s: SPIRV-Cross not installed; Metal conversion skipped\n", name);
    else CHECK(metal == 1);
    free(spirv);
}

static void translates(const uint32_t *code, size_t dwords, const char *name) { translates_as(COMPUTE, code, dwords, name); }

/* The program is refused, as the stage given, with an error containing the text. */
static void refused_as(stage s, const uint32_t *code, size_t dwords, const char *text)
{
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    CHECK(translate(s, code, dwords, &spirv, &words, error, sizeof error) == -1);
    CHECK(spirv == NULL);
    if (!strstr(error, text)) fprintf(stderr, "  expected \"%s\" in: %s\n", text, error);
    CHECK(strstr(error, text) != NULL);
}

/* The rect-list geometry stage (runtime/rect_list.h) for some parameter and clip-distance counts:
   modules spirv-val accepts; out-of-range counts are refused. */
/* Images: the copy program translates with its two descriptors bound, the read one and the written
   one found by gcn_image_resources; without a binding for one, translation stops naming it. */
static void image_programs(void)
{
    unsigned sgprs[GCN_MAX_IMAGES], access[GCN_MAX_IMAGES], count = 0;
    char error[256] = "";
    CHECK(gcn_image_resources(PROGRAM(image_copy), sgprs, access, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 2 && sgprs[0] == 0 && access[0] == GCN_IMAGE_READ && sgprs[1] == 8 && access[1] == GCN_IMAGE_WRITE);
    const gcn_image_binding images[] = {{0, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4}, {8, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4}};
    gcn_compute_layout with_images = layout;
    with_images.user_sgprs = 16, with_images.images = images, with_images.image_count = 2;
    uint32_t *spirv = NULL;
    size_t words = 0;
    const int translated = gcn_translate_compute(PROGRAM(image_copy), &with_images, &spirv, &words, error, sizeof error);
    if (translated) fprintf(stderr, "image_copy: %s\n", error);
    CHECK(translated == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "image_copy") != 0);
        free(spirv);
        spirv = NULL;
    }
    with_images.image_count = 1;
    CHECK(gcn_translate_compute(PROGRAM(image_copy), &with_images, &spirv, &words, error, sizeof error) == -1 &&
          strstr(error, "s[8:15] is not bound"));
}

static void rect_list_stages(void)
{
    static const unsigned counts[][2] = {{0, 0}, {1, 0}, {4, 2}, {RT_RECT_LIST_MAX_PARAMETERS, RT_RECT_LIST_MAX_CLIP_DISTANCES}};
    for (size_t i = 0; i < sizeof counts / sizeof *counts; i++) {
        uint32_t *spirv = NULL;
        size_t words = 0;
        char name[64];
        snprintf(name, sizeof name, "rect_list_%u_%u", counts[i][0], counts[i][1]);
        CHECK(rt_rect_list_geometry(counts[i][0], counts[i][1], &spirv, &words) == 0 && spirv && spirv[0] == 0x07230203);
        if (!spirv) continue;
        const int valid = validate(spirv, words, name);
        if (valid < 0) fprintf(stderr, "  %s: spirv-val not installed; validation skipped\n", name);
        CHECK(valid != 0);
        free(spirv);
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    CHECK(rt_rect_list_geometry(RT_RECT_LIST_MAX_PARAMETERS + 1, 0, &spirv, &words) == -1);
    CHECK(rt_rect_list_geometry(0, RT_RECT_LIST_MAX_CLIP_DISTANCES + 1, &spirv, &words) == -1);
}

int main(void)
{
    rect_list_stages();
    image_programs();
    translates(PROGRAM(scale_add), "scale_add");
    translates(PROGRAM(integer_and_select), "integer_and_select");
    translates(PROGRAM(constants_and_modifiers), "constants_and_modifiers");
    translates(PROGRAM(divergent_if), "divergent_if");
    translates(PROGRAM(uniform_loop), "uniform_loop");
    translates(PROGRAM(vector_loop), "vector_loop");
    translates(PROGRAM(compare_exec), "compare_exec");
    translates(PROGRAM(masked_store), "masked_store");
    translates(PROGRAM(typed_buffers), "typed_buffers");
    translates(PROGRAM(pack_halves), "pack_halves");
    translates(PROGRAM(integer_batch), "integer_batch");
    translates(PROGRAM(scalar_batch), "scalar_batch");
    translates(PROGRAM(vcc_halves), "vcc_halves");
    translates(PROGRAM(whole_quad), "whole_quad");
    translates(PROGRAM(transcendentals), "transcendentals");
    translates_as(VERTEX, PROGRAM(vertex_position_color), "vertex_position_color");
    translates_as(PIXEL, PROGRAM(pixel_color), "pixel_color");
    translates_as(PIXEL, PROGRAM(pixel_discard_left), "pixel_discard_left");
    translates_as(PIXEL, PROGRAM(pixel_user_color), "pixel_user_color");

    /* user data that would not fit the guaranteed push constants, or not on a word boundary */
    {
        uint32_t *spirv = NULL;
        size_t words = 0;
        char error[256] = "";
        const gcn_pixel_layout past = {.user_sgprs = 8, .push_constant_offset = 68};
        const gcn_pixel_layout unaligned = {.user_sgprs = 8, .push_constant_offset = 2};
        CHECK(gcn_translate_pixel(PROGRAM(pixel_user_color), &past, &spirv, &words, error, sizeof error) == -1 &&
              strstr(error, "push-constant byte 68"));
        CHECK(gcn_translate_pixel(PROGRAM(pixel_user_color), &unaligned, &spirv, &words, error, sizeof error) == -1);
    }
    refused_as(PIXEL, PROGRAM(pixel_p2_alone), "no v_interp_p1");
    refused_as(PIXEL, PROGRAM(pixel_other_barycentrics), "barycentrics in v2");
    refused_as(VERTEX, PROGRAM(vertex_pos1), "target 13");
    output_control = CLIP_DISTANCES_0_1;
    translates_as(VERTEX, PROGRAM(vertex_clip), "vertex_clip");
    output_control = MISC_VECTOR_FIRST;
    refused_as(VERTEX, PROGRAM(vertex_clip), "misc vector");
    output_control = CULL_DISTANCE_0;
    refused_as(VERTEX, PROGRAM(vertex_clip), "cull distance 0");
    output_control = 0;
    refused_as(COMPUTE, PROGRAM(pixel_color), "outside pixel shaders");
    refused_as(PIXEL, PROGRAM(vertex_position_color), "target 32");

    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    CHECK(gcn_translate_compute(PROGRAM(unsupported), &layout, &spirv, &words, error, sizeof error) == -1);
    CHECK(spirv == NULL && strstr(error, "v_interp_p1_f32") != NULL);

    CHECK(gcn_translate_compute(PROGRAM(typed_unorm), &layout, &spirv, &words, error, sizeof error) == -1);
    CHECK(spirv == NULL && strstr(error, "dfmt 10 nfmt 0") != NULL);
    CHECK(gcn_translate_compute(PROGRAM(typed_too_many_components), &layout, &spirv, &words, error, sizeof error) == -1);
    CHECK(spirv == NULL && strstr(error, "dfmt 4 nfmt 7") != NULL);

    const uint32_t no_end[] = {0x7e020302}; /* v_mov_b32 v1, v2, and no s_endpgm */
    CHECK(gcn_translate_compute(no_end, 1, &layout, &spirv, &words, error, sizeof error) == -1);

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
