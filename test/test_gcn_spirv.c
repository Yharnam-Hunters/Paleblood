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
#include "runtime/tile_copy.h"

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
    gcn_image_resource found[GCN_MAX_IMAGES];
    unsigned count = 0;
    char error[300] = "";
    CHECK(gcn_image_resources(PROGRAM(image_copy), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 2 && !found[0].origin.from_memory && found[0].origin.sgpr == 0 && found[0].access == GCN_IMAGE_READ &&
          !found[1].origin.from_memory && found[1].origin.sgpr == 8 && found[1].access == GCN_IMAGE_WRITE);
    const gcn_image_binding images[] = {{{0, 0, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 0, 0, 0}, {{0, 8, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 0, 0, 0}};
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

    /* Where each MIMG's descriptor comes from. A whole s_load_dwordx8 from a user-data pointer: from
       memory, at the load's offset. Two uses of s[4:11], before and after such a load over it: two
       images, the user data's and the loaded one. A scalar move into it or a compare's 64-bit
       result: refused by name. */
    /* sixteen descriptors (s_load_dwordx8 s[0:7], s[8:9], 8k, then image_load each): sixteen images; a
       seventeenth is more than a program may use */
    {
        static uint32_t many[3 * 17 + 1];
        for (unsigned limit = 16; limit <= 17; limit++) {
            unsigned n = 0;
            for (unsigned k = 0; k < limit; k++) many[n++] = 0xc0c00900u | 8 * k, many[n++] = 0xf0000f00, many[n++] = 0x00000401;
            many[n++] = 0xbf810000;
            gcn_image_resource sixteen[GCN_MAX_IMAGES];
            unsigned images = 0;
            const int listed = gcn_image_resources(many, n, sixteen, GCN_MAX_IMAGES, &images, error, sizeof error);
            if (limit == 16) CHECK(listed == 0 && images == 16 && sixteen[15].origin.offset == 15 * 32);
            else CHECK(listed == -1 && strstr(error, "more than 16 images"));
        }
    }
    static const uint32_t loaded[] = {0xc0c00904, 0xf0000f00, 0x00000401, 0xbf810000};   /* s_load_dwordx8 s[0:7], s[8:9], 4 */
    static const uint32_t loaded_literal[] = {0xc0c008ff, 0x00000004, 0xf0000f00, 0x00000401, 0xbf810000}; /* the same at literal offset 4 */
    /* s_load_dwordx16 s[0:15], s[8:9], 4: the descriptor in its second half (s[8:15], MIMG SRSRC 2) is at 16 + 32 bytes */
    static const uint32_t wider[] = {0xc1000904, 0xf0000f00, 0x00020401, 0xbf810000};
    /* s_load_dwordx4 s[8:11]: only half the descriptor */
    static const uint32_t narrower[] = {0xc0840904, 0xf0000f00, 0x00020401, 0xbf810000};
    static const uint32_t moved[] = {0xbe8c0380, 0xf0000f00, 0x00020401, 0xbf810000};    /* s_mov_b32 s12, 0 */
    static const uint32_t compared[] = {0xd184000e, 0x00020300, 0xf0000f00, 0x00020401, 0xbf810000}; /* v_cmp_eq_u32_e64 s[14:15] */
    static const uint32_t pointer_written[] = {0xbe880380, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbf810000}; /* s_mov_b32 s8, 0 first */
    static uint32_t branch_first[64];
    branch_first[0] = 0xbf840000;                                                      /* s_cbranch_scc0 to the next instruction */
    memcpy(branch_first + 1, pixel_two_images, sizeof pixel_two_images);
    CHECK(gcn_image_resources(PROGRAM(loaded), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 1 && found[0].origin.from_memory && found[0].origin.sgpr == 0 && found[0].origin.pointer_sgpr == 8 &&
          found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(loaded_literal), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 && count == 1 &&
          found[0].origin.from_memory && found[0].origin.pointer_sgpr == 8 && found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(wider), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 1 && found[0].origin.from_memory && found[0].origin.sgpr == 8 && found[0].origin.pointer_sgpr == 8 &&
          found[0].origin.offset == 48);
    CHECK(gcn_image_resources(PROGRAM(narrower), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "not loaded from a user-data pointer"));
    CHECK(gcn_image_resources(PROGRAM(pixel_two_images), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 2 && !found[0].origin.from_memory && found[0].origin.sgpr == 4 && found[1].origin.from_memory &&
          found[1].origin.pointer_sgpr == 2 && found[1].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(moved), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "s[8:15] is written by the program (s_mov_b32"));
    CHECK(gcn_image_resources(PROGRAM(compared), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "v_cmp_eq_u32"));
    CHECK(gcn_image_resources(PROGRAM(pointer_written), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "loaded through s[8:9], which the program writes"));
    /* a forward branch before both: the same two images (the load over s[4:11] comes after the first use) */
    CHECK(gcn_image_resources(branch_first, 1 + sizeof pixel_two_images / 4, found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 &&
          count == 2 && !found[0].origin.from_memory && found[0].origin.sgpr == 4 && found[1].origin.from_memory);
    /* With branches: a load followed when every path to the use passes it. An s_cbranch_execz over
       both the load and the use (the game's guarded samples): from memory; with a compare's mask
       in those SGPRs before the load, or an earlier load into them, the load before the use still
       counts. A branch over the load that lands before the use, a loop back to the use after a
       write to those SGPRs, or a write to the pointer before the load or (with a branch back to the
       load) anywhere: refused by name. A write to the pointer after the use, with no branch back,
       does not matter. */
    static const uint32_t guarded[] = {0xbf880003, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbf810000}; /* s_cbranch_execz to s_endpgm */
    static const uint32_t skipped_load[] = {0xbf840001, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbf810000}; /* s_cbranch_scc0 to the use */
    static const uint32_t loaded_twice[] = {0xbf880004, 0xc0c00904, 0xc0c00908, 0xf0000f00, 0x00000401, 0xbf810000}; /* offsets 16, 32 */
    static const uint32_t masked_first[] = {0xd0020000, 0x00020100, 0xbf880003, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbf810000}; /* v_cmp_lt_f32 s[0:1] */
    static const uint32_t looped[] = {0xc0c00904, 0xf0000f00, 0x00000401, 0xbe800380, 0xbf84fffc, 0xbf810000}; /* s_mov_b32 s0, 0; back to the use */
    /* the user data's descriptor at a use behind a forward branch, its SGPRs written only after the use:
       still the user data's; with a branch back to the use after that write, refused */
    static const uint32_t user_data_later[] = {0xbf880003, 0xf0000f00, 0x00000401, 0xbe800380, 0xbf810000}; /* s_mov_b32 s0, 0 after */
    static const uint32_t user_data_looped[] = {0xf0000f00, 0x00000401, 0xbe800380, 0xbf84fffc, 0xbf810000};  /* then back to the use */
    static const uint32_t pointer_later[] = {0xbf880004, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbe880380, 0xbf810000}; /* s_mov_b32 s8, 0 after */
    static const uint32_t pointer_looped[] = {0xc0c00904, 0xf0000f00, 0x00000401, 0xbe880380, 0xbf84fffb, 0xbf810000}; /* then back to the load */
    CHECK(gcn_image_resources(PROGRAM(guarded), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 1 && found[0].origin.from_memory && found[0].origin.pointer_sgpr == 8 && found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(skipped_load), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "lands at dword 2, between the load and its use"));
    CHECK(gcn_image_resources(PROGRAM(loaded_twice), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 &&
          count == 1 && found[0].origin.offset == 32);
    CHECK(gcn_image_resources(PROGRAM(masked_first), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 &&
          count == 1 && found[0].origin.from_memory && found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(looped), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "s_cbranch_scc0"));
    CHECK(gcn_image_resources(PROGRAM(user_data_later), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 && count == 1 &&
          !found[0].origin.from_memory && found[0].origin.sgpr == 0);
    CHECK(gcn_image_resources(PROGRAM(user_data_looped), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "s_cbranch_scc0"));
    CHECK(gcn_image_resources(PROGRAM(pointer_later), found, GCN_MAX_IMAGES, &count, error, sizeof error) == 0 && count == 1 &&
          found[0].origin.from_memory && found[0].origin.pointer_sgpr == 8 && found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(pointer_looped), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "loaded through s[8:9], which the program writes"));
    /* the two-image program translates with both bound, and stops naming the loaded one without it */
    const gcn_image_binding two[] = {{{0, 4, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0}, {{1, 4, 2, 16}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0}};
    gcn_pixel_layout both_images = pixel_layout;
    both_images.user_sgprs = 12, both_images.images = two, both_images.image_count = 2, both_images.error_address = 0x1000;
    const int two_translated = gcn_translate_pixel(PROGRAM(pixel_two_images), &both_images, &spirv, &words, error, sizeof error);
    if (two_translated) fprintf(stderr, "pixel_two_images: %s\n", error);
    CHECK(two_translated == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "pixel_two_images") != 0);
        free(spirv);
        spirv = NULL;
    }
    both_images.image_count = 1;
    CHECK(gcn_translate_pixel(PROGRAM(pixel_two_images), &both_images, &spirv, &words, error, sizeof error) == -1 &&
          strstr(error, "(loaded from memory) is not bound"));

    /* image_sample: the lookup program's sampler is found in the user data (s[12:15]) and its 1D array
       loaded from memory; it translates and validates with both images and the sampler bound, and
       stops naming the sampler without it. */
    gcn_resource_origin samplers[GCN_MAX_SAMPLERS];
    CHECK(gcn_sampler_resources(PROGRAM(pixel_lookup), samplers, GCN_MAX_SAMPLERS, &count, error, sizeof error) == 0);
    CHECK(count == 1 && !samplers[0].from_memory && samplers[0].sgpr == 12);
    const gcn_image_binding lookup_images[] = {{{0, 4, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0},
                                               {{1, 0, 2, 0}, GCN_IMAGE_1D, 1, GCN_IMAGE_FLOAT, 1, 1, 0, 0}};
    gcn_pixel_layout lookup = pixel_layout;
    lookup.user_sgprs = 16, lookup.images = lookup_images, lookup.image_count = 2, lookup.samplers = samplers, lookup.sampler_count = 1;
    lookup.error_address = 0x1000;
    const int sampled = gcn_translate_pixel(PROGRAM(pixel_lookup), &lookup, &spirv, &words, error, sizeof error);
    if (sampled) fprintf(stderr, "pixel_lookup: %s\n", error);
    CHECK(sampled == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "pixel_lookup") != 0);
        CHECK(to_metal(spirv, words, "pixel_lookup") != 0);
        free(spirv);
        spirv = NULL;
    }
    lookup.sampler_count = 0;
    CHECK(gcn_translate_pixel(PROGRAM(pixel_lookup), &lookup, &spirv, &words, error, sizeof error) == -1 &&
          strstr(error, "sampler in s[12:15] is not bound"));
    const gcn_image_binding flat[] = {{{0, 4, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0}, {{1, 0, 2, 0}, GCN_IMAGE_1D, 0, GCN_IMAGE_FLOAT, 1, 1, 0, 0}};
    lookup.images = flat, lookup.sampler_count = 1;
    CHECK(gcn_translate_pixel(PROGRAM(pixel_lookup), &lookup, &spirv, &words, error, sizeof error) == -1 && strstr(error, "DA 1"));

    /* A pixel shader's image_load_mip from a sampled image (its range checked at run time, so only
       with a stop word); a storage binding refuses image_load_mip. */
    const gcn_image_binding texture[] = {{{0, 0, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0}};
    gcn_pixel_layout pixel = pixel_layout;
    pixel.images = texture, pixel.image_count = 1, pixel.error_address = 0x1000;
    const int fetched = gcn_translate_pixel(PROGRAM(pixel_image_texel), &pixel, &spirv, &words, error, sizeof error);
    if (fetched) fprintf(stderr, "pixel_image_texel: %s\n", error);
    CHECK(fetched == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "pixel_image_texel") != 0);
        CHECK(to_metal(spirv, words, "pixel_image_texel") != 0);
        free(spirv);
        spirv = NULL;
    }
    pixel.error_address = 0;
    CHECK(gcn_translate_pixel(PROGRAM(pixel_image_texel), &pixel, &spirv, &words, error, sizeof error) == -1 &&
          strstr(error, "run-time stop word"));
    const gcn_image_binding storage[] = {{{0, 0, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 0, 0, 0}};
    pixel.images = storage, pixel.error_address = 0x1000;
    CHECK(gcn_translate_pixel(PROGRAM(pixel_image_texel), &pixel, &spirv, &words, error, sizeof error) == -1 &&
          strstr(error, "image_load_mip"));
}

/* Calls: gcn_call_sgpr finds the fetch-shader call's target pair; the program translates with the
   callee inlined and validates; a callee that branches or returns through another pair, and a call
   through a pair written first, stop by name. */
static void calls(void)
{
    unsigned pair = 99;
    char error[300] = "";
    CHECK(gcn_call_sgpr(PROGRAM(vertex_calling_fetch), &pair, error, sizeof error) == 1 && pair == 0);
    CHECK(gcn_call_sgpr(PROGRAM(vertex_position_color), &pair, error, sizeof error) == 0);
    static const uint32_t written_first[] = {0xbe800380, 0xbe802100, 0xbf810000}; /* s_mov_b32 s0, 0 first */
    CHECK(gcn_call_sgpr(PROGRAM(written_first), &pair, error, sizeof error) == -1 && strstr(error, "after s_mov_b32"));
    gcn_vertex_layout layout = vertex_layout;
    layout.callee = fetch_position_color, layout.callee_dwords = sizeof fetch_position_color / 4, layout.code_address = 0x1000;
    layout.error_address = 0x2000;
    uint32_t *spirv = NULL;
    size_t words = 0;
    const int translated = gcn_translate_vertex(PROGRAM(vertex_calling_fetch), &layout, &spirv, &words, error, sizeof error);
    if (translated) fprintf(stderr, "vertex_calling_fetch: %s\n", error);
    CHECK(translated == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "vertex_calling_fetch") != 0);
        free(spirv);
        spirv = NULL;
    }
    static uint32_t branching[16], other_return[16];
    memcpy(branching, fetch_position_color, sizeof fetch_position_color);
    branching[2] = 0xbf840000;                            /* s_cbranch_scc0 for the s_waitcnt */
    memcpy(other_return, fetch_position_color, sizeof fetch_position_color);
    other_return[sizeof fetch_position_color / 4 - 1] = 0xbe802002; /* s_setpc_b64 s[2:3] */
    layout.callee = branching;
    CHECK(gcn_translate_vertex(PROGRAM(vertex_calling_fetch), &layout, &spirv, &words, error, sizeof error) == -1 && strstr(error, "s_cbranch_scc0"));
    layout.callee = other_return;
    CHECK(gcn_translate_vertex(PROGRAM(vertex_calling_fetch), &layout, &spirv, &words, error, sizeof error) == -1 && strstr(error, "returns through s[2:3]"));
    layout.callee = NULL;
    CHECK(gcn_translate_vertex(PROGRAM(vertex_calling_fetch), &layout, &spirv, &words, error, sizeof error) == -1 && strstr(error, "no callee"));

    /* An image descriptor in the user data that the fetch shader leaves alone keeps its origin past the
       call: listed, and translated as a vertex shader's sampled image at the binding given; without the
       callee the call counts as writing every SGPR, and a callee that loads into those SGPRs stops. */
    gcn_image_resource found[GCN_MAX_IMAGES];
    unsigned count = 0;
    CHECK(gcn_called_image_resources(PROGRAM(vertex_fetch_then_image), fetch_position_only, sizeof fetch_position_only / 4, found,
                                     GCN_MAX_IMAGES, &count, error, sizeof error) == 0);
    CHECK(count == 1 && !found[0].origin.from_memory && found[0].origin.sgpr == 8 && found[0].access == GCN_IMAGE_READ);
    CHECK(gcn_image_resources(PROGRAM(vertex_fetch_then_image), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "s_swappc_b64"));
    CHECK(gcn_called_image_resources(PROGRAM(vertex_fetch_then_image), fetch_into_image_sgprs, sizeof fetch_into_image_sgprs / 4, found,
                                     GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "s_swappc_b64"));
    const gcn_image_binding texture[] = {{{0, 8, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 1, 0, 0}};
    layout.callee = fetch_position_only, layout.callee_dwords = sizeof fetch_position_only / 4;
    layout.images = texture, layout.image_count = 1, layout.first_binding = 3;
    const int loaded = gcn_translate_vertex(PROGRAM(vertex_fetch_then_image), &layout, &spirv, &words, error, sizeof error);
    if (loaded) fprintf(stderr, "vertex_fetch_then_image: %s\n", error);
    CHECK(loaded == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "vertex_fetch_then_image") != 0);
        int bound_at_3 = 0; /* OpDecorate %image Binding 3 */
        for (size_t i = 0; i + 3 < words; i++)
            if (spirv[i] == (4u << 16 | 71u) && spirv[i + 2] == 33u && spirv[i + 3] == 3u) bound_at_3 = 1;
        CHECK(bound_at_3);
        free(spirv);
        spirv = NULL;
    }
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

/* Buffer stores' descriptors (gcn_buffer_store_resources): scale_add stores through the user data's
   s[4:7]; a store through a descriptor the program writes first has no known origin (unknown); a
   program with no buffer stores lists none. */
static void store_resources(void)
{
    gcn_resource_origin found[GCN_MAX_IMAGES];
    unsigned count = 99;
    int unknown = 1;
    char error[300] = "";
    CHECK(gcn_buffer_store_resources(PROGRAM(scale_add), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 1 && !unknown && !found[0].from_memory && found[0].sgpr == 4);
    static const uint32_t computed[] = {0xbe840380, 0xe0701000, 0x80010000, 0xbf810000}; /* s_mov_b32 s4, 0; buffer_store_dword ... s[4:7] */
    CHECK(gcn_buffer_store_resources(PROGRAM(computed), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 0 && unknown);
    CHECK(gcn_buffer_store_resources(PROGRAM(image_copy), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 0 && !unknown);
}

/* A buffer_store_dwordx4 translates to a valid module that holds a store of the whole 4-dword vector
   (an OpTypePointer, opcode 32, in PhysicalStorageBuffer, 5349, to a vector, opcode 23, of four 32-bit
   unsigned integers): stores of separate dwords cross the bus to host memory a dword at a time.
   test_gcn_compute runs it, aligned and not, partly in range. */
static void vector_stores(void)
{
    translates(store_dwordx4_aligned, sizeof store_dwordx4_aligned / sizeof *store_dwordx4_aligned, "store_dwordx4_aligned");
    translates(store_dwordx4_unaligned, sizeof store_dwordx4_unaligned / sizeof *store_dwordx4_unaligned, "store_dwordx4_unaligned");
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256];
    CHECK(translate(COMPUTE, store_dwordx4_aligned, sizeof store_dwordx4_aligned / sizeof *store_dwordx4_aligned, &spirv, &words, error,
                    sizeof error) == 0);
    if (!spirv) return;
    enum { OP_TYPE_INT = 21, OP_TYPE_VECTOR = 23, OP_TYPE_POINTER = 32, PHYSICAL_STORAGE_BUFFER = 5349, MAX_ID = 1 << 16 };
    static uint8_t uint32_type[MAX_ID], uvec4_type[MAX_ID];
    memset(uint32_type, 0, sizeof uint32_type), memset(uvec4_type, 0, sizeof uvec4_type);
    int vector_pointer = 0;
    for (size_t at = 5; at < words && spirv[at] >> 16; at += spirv[at] >> 16) {
        const uint32_t *in = spirv + at;
        const unsigned opcode = in[0] & 0xffff;
        if (opcode == OP_TYPE_INT && in[1] < MAX_ID && in[2] == 32 && in[3] == 0) uint32_type[in[1]] = 1;
        if (opcode == OP_TYPE_VECTOR && in[1] < MAX_ID && in[2] < MAX_ID && uint32_type[in[2]] && in[3] == 4) uvec4_type[in[1]] = 1;
        if (opcode == OP_TYPE_POINTER && in[2] == PHYSICAL_STORAGE_BUFFER && in[3] < MAX_ID && uvec4_type[in[3]]) vector_pointer = 1;
    }
    CHECK(vector_pointer);
    free(spirv);
}

/* The renderer's GPU tile copies (runtime/tile_copy.h), every element size and direction: valid modules;
   other element sizes are refused. test_render_executor runs them against rt_tiling_offset. */
static void tile_copy_shaders(void)
{
    static const unsigned sizes[] = {1, 2, 4};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++)
        for (int to_tiled = 0; to_tiled < 2; to_tiled++) {
            uint32_t *spirv = NULL;
            size_t words = 0;
            char name[64];
            snprintf(name, sizeof name, "tile_copy_%u_%s", sizes[i], to_tiled ? "to_tiled" : "from_tiled");
            CHECK(rt_tile_copy_shader(sizes[i], to_tiled, &spirv, &words) == 0 && spirv && spirv[0] == 0x07230203);
            if (!spirv) continue;
            const int valid = validate(spirv, words, name);
            if (valid < 0) fprintf(stderr, "  %s: spirv-val not installed; validation skipped\n", name);
            CHECK(valid != 0);
            /* the element moves in one store of its whole size (OpStore, opcode 62), not word by word:
               word stores with gaps between them cross the bus to host memory one word at a time */
            unsigned stores = 0;
            for (size_t at = 5; at < words && spirv[at] >> 16; at += spirv[at] >> 16) stores += (spirv[at] & 0xffff) == 62;
            CHECK(stores == 1);
            free(spirv);
        }
    uint32_t *spirv = NULL;
    size_t words = 0;
    CHECK(rt_tile_copy_shader(3, 0, &spirv, &words) == -1);
}

/* The user data a program reads before writing it, in program order: s_mov_b32 s5, s2; s_mov_b32 s2, 0;
   s_mov_b32 s6, s2 reads s2 (only its first read is the user data's), and writes s5 and s6; the
   starting registers' setup counts as neither. */
static void user_data_read(void)
{
    static const uint32_t program[] = {0xbe850302, 0xbe820380, 0xbe860302, 0xbf810000};
    gcn_compute_layout reporting = layout;
    uint32_t read = ~0u;
    reporting.user_data_read = &read;
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    CHECK(gcn_translate_compute(program, sizeof program / sizeof *program, &reporting, &spirv, &words, error, sizeof error) == 0);
    free(spirv);
    CHECK(read == 1u << 2);
}

int main(void)
{
    rect_list_stages();
    tile_copy_shaders();
    store_resources();
    vector_stores();
    image_programs();
    calls();
    user_data_read();
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
