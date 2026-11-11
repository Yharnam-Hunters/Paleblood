/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer's record of the GPU library's calls (runtime/render_calls.h).

   Register fields, from AMD's GFX7 field header (gca/gfx_7_2_sh_mask.h):
     CB_COLORn_PITCH   TILE_MAX[10:0] = pitch / 8 - 1
     CB_COLORn_SLICE   TILE_MAX[21:0] = pitch * height / 64 - 1
     CB_COLORn_VIEW    SLICE_START[10:0], SLICE_MAX[23:13]
     CB_COLORn_INFO    FORMAT[6:2], NUMBER_TYPE[10:8], COMP_SWAP[12:11], FAST_CLEAR[13],
                       COMPRESSION[14]
     CB_COLORn_ATTRIB  TILE_MODE_INDEX[4:0], NUM_SAMPLES[14:12] (log2)
     SPI_SHADER_PGM_LO_xS MEM_BASE[31:0], SPI_SHADER_PGM_HI_xS MEM_BASE[7:0]: address bits 39:8
                       and 47:40
     SPI_SHADER_PGM_RSRC1_xS VGPRS[5:0] = VGPRs / 4 - 1, SGPRS[9:6] = SGPRs / 8 - 1;
                       for VS also VGPR_COMP_CNT[25:24]
     SPI_SHADER_PGM_RSRC2_xS USER_SGPR[5:1]
     SPI_VS_OUT_CONFIG VS_EXPORT_COUNT[5:1] = parameter vectors - 1
     SPI_PS_IN_CONTROL NUM_INTERP[5:0]
   and ColorFormat from its enum header (gca/gfx_7_2_enum.h). */
#include "runtime/render_calls.h"

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { WORD_BASE, WORD_PITCH, WORD_SLICE, WORD_VIEW, WORD_INFO, WORD_ATTRIB, WORD_RESERVED, WORD_CMASK, WORD_CMASK_SLICE };
/* CMASK holds 4 bits per 8x8 tile; CMASK_SLICE.TILE_MAX counts 128x128-pixel blocks of it, 128 bytes each
   (Mesa's radeonsi sizes it so: slice_tile_max = pixels / (128 * 128) - 1) */
enum { CMASK_BLOCK_BYTES = 128 };
enum { BASE_UNIT_SHIFT = 8, PITCH_UNIT = 8, SLICE_UNIT = 64 };
enum {
    COLOR_INVALID = 0x0, COLOR_RESERVED_13 = 0xd, COLOR_RESERVED_15 = 0xf, COLOR_LAST_DEFINED = 0x16,
};

static uint32_t field(uint32_t word, unsigned low, unsigned bits) { return (word >> low) & ((1u << bits) - 1); }

int rt_color_target_decode(const uint32_t words[RT_COLOR_TARGET_WORDS], rt_color_target *target, char *error,
                           size_t error_size)
{
    memset(target, 0, sizeof *target);
    const uint32_t info = words[WORD_INFO];
    target->format = field(info, 2, 5);
    if (target->format == COLOR_INVALID) return 0;
    if (target->format == COLOR_RESERVED_13 || target->format == COLOR_RESERVED_15 || target->format > COLOR_LAST_DEFINED) {
        snprintf(error, error_size, "render target format %#x is reserved", target->format);
        return -1;
    }
    target->number_type = field(info, 8, 3);
    target->component_swap = field(info, 11, 2);
    target->fast_clear = (int)field(info, 13, 1);
    target->compression = (int)field(info, 14, 1);
    target->address = (uint64_t)words[WORD_BASE] << BASE_UNIT_SHIFT;
    target->pitch = (field(words[WORD_PITCH], 0, 11) + 1) * PITCH_UNIT;
    const uint64_t pixels = ((uint64_t)field(words[WORD_SLICE], 0, 22) + 1) * SLICE_UNIT;
    if (pixels % target->pitch) {
        snprintf(error, error_size, "render target slice of %llu pixels is not a whole number of %u-pixel rows",
                 (unsigned long long)pixels, target->pitch);
        return -1;
    }
    target->height = (uint32_t)(pixels / target->pitch);
    target->first_slice = field(words[WORD_VIEW], 0, 11);
    target->last_slice = field(words[WORD_VIEW], 13, 11);
    target->tile_mode_index = field(words[WORD_ATTRIB], 0, 5);
    target->samples = 1u << field(words[WORD_ATTRIB], 12, 3);
    target->cmask_address = (uint64_t)words[WORD_CMASK] << BASE_UNIT_SHIFT;
    target->cmask_bytes = (field(words[WORD_CMASK_SLICE], 0, 14) + 1) * CMASK_BLOCK_BYTES;
    return 1;
}

/* ---- shader register blocks ---- */

enum { PROGRAM_LOW, PROGRAM_HIGH, RESOURCES_1, RESOURCES_2 };
enum { VS_OUT_CONFIG = 4, VS_POS_FORMAT, VS_OUT_CNTL };
enum { PS_Z_FORMAT = 4, PS_COL_FORMAT, PS_INPUT_ENA, PS_INPUT_ADDR, PS_IN_CONTROL, PS_BARYC_CNTL, PS_DB_SHADER_CONTROL,
       PS_CB_SHADER_MASK };
enum { PROGRAM_UNIT_SHIFT = 8, VGPR_UNIT = 4, SGPR_UNIT = 8 };

static uint64_t program_address(const uint32_t *words)
{
    return ((uint64_t)field(words[PROGRAM_HIGH], 0, 8) << 32 | words[PROGRAM_LOW]) << PROGRAM_UNIT_SHIFT;
}

enum { CS_NUM_THREAD_X = 4 };

void rt_compute_shader_decode(const uint32_t words[RT_COMPUTE_SHADER_WORDS], rt_compute_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1], resources2 = words[RESOURCES_2];
    *shader = (rt_compute_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .user_sgprs = field(resources2, 1, 5),
        .workgroup_id_enables = field(resources2, 7, 3),
        .workgroup_size_enabled = (int)field(resources2, 10, 1),
        .thread_id_components = field(resources2, 11, 2),
        .threads = {words[CS_NUM_THREAD_X], words[CS_NUM_THREAD_X + 1], words[CS_NUM_THREAD_X + 2]},
    };
}

void rt_image_descriptor_decode(const uint32_t words[RT_IMAGE_DESCRIPTOR_WORDS], rt_image_descriptor *image)
{
    enum { ADDRESS_UNIT_SHIFT = 8 };
    *image = (rt_image_descriptor){
        .address = ((uint64_t)field(words[1], 0, 6) << 32 | words[0]) << ADDRESS_UNIT_SHIFT,
        .data_format = field(words[1], 20, 6),
        .num_format = field(words[1], 26, 4),
        .width = field(words[2], 0, 14) + 1,
        .height = field(words[2], 14, 14) + 1,
        .swizzle = field(words[3], 0, 12),
        .base_level = field(words[3], 12, 4),
        .last_level = field(words[3], 16, 4),
        .tiling_index = field(words[3], 20, 5),
        .pow2_pad = field(words[3], 25, 1),
        .type = field(words[3], 28, 4),
        .depth = field(words[4], 0, 13) + 1,
        .pitch = field(words[4], 13, 14) + 1,
        .base_array = field(words[5], 0, 13),
        .last_array = field(words[5], 13, 13),
    };
    image->layers = 1;
    /* an array's DEPTH counts its layers, a cube's its whole cubes of six faces (as shadPS4's NumLayers) */
    if (image->type == RT_IMAGE_1D_ARRAY || image->type == RT_IMAGE_2D_ARRAY || image->type == RT_IMAGE_CUBE) {
        image->layers = image->type == RT_IMAGE_CUBE ? 6 * image->depth : image->depth;
        if (image->pow2_pad)
            while (image->layers & (image->layers - 1)) image->layers += image->layers & -image->layers;
    }
}

void rt_vertex_shader_decode(const uint32_t words[RT_VERTEX_SHADER_WORDS], rt_vertex_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1];
    *shader = (rt_vertex_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .vgpr_inputs = field(resources, 24, 2),
        .user_sgprs = field(words[RESOURCES_2], 1, 5),
        .parameter_count = field(words[VS_OUT_CONFIG], 1, 5) + 1,
        .position_format = words[VS_POS_FORMAT],
        .output_control = words[VS_OUT_CNTL],
    };
}

void rt_pixel_shader_decode(const uint32_t words[RT_PIXEL_SHADER_WORDS], rt_pixel_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1];
    *shader = (rt_pixel_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .user_sgprs = field(words[RESOURCES_2], 1, 5),
        .depth_format = words[PS_Z_FORMAT],
        .color_formats = words[PS_COL_FORMAT],
        .input_enable = words[PS_INPUT_ENA],
        .input_address = words[PS_INPUT_ADDR],
        .interpolated = field(words[PS_IN_CONTROL], 0, 6),
        .barycentric_control = words[PS_BARYC_CNTL],
        .depth_control = words[PS_DB_SHADER_CONTROL],
        .color_mask = words[PS_CB_SHADER_MASK],
    };
}

/* ---- the record ---- */

typedef struct {
    uint64_t buffer;
    rt_render_call *calls;
    size_t count, capacity;
    int submitted;                   /* submitted since its last call: the next call starts it over */
} call_list;

static pthread_mutex_t lists_lock = PTHREAD_MUTEX_INITIALIZER;
static call_list *lists;
static size_t list_count;

/* Calls placed by guest address (rt_render_record_*_at), waiting for the submission whose buffer
   holds that address. A buffer that is never submitted leaves its calls here; past the limit the
   oldest are dropped. */
typedef struct {
    uint64_t address;
    rt_render_call call;
} placed_call;

enum { MAX_PLACED_CALLS = 4096 };
static placed_call placed[MAX_PLACED_CALLS];
static size_t placed_count;

static call_list *list_for(uint64_t buffer)
{
    for (size_t i = 0; i < list_count; i++)
        if (lists[i].buffer == buffer) return &lists[i];
    call_list *grown = realloc(lists, (list_count + 1) * sizeof *lists);
    if (!grown) return NULL;
    lists = grown;
    lists[list_count] = (call_list){.buffer = buffer};
    return &lists[list_count++];
}

/* Appends a call to its buffer's list; a smaller offset than the last call's starts the list over. */
static int has_room(call_list *list)
{
    if (list->count == list->capacity) {
        const size_t capacity = list->capacity ? list->capacity * 2 : 64;
        rt_render_call *grown = realloc(list->calls, capacity * sizeof *grown);
        if (grown) list->calls = grown, list->capacity = capacity;
    }
    return list->count < list->capacity;
}

static int record(uint64_t buffer, const rt_render_call *call, char *error, size_t error_size)
{
    pthread_mutex_lock(&lists_lock);
    call_list *list = list_for(buffer);
    int result = -1;
    if (list) {
        if (list->submitted || (list->count && call->offset < list->calls[list->count - 1].offset)) list->count = 0;
        list->submitted = 0;
        if (has_room(list)) {
            list->calls[list->count++] = *call;
            result = 0;
        }
    }
    pthread_mutex_unlock(&lists_lock);
    if (result) snprintf(error, error_size, "no memory to record a render call");
    return result;
}

int rt_render_record_color_target(uint64_t buffer, uint32_t offset, unsigned slot,
                                  const uint32_t words[RT_COLOR_TARGET_WORDS], char *error, size_t error_size)
{
    if (slot >= RT_COLOR_TARGETS) {
        snprintf(error, error_size, "render target slot %u (there are %d)", slot, RT_COLOR_TARGETS);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_COLOR_TARGET, .offset = offset, .u.color_target.slot = slot};
    if (words) {
        const int decoded = rt_color_target_decode(words, &call.u.color_target.target, error, error_size);
        if (decoded < 0) return -1;
        call.u.color_target.enabled = decoded;
    }
    return record(buffer, &call, error, error_size);
}

int rt_render_record_viewport(uint64_t buffer, uint32_t offset, unsigned index, float zmin, float zmax,
                              const float scale[3], const float translate[3], char *error, size_t error_size)
{
    if (index >= RT_VIEWPORTS) {
        snprintf(error, error_size, "viewport %u (there are %d)", index, RT_VIEWPORTS);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_VIEWPORT, .offset = offset};
    call.u.viewport.index = index, call.u.viewport.zmin = zmin, call.u.viewport.zmax = zmax;
    memcpy(call.u.viewport.scale, scale, sizeof call.u.viewport.scale);
    memcpy(call.u.viewport.translate, translate, sizeof call.u.viewport.translate);
    return record(buffer, &call, error, error_size);
}

int rt_render_record_draw_auto(uint64_t buffer, uint32_t offset, uint32_t index_count, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_DRAW_AUTO, .offset = offset, .u.draw_auto.index_count = index_count};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value, char *error,
                              size_t error_size)
{
    const int context = address >= RT_CONTEXT_REGISTERS_FIRST && address <= RT_CONTEXT_REGISTERS_LAST;
    const int uconfig = address >= RT_UCONFIG_REGISTERS_FIRST && address <= RT_UCONFIG_REGISTERS_LAST;
    if (!context && !uconfig) {
        snprintf(error, error_size, "register 0x%x is outside the context and user config ranges", (unsigned)address);
        return -1;
    }
    const rt_render_call call = {.kind = RT_CALL_REGISTER, .offset = offset, .u.reg = {address, value}};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_instances(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t error_size)
{
    if (!count) {
        snprintf(error, error_size, "an instance count of 0");
        return -1;
    }
    const rt_render_call call = {.kind = RT_CALL_INSTANCES, .offset = offset, .u.instances.count = count};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_index_base(uint64_t buffer, uint32_t offset, uint64_t address, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_INDEX_BASE, .offset = offset, .u.index_base.address = address};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_index_size(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_INDEX_SIZE, .offset = offset, .u.index_size.count = count};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_index_type(uint64_t buffer, uint32_t offset, uint32_t type, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_INDEX_TYPE, .offset = offset, .u.index_type.type = type};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_draw_index_offset(uint64_t buffer, uint32_t offset, uint32_t first, uint32_t count, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_DRAW_INDEX_OFFSET, .offset = offset, .u.draw_index = {.first = first, .count = count}};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_vertex_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_VERTEX_SHADER_WORDS],
                                   char *error, size_t error_size)
{
    rt_render_call call = {.kind = RT_CALL_VERTEX_SHADER, .offset = offset};
    rt_vertex_shader_decode(words, &call.u.vertex_shader);
    return record(buffer, &call, error, error_size);
}

int rt_render_record_pixel_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_PIXEL_SHADER_WORDS],
                                  char *error, size_t error_size)
{
    rt_render_call call = {.kind = RT_CALL_PIXEL_SHADER, .offset = offset};
    rt_pixel_shader_decode(words, &call.u.pixel_shader);
    return record(buffer, &call, error, error_size);
}

static int place(uint64_t address, const rt_render_call *call)
{
    pthread_mutex_lock(&lists_lock);
    if (placed_count == MAX_PLACED_CALLS) { /* drop the oldest: a buffer that was never submitted */
        memmove(placed, placed + 1, (MAX_PLACED_CALLS - 1) * sizeof *placed);
        placed_count--;
    }
    placed[placed_count++] = (placed_call){.address = address, .call = *call};
    pthread_mutex_unlock(&lists_lock);
    return 0;
}

int rt_render_record_vertex_shader_at(uint64_t address, const uint32_t words[RT_VERTEX_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_VERTEX_SHADER};
    rt_vertex_shader_decode(words, &call.u.vertex_shader);
    return place(address, &call);
}

int rt_render_record_compute_shader_at(uint64_t address, const uint32_t words[RT_COMPUTE_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_COMPUTE_SHADER};
    rt_compute_shader_decode(words, &call.u.compute_shader);
    return place(address, &call);
}

int rt_render_record_dispatch(uint64_t buffer, uint32_t offset, const uint32_t groups[3], char *error, size_t error_size)
{
    if (!groups[0] || !groups[1] || !groups[2]) {
        snprintf(error, error_size, "a dispatch of %u x %u x %u workgroups: none to run", groups[0], groups[1], groups[2]);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_DISPATCH, .offset = offset};
    memcpy(call.u.dispatch.groups, groups, sizeof call.u.dispatch.groups);
    return record(buffer, &call, error, error_size);
}

int rt_render_record_pixel_shader_at(uint64_t address, const uint32_t words[RT_PIXEL_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_PIXEL_SHADER};
    rt_pixel_shader_decode(words, &call.u.pixel_shader);
    return place(address, &call);
}

/* Moves the placed calls inside [buffer, buffer + size) into the buffer's list, each at its offset
   in order among the calls already there. Called with lists_lock held. */
static void take_placed(uint64_t buffer, uint32_t size)
{
    call_list *list = NULL;
    size_t kept = 0;
    for (size_t i = 0; i < placed_count; i++) {
        const placed_call *p = &placed[i];
        if (p->address < buffer || p->address - buffer >= size) {
            placed[kept++] = *p;
            continue;
        }
        if (!list) {
            list = list_for(buffer);
            if (!list) { placed[kept++] = *p; continue; }
            if (list->submitted) list->count = 0; /* a new use of the buffer */
            list->submitted = 0;
        }
        if (!has_room(list)) { placed[kept++] = *p; continue; }
        rt_render_call call = p->call;
        call.offset = (uint32_t)(p->address - buffer);
        size_t at = list->count;
        while (at && list->calls[at - 1].offset > call.offset) at--;
        memmove(list->calls + at + 1, list->calls + at, (list->count - at) * sizeof *list->calls);
        list->calls[at] = call;
        list->count++;
    }
    placed_count = kept;
}

int rt_render_record_user_data(uint64_t buffer, uint32_t offset, rt_shader_stage stage, unsigned slot, const uint32_t *words,
                               unsigned count, char *error, size_t error_size)
{
    if ((unsigned)stage >= RT_SHADER_STAGES || count == 0 || count > RT_USER_DATA_MAX_WORDS ||
        slot >= RT_USER_DATA_SLOTS || count > RT_USER_DATA_SLOTS - slot) {
        snprintf(error, error_size, "user data: stage %u, slots %u-%u (stages 0-%d, slots 0-%d, 1-%d words)", (unsigned)stage, slot,
                 slot + count - 1, RT_SHADER_STAGES - 1, RT_USER_DATA_SLOTS - 1, RT_USER_DATA_MAX_WORDS);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_USER_DATA, .offset = offset};
    call.u.user_data.stage = stage, call.u.user_data.slot = slot, call.u.user_data.count = count;
    memcpy(call.u.user_data.words, words, count * sizeof *words);
    return record(buffer, &call, error, error_size);
}

const rt_render_call *rt_render_submitted(uint64_t buffer, uint32_t size, size_t *count)
{
    pthread_mutex_lock(&lists_lock);
    take_placed(buffer, size);
    const rt_render_call *calls = NULL;
    *count = 0;
    for (size_t i = 0; i < list_count; i++) {
        if (lists[i].buffer != buffer) continue;
        calls = lists[i].calls;
        lists[i].submitted = 1;
        while (*count < lists[i].count && lists[i].calls[*count].offset < size) (*count)++;
    }
    pthread_mutex_unlock(&lists_lock);
    return calls;
}

rt_render_call *rt_render_take(uint64_t buffer, uint32_t size, size_t *count)
{
    pthread_mutex_lock(&lists_lock);
    take_placed(buffer, size);
    rt_render_call *copy = NULL;
    *count = 0;
    for (size_t i = 0; i < list_count; i++) {
        if (lists[i].buffer != buffer) continue;
        lists[i].submitted = 1;
        size_t n = 0;
        while (n < lists[i].count && lists[i].calls[n].offset < size) n++;
        if (n && (copy = malloc(n * sizeof *copy))) {
            memcpy(copy, lists[i].calls, n * sizeof *copy);
            *count = n;
        } else if (n) {
            *count = SIZE_MAX; /* no memory */
        }
    }
    pthread_mutex_unlock(&lists_lock);
    return copy;
}

void rt_render_forget(void)
{
    pthread_mutex_lock(&lists_lock);
    for (size_t i = 0; i < list_count; i++) free(lists[i].calls);
    free(lists);
    lists = NULL, list_count = 0;
    placed_count = 0;
    pthread_mutex_unlock(&lists_lock);
}

int rt_render_record_draw_index_offset_packet(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size,
                                              uint32_t first, uint32_t count, uint32_t initiator,
                                              char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_DRAW_INDEX_OFFSET, .offset = offset,
                                .u.draw_index = {.first = first, .count = count, .has_packet_fields = 1,
                                                 .header = header, .max_size = max_size, .initiator = initiator}};
    return record(buffer, &call, error, error_size);
}

int rt_render_record_draw_indexed(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size, uint64_t address,
                                  uint32_t count, uint32_t initiator, char *error, size_t error_size)
{
    const rt_render_call call = {.kind = RT_CALL_DRAW_INDEXED, .offset = offset,
                                .u.draw_indexed = {header, max_size, address, count, initiator}};
    return record(buffer, &call, error, error_size);
}
