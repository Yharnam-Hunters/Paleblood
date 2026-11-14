/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pbgcn_survey: translates a GCN (GFX7) shader program from standard input, little-endian, to SPIR-V
   in the translator's survey mode (gcn_spirv_set_survey), with a layout made from the stage and the
   settings on the command line instead of a draw's, and prints every reason the translator gives, one
   a line, after the dword it was first met at ("@dword reason"); nothing when it translates.
   game/render/tools/shader_survey.py runs it on the shaders of your own copy of the game, for the list
   of what the translator lacks (docs/RENDERER.md, "Coverage survey").

   usage: pbgcn_survey STAGE [user=N] [inputs=MASK] [threads=X,Y,Z] [groups=BITS] [lds=BYTES]
                       [control=WORD] [depth=BITS] [spirv=PATH]
     STAGE    vertex, pixel, compute, local, hull, domain, export or geometry
     user     the user SGPRs (SPI_SHADER_PGM_RSRC2's USER_SGPR)
     inputs   a pixel shader's SPI_PS_INPUT_ADDR (default: the perspective centre's barycentrics)
     threads  a compute shader's workgroup size (default 64,1,1)
     groups   a compute shader's TGID_X/Y/Z_EN bits (RSRC2 bits 7-9, shifted down)
     lds      a compute or local shader's LDS bytes
     control  a vertex shader's PA_CL_VS_OUT_CNTL
     depth    a pixel shader's depth exports (GCN_DEPTH_EXPORT_*)
     spirv    where to write the SPIR-V when it translates (for spirv-val)
   The program's images and samplers are bound as the code shows them (gcn_image_resources,
   gcn_sampler_resources): 2D float images of four components, sampled unless written; a call to a fetch
   shader calls one that only returns. A setting that only a draw knows (an image's real shape, the fetch
   shader's loads) can make a reason here that a draw
   would not have, and the other way round: the survey lists what the code asks for.
   Exit status: 0 translated, 1 not translated (the reasons printed), 2 bad usage or input. */
#include "runtime/gcn.h"
#include "runtime/gcn_spirv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { REASONS_BYTES = 1 << 16, STOP_WORD_ADDRESS = 0x1000, CODE_ADDRESS = 0x10000, RING_ADDRESS = 0x100000, RING_BYTES = 0x100000,
       DEFAULT_PERSP_CENTER = 1u << 1 };

typedef struct {
    unsigned user;
    uint32_t inputs, threads[3], groups, lds, control, depth;
    const char *spirv_path;
} settings;

static uint32_t *read_input(size_t *length)
{
    size_t capacity = 1 << 16;
    uint32_t *code = malloc(capacity * sizeof *code);
    if (!code) return NULL;
    size_t got;
    *length = 0;
    while ((got = fread(code + *length, sizeof *code, capacity - *length, stdin)) > 0) {
        *length += got;
        if (*length == capacity) {
            uint32_t *bigger = realloc(code, (capacity *= 2) * sizeof *code);
            if (!bigger) return free(code), NULL;
            code = bigger;
        }
    }
    return code;
}

static int parse_setting(const char *arg, settings *s)
{
    const char *value = strchr(arg, '=');
    if (!value) return -1;
    value++;
    const size_t key = (size_t)(value - 1 - arg);
    if (!strncmp(arg, "user", key)) s->user = (unsigned)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "inputs", key)) s->inputs = (uint32_t)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "groups", key)) s->groups = (uint32_t)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "lds", key)) s->lds = (uint32_t)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "control", key)) s->control = (uint32_t)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "depth", key)) s->depth = (uint32_t)strtoul(value, NULL, 0);
    else if (!strncmp(arg, "spirv", key)) s->spirv_path = value;
    else if (!strncmp(arg, "threads", key)) {
        if (sscanf(value, "%u,%u,%u", &s->threads[0], &s->threads[1], &s->threads[2]) != 3) return -1;
    } else return -1;
    return 0;
}

/* the VGPR a pixel input starts in: each enabled input before it takes its VGPRs (SPI_PS_INPUT_ADDR order) */
static unsigned pixel_input_vgpr(uint32_t inputs, unsigned bit)
{
    static const unsigned sizes[16] = {2, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    unsigned vgpr = 0;
    for (unsigned b = 0; b < bit; b++)
        if (inputs >> b & 1) vgpr += sizes[b];
    return vgpr;
}

/* The images and samplers the code shows, bound as the survey binds them. */
static int bind_resources(const uint32_t *code, size_t dwords, int pixel, gcn_image_binding *images, unsigned *image_count,
                          gcn_resource_origin *samplers, unsigned *sampler_count)
{
    gcn_image_resource resources[GCN_MAX_IMAGES];
    char reason[256];
    *image_count = *sampler_count = 0;
    if (gcn_image_resources(code, dwords, resources, GCN_MAX_IMAGES, image_count, reason, sizeof reason)) {
        printf("@0 images: %s\n", reason);
        return -1;
    }
    for (unsigned i = 0; i < *image_count; i++)
        images[i] = (gcn_image_binding){.origin = resources[i].origin, .dim = GCN_IMAGE_2D, .type = GCN_IMAGE_FLOAT, .components = 4,
                                        .sampled = !(resources[i].access & GCN_IMAGE_WRITE), .swizzle = 0 /* the identity */};
    if (pixel && gcn_sampler_resources(code, dwords, samplers, GCN_MAX_SAMPLERS, sampler_count, reason, sizeof reason)) {
        printf("@0 samplers: %s\n", reason);
        return -1;
    }
    return 0;
}

/* A fetch shader for the program's call, if it makes one: only the closing s_setpc_b64 to the call's return
   pair (SOP1 s_setpc_b64, its SSRC0 that pair), so that the rest of the program is surveyed; the real one is
   made at run time from the draw's vertex inputs. */
static size_t stand_in_callee(const uint32_t *code, size_t dwords, uint32_t *callee)
{
    enum { S_SETPC_B64 = 0xbe802000u };
    const long program = gcn_program_dwords(code, dwords);
    for (size_t at = 0; program > 0 && at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) return 0;
        if (!strcmp(ins.name, "s_swappc_b64")) {
            *callee = S_SETPC_B64 | ((code[at] >> 16) & 0x7f);
            return 1;
        }
        at += ins.dwords;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: pbgcn_survey STAGE [setting=value ...] < code\n");
        return 2;
    }
    const char *stage = argv[1];
    settings s = {.inputs = DEFAULT_PERSP_CENTER, .threads = {64, 1, 1}};
    for (int i = 2; i < argc; i++)
        if (parse_setting(argv[i], &s)) {
            fprintf(stderr, "pbgcn_survey: bad setting %s\n", argv[i]);
            return 2;
        }
    size_t dwords = 0;
    uint32_t *code = read_input(&dwords);
    if (!code || !dwords) {
        fprintf(stderr, "pbgcn_survey: no program on standard input\n");
        return 2;
    }
    gcn_image_binding images[GCN_MAX_IMAGES];
    gcn_resource_origin samplers[GCN_MAX_SAMPLERS];
    unsigned image_count = 0, sampler_count = 0;
    const int pixel = !strcmp(stage, "pixel");
    uint32_t callee = 0;
    const size_t callee_dwords = stand_in_callee(code, dwords, &callee);
    const int bound = bind_resources(code, dwords, pixel, images, &image_count, samplers, &sampler_count);

    gcn_spirv_set_survey(1);
    char *reasons = calloc(1, REASONS_BYTES);
    uint32_t *spirv = NULL, user_read = 0;
    size_t spirv_words = 0;
    int result = -1;
    if (!reasons) return 2;
    if (!strcmp(stage, "vertex")) {
        const gcn_vertex_layout l = {.user_sgprs = s.user, .output_control = s.control, .error_address = STOP_WORD_ADDRESS,
                                     .callee = callee_dwords ? &callee : NULL, .callee_dwords = callee_dwords, .code_address = CODE_ADDRESS,
                                     .images = images, .image_count = image_count, .user_data_read = &user_read};
        result = gcn_translate_vertex(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES);
    } else if (pixel) {
        const gcn_pixel_layout l = {.user_sgprs = s.user, .barycentric_vgpr = pixel_input_vgpr(s.inputs, 1),
                                    .position_enable = (s.inputs >> 8 & 1 ? GCN_PIXEL_POSITION_X : 0u) | (s.inputs >> 9 & 1 ? GCN_PIXEL_POSITION_Y : 0u),
                                    .position_vgpr = pixel_input_vgpr(s.inputs, 8), .front_face = (s.inputs >> 12 & 1) != 0,
                                    .front_face_vgpr = pixel_input_vgpr(s.inputs, 12), .push_constant_offset = 64,
                                    .error_address = STOP_WORD_ADDRESS, .images = images, .image_count = image_count, .samplers = samplers,
                                    .sampler_count = sampler_count, .depth_export = s.depth, .user_data_read = &user_read};
        result = gcn_translate_pixel(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES);
    } else if (!strcmp(stage, "compute")) {
        const gcn_compute_layout l = {.local_size = {s.threads[0], s.threads[1], s.threads[2]}, .user_sgprs = s.user,
                                      .workgroup_id_enables = s.groups, .error_address = STOP_WORD_ADDRESS, .images = images,
                                      .image_count = image_count, .user_data_read = &user_read, .lds = {.bytes = s.lds}};
        result = gcn_translate_compute(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES);
    } else if (!strcmp(stage, "local") || !strcmp(stage, "hull") || !strcmp(stage, "domain")) {
        const gcn_tessellation_layout l = {.user_sgprs = s.user, .patches = 1, .input_points = 3, .output_points = 3,
                                           .error_address = STOP_WORD_ADDRESS, .lds = {.bytes = s.lds ? s.lds : 4096, .address = RING_ADDRESS},
                                           .lds_workgroups = 16, .factor_stride = 16, .factor_ring = RING_ADDRESS * 4,
                                           .factor_ring_bytes = RING_BYTES, .factor_buffer = RING_ADDRESS * 8, .output_control = s.control,
                                           .callee = callee_dwords ? &callee : NULL, .callee_dwords = callee_dwords, .code_address = CODE_ADDRESS,
                                           .images = images, .image_count = image_count, .domain = GCN_TESS_TRIANGLES,
                                           .topology = GCN_TESS_TRIANGLES_CW, .user_data_read = &user_read};
        result = !strcmp(stage, "local") ? gcn_translate_local(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES)
                 : !strcmp(stage, "hull") ? gcn_translate_hull(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES)
                                          : gcn_translate_domain(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES);
    } else if (!strcmp(stage, "export") || !strcmp(stage, "geometry")) {
        const gcn_geometry_layout l = {.user_sgprs = s.user, .input_primitive = GCN_GS_INPUT_TRIANGLES, .esgs_itemsize = 16,
                                       .gsvs_itemsize = 64, .max_vertices = 4, .esgs_ring = RING_ADDRESS, .esgs_ring_bytes = RING_BYTES,
                                       .gsvs_ring = RING_ADDRESS * 2, .gsvs_ring_bytes = RING_BYTES, .strip_buffer = RING_ADDRESS * 3,
                                       .strip_buffer_bytes = RING_BYTES, .error_address = STOP_WORD_ADDRESS, .user_data_read = &user_read,
                                       .callee = callee_dwords ? &callee : NULL, .callee_dwords = callee_dwords, .code_address = CODE_ADDRESS};
        result = !strcmp(stage, "export") ? gcn_translate_export(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES)
                                          : gcn_translate_geometry(code, dwords, &l, &spirv, &spirv_words, reasons, REASONS_BYTES);
    } else {
        fprintf(stderr, "pbgcn_survey: unknown stage %s\n", stage);
        return 2;
    }
    fputs(reasons, stdout);
    if (!result && s.spirv_path) {
        FILE *f = fopen(s.spirv_path, "wb");
        if (!f || fwrite(spirv, sizeof *spirv, spirv_words, f) != spirv_words) {
            fprintf(stderr, "pbgcn_survey: cannot write %s\n", s.spirv_path);
            result = -1;
        }
        if (f) fclose(f);
    }
    free(spirv), free(reasons), free(code);
    return result || bound ? 1 : 0;
}
