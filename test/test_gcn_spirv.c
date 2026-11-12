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

static const gcn_compute_layout layout = {.local_size = {64, 1, 1}, .user_sgprs = 8};

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
static gcn_lds compute_lds;     /* the compute layout's, for the next translation */

static int translate(stage s, const uint32_t *code, size_t dwords, uint32_t **spirv, size_t *words, char *error, size_t error_size)
{
    gcn_vertex_layout vertex = vertex_layout;
    vertex.output_control = output_control;
    if (s == VERTEX) return gcn_translate_vertex(code, dwords, &vertex, spirv, words, error, error_size);
    if (s == PIXEL) return gcn_translate_pixel(code, dwords, &pixel_layout, spirv, words, error, error_size);
    gcn_compute_layout compute = layout;
    compute.lds = compute_lds;
    return gcn_translate_compute(code, dwords, &compute, spirv, words, error, error_size);
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
       counts. A branch over the load that lands before the use, a loop back to the use from after a
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
    /* a loop over the use whose closing branch comes before the write (the loading screen's pixel shader at
       0x20d1ce7300 reuses the descriptor's SGPRs only after its loop): still the user data's */
    static const uint32_t user_data_reused_after_loop[] = {0xf0000f00, 0x00000401, 0xbf84fffd, 0xbe800380, 0xbf810000};
    /* a descriptor loaded before a loop over its use (the same shader's sampler in s[16:19]): from memory;
       with a branch from before the load into the loop (to the loop's s_nop), refused */
    static const uint32_t loaded_then_looped[] = {0xc0c00904, 0xf0000f00, 0x00000401, 0xbf84fffd, 0xbf810000};
    static const uint32_t skipped_into_loop[] = {0xbf880003, 0xc0c00904, 0xf0000f00, 0x00000401, 0xbf800000, 0xbf84fffc, 0xbf810000};
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
    const int after_loop = gcn_image_resources(PROGRAM(user_data_reused_after_loop), found, GCN_MAX_IMAGES, &count, error, sizeof error);
    if (after_loop) fprintf(stderr, "user_data_reused_after_loop: %s\n", error);
    CHECK(after_loop == 0 && count == 1 && !found[0].origin.from_memory && found[0].origin.sgpr == 0);
    const int before_loop = gcn_image_resources(PROGRAM(loaded_then_looped), found, GCN_MAX_IMAGES, &count, error, sizeof error);
    if (before_loop) fprintf(stderr, "loaded_then_looped: %s\n", error);
    CHECK(before_loop == 0 && count == 1 && found[0].origin.from_memory && found[0].origin.pointer_sgpr == 8 && found[0].origin.offset == 16);
    CHECK(gcn_image_resources(PROGRAM(skipped_into_loop), found, GCN_MAX_IMAGES, &count, error, sizeof error) == -1 &&
          strstr(error, "lands at dword 4, before a loop's branch back"));
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
    /* from a storage image (one level): translated with a run-time stop word (a level past 0 stops
       there), refused by name without one */
    const gcn_image_binding storage[] = {{{0, 0, 0, 0}, GCN_IMAGE_2D, 0, GCN_IMAGE_FLOAT, 4, 0, 0, 0}};
    pixel.images = storage, pixel.error_address = 0x1000;
    const int from_storage = gcn_translate_pixel(PROGRAM(pixel_image_texel), &pixel, &spirv, &words, error, sizeof error);
    if (from_storage) fprintf(stderr, "storage image_load_mip: %s\n", error);
    CHECK(from_storage == 0);
    if (spirv) {
        CHECK(validate(spirv, words, "pixel_image_texel_storage") != 0);
        free(spirv);
        spirv = NULL;
    }
    pixel.error_address = 0;
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

/* What programs read (gcn_memory_reads): scale_add its user-data buffer s[0:3]; an s_load_dwordx4 at
   dword 4 from the pointer in s[0:1] reads 16 bytes at 16, an s_buffer_load reads its descriptor
   s[4:7]; a flat load may read anywhere (unknown). */
static void memory_reads(void)
{
    gcn_memory_read found[GCN_MAX_IMAGES];
    unsigned count = 99;
    int unknown = 1;
    char error[300] = "";
    CHECK(gcn_memory_reads(PROGRAM(scale_add), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 1 && !unknown && !found[0].pointer && !found[0].origin.from_memory && found[0].origin.sgpr == 0);
    static const uint32_t scalar[] = {
        0xc0880104, /* s_load_dwordx4 s[16:19], s[0:1], 0x4 */
        0xc20a0502, /* s_buffer_load_dword s20, s[4:7], 0x2 */
        0xbf810000, /* s_endpgm */
    };
    CHECK(gcn_memory_reads(PROGRAM(scalar), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 2 && !unknown);
    CHECK(found[0].pointer && found[0].origin.sgpr == 0 && found[0].offset == 16 && found[0].bytes == 16);
    CHECK(!found[1].pointer && found[1].origin.sgpr == 4);
    static const uint32_t flat[] = {0xdc300000, 0x01000002, /* flat_load_dword v1, v[2:3] */ 0xbf810000};
    CHECK(gcn_memory_reads(PROGRAM(flat), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(unknown);
    CHECK(gcn_memory_reads(PROGRAM(table_copy), found, GCN_MAX_IMAGES, &count, &unknown, error, sizeof error) == 0);
    CHECK(count == 2 && !unknown && found[0].origin.sgpr == 0 && found[1].origin.sgpr == 4);
}

/* Table-driven copies (gcn_table_copy_shape): the table in s[0:3] (read as dwords or through its
   format), the source s[4:7], the destination s[8:11], the workgroup index s12. A store with an offset,
   or another program, is not one. */
static void table_copies(void)
{
    gcn_table_copy shape = {0};
    CHECK(gcn_table_copy_shape(PROGRAM(table_copy), &shape) == 1);
    CHECK(shape.table_sgpr == 0 && shape.source_sgpr == 4 && shape.destination_sgpr == 8 && shape.workgroup_sgpr == 12 && !shape.table_format);
    CHECK(gcn_table_copy_shape(PROGRAM(table_copy_format), &shape) == 1 && shape.table_format);
    CHECK(gcn_table_copy_shape(PROGRAM(table_copy_store_offset), &shape) == 0);
    CHECK(gcn_table_copy_shape(PROGRAM(scale_add), &shape) == 0);
    CHECK(gcn_table_copy_shape(PROGRAM(indexed_scale_add), &shape) == 0);
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
    static const unsigned sizes[] = {1, 2, 4, 8, 16}; /* element bytes */
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

/* Whether the module holds the instruction (opcode, then the words given after its word count). */
static int has_instruction(const uint32_t *spirv, size_t words, uint16_t opcode, const uint32_t *operands, size_t count, int skip_first)
{
    for (size_t i = 5; i < words;) {
        const size_t length = spirv[i] >> 16;
        if (!length) break;
        if ((spirv[i] & 0xffff) == opcode && length == count + 1 + (size_t)skip_first &&
            !memcmp(spirv + i + 1 + skip_first, operands, count * sizeof *operands))
            return 1;
        i += length;
    }
    return 0;
}

/* exp mrtz (target 8): with DB_SHADER_CONTROL's Z_EXPORT_ENABLE, its first component becomes FragDepth
   (BuiltIn 22, with the DepthReplacing execution mode); without any enable it is ignored; a stencil
   export enable stops by name. */
static void depth_exports(void)
{
    enum { OP_EXECUTION_MODE = 16, OP_DECORATE = 71, DECORATION_BUILT_IN = 11, BUILT_IN_FRAG_DEPTH = 22, MODE_DEPTH_REPLACING = 12 };
    static const uint32_t exported[] = {0x7e0002f0, /* v_mov_b32 v0, 0.5 */
                                        0xf8001881, 0x00000000, /* exp mrtz v0 done vm */
                                        0xbf810000};
    const uint32_t depth_export[3] = {GCN_DEPTH_EXPORT_Z, 0, 1u << 1};
    for (int k = 0; k < 3; k++) {
        gcn_pixel_layout layout = pixel_layout;
        layout.depth_export = depth_export[k];
        uint32_t *spirv = NULL;
        size_t words = 0;
        char error[256] = "";
        const int result = gcn_translate_pixel(PROGRAM(exported), &layout, &spirv, &words, error, sizeof error);
        if (k == 2) {
            CHECK(result == -1 && strstr(error, "stencil or sample-mask exports"));
            continue;
        }
        if (result) fprintf(stderr, "depth export %d: %s\n", k, error);
        CHECK(result == 0 && spirv);
        if (!spirv) continue;
        const uint32_t frag_depth[2] = {DECORATION_BUILT_IN, BUILT_IN_FRAG_DEPTH}, replacing[1] = {MODE_DEPTH_REPLACING};
        CHECK(has_instruction(spirv, words, OP_DECORATE, frag_depth, 2, 1) == (k == 0));
        CHECK(has_instruction(spirv, words, OP_EXECUTION_MODE, replacing, 1, 1) == (k == 0));
        const int valid = validate(spirv, words, k ? "depth_ignored" : "depth_export");
        CHECK(valid != 0);
        free(spirv);
    }
}

/* A tessellation module: made, and accepted by spirv-val (the Metal executor stops at tessellation
   by name, so these are not converted). */
static void tessellation_module(int result, uint32_t *spirv, size_t words, const char *error, const char *name)
{
    if (result) fprintf(stderr, "  %s: %s\n", name, error);
    CHECK(result == 0 && spirv && words > 5);
    if (!spirv) return;
    const int valid = validate(spirv, words, name);
    if (valid < 0) fprintf(stderr, "  %s: spirv-val not installed; validation skipped\n", name);
    else CHECK(valid == 1);
    free(spirv);
}

/* The LDS in a compute shader (workgroup memory) and the tessellation stages (LDS in a buffer): modules
   spirv-val accepts; a ds_* instruction with no LDS stops by name, as do s_barrier outside a compute
   shader and a workgroup of more than 1024 lanes. */
static void tessellation(void)
{
    compute_lds = (gcn_lds){.bytes = 272};
    translates(PROGRAM(lds_exchange), "lds_exchange");
    compute_lds = (gcn_lds){0};
    refused_as(COMPUTE, PROGRAM(lds_exchange), "with no LDS");
    static const uint32_t barrier_in_vertex[] = {0xbf8a0000, 0xbf810000}; /* s_barrier; s_endpgm */
    refused_as(VERTEX, PROGRAM(barrier_in_vertex), "s_barrier outside a compute shader");

    gcn_tessellation_layout l = {.user_sgprs = 8, .patches = 15, .input_points = 1, .output_points = 1,
                                 .lds = {.bytes = 3584, .address = 0x100000000ull}, .lds_workgroups = 64, .factor_stride = 24,
                                 .factor_ring = 0xfe00000000ull, .factor_ring_bytes = 0x20000, .factor_buffer = 0x200000000ull,
                                 .domain = GCN_TESS_QUADS, .partitioning = GCN_TESS_INTEGER, .topology = GCN_TESS_TRIANGLES_CW};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    int r = gcn_translate_local(PROGRAM(tess_local), &l, &spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "tess_local");
    r = gcn_translate_hull(PROGRAM(tess_hull), &l, &spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "tess_hull");
    l.off_chip = 1;
    r = gcn_translate_hull(PROGRAM(tess_hull), &l, &spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "tess_hull_off_chip");
    l.off_chip = 0;
    static const uint32_t topologies[][3] = {{GCN_TESS_QUADS, GCN_TESS_INTEGER, GCN_TESS_TRIANGLES_CW},
                                             {GCN_TESS_TRIANGLES, GCN_TESS_FRACTIONAL_ODD, GCN_TESS_TRIANGLES_CCW},
                                             {GCN_TESS_TRIANGLES, GCN_TESS_FRACTIONAL_EVEN, GCN_TESS_POINTS},
                                             {GCN_TESS_ISOLINES, GCN_TESS_POW2, GCN_TESS_LINES}};
    for (unsigned i = 0; i < 4; i++) {
        l.domain = topologies[i][0], l.partitioning = topologies[i][1], l.topology = topologies[i][2];
        l.push_constant_offset = 0;
        r = gcn_translate_domain(PROGRAM(tess_domain), &l, &spirv, &words, error, sizeof error);
        tessellation_module(r, spirv, words, error, "tess_domain");
        const gcn_tessellation_control c = {.domain = l.domain, .partitioning = l.partitioning, .min_level = 1.0f, .max_level = 64.0f,
                                            .factor_buffer = 0x200000000ull, .factor_stride = 24};
        r = gcn_tessellation_control_shader(&c, &spirv, &words, error, sizeof error);
        tessellation_module(r, spirv, words, error, "tess_control");
    }
    r = gcn_tessellation_vertex_shader(&spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "tess_vertex");

    l.domain = GCN_TESS_QUADS, l.topology = GCN_TESS_LINES; /* lines are isolines' alone */
    CHECK(gcn_translate_domain(PROGRAM(tess_domain), &l, &spirv, &words, error, sizeof error) == -1 && strstr(error, "VGT_TF_PARAM"));
    l.topology = GCN_TESS_TRIANGLES_CW, l.patches = 64, l.input_points = 32;
    CHECK(gcn_translate_local(PROGRAM(tess_local), &l, &spirv, &words, error, sizeof error) == -1 && strstr(error, "workgroup"));
    const gcn_tessellation_control few = {.domain = GCN_TESS_QUADS, .min_level = 1.0f, .max_level = 64.0f, .factor_buffer = 1, .factor_stride = 16};
    CHECK(gcn_tessellation_control_shader(&few, &spirv, &words, error, sizeof error) == -1 && strstr(error, "fewer"));
}


/* The geometry-shader stages (runtime/gcn_spirv.h): the export and geometry passes and the copy shader make
   modules spirv-val accepts, for each input primitive; an emit to stream 1, a ring load in the export
   shader, a message outside a geometry shader and rings too small for one wave stop by name. */
static void geometry_shaders(void)
{
    gcn_geometry_layout l = {.user_sgprs = 4, .input_primitive = GCN_GS_INPUT_POINTS, .esgs_itemsize = 2, .gsvs_itemsize = 32,
                             .max_vertices = 8, .esgs_ring = 0x100000000ull, .esgs_ring_bytes = 1 << 20, .gsvs_ring = 0x200000000ull,
                             .gsvs_ring_bytes = 1 << 20, .strip_buffer = 0x300000000ull, .strip_buffer_bytes = 1 << 20,
                             .error_address = 0x400000000ull};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    int r = gcn_translate_export(PROGRAM(gs_export), &l, &spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "gs_export");
    for (uint32_t input = GCN_GS_INPUT_POINTS; input <= GCN_GS_INPUT_TRIANGLE_STRIP; input++) {
        l.input_primitive = input;
        r = gcn_translate_geometry(PROGRAM(gs_geometry), &l, &spirv, &words, error, sizeof error);
        tessellation_module(r, spirv, words, error, "gs_geometry");
    }
    const gcn_vertex_layout copy = {.user_sgprs = 4, .gsvs_ring = 0x200000000ull, .gsvs_ring_bytes = 1 << 20};
    r = gcn_translate_vertex(PROGRAM(gs_copy), &copy, &spirv, &words, error, sizeof error);
    tessellation_module(r, spirv, words, error, "gs_copy");

    static const uint32_t stream_one[] = {0xbf900122, 0xbf810000}; /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 1); s_endpgm */
    CHECK(gcn_translate_geometry(PROGRAM(stream_one), &l, &spirv, &words, error, sizeof error) == -1 && strstr(error, "stream 1"));
    CHECK(gcn_translate_export(PROGRAM(gs_geometry), &l, &spirv, &words, error, sizeof error) == -1 && strstr(error, "load of the ESGS ring"));
    static const uint32_t emit_only[] = {0xbf900022, 0xbf810000};
    refused_as(VERTEX, PROGRAM(emit_only), "outside a geometry shader");
    l.esgs_ring_bytes = 256;
    CHECK(gcn_translate_export(PROGRAM(gs_export), &l, &spirv, &words, error, sizeof error) == -1 && strstr(error, "rings"));
}

int main(void)
{
    tessellation();
    geometry_shaders();
    rect_list_stages();
    tile_copy_shaders();
    store_resources();
    memory_reads();
    table_copies();
    vector_stores();
    image_programs();
    calls();
    user_data_read();
    depth_exports();
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
    /* ds_swizzle_b32's quad mode with one source lane: compute and pixel shaders; other patterns, the
       bit-mask mode and vertex shaders stop by name */
    translates(PROGRAM(quad_swizzle), "quad_swizzle");
    translates_as(PIXEL, PROGRAM(pixel_quad_swizzle), "pixel_quad_swizzle");
    static const uint32_t mixed_quad[] = {0xd8d480e4, 0x03000002, 0xbf810000}; /* ds_swizzle_b32 v3, v2 offset:swizzle(QUAD_PERM,0,1,2,3) */
    static const uint32_t bit_mask[] = {0xd8d4041f, 0x03000002, 0xbf810000};   /* ds_swizzle_b32 v3, v2 offset:0x41f */
    refused_as(COMPUTE, PROGRAM(mixed_quad), "offset 0x80e4");
    refused_as(COMPUTE, PROGRAM(bit_mask), "offset 0x41f");
    refused_as(VERTEX, PROGRAM(quad_swizzle), "in a vertex shader");
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
