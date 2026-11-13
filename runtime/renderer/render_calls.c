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
#include "runtime/render_placed.h"

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

enum { LDS_UNIT = 512, HS_TF_PARAM = 4, HS_MAX_LEVEL, HS_MIN_LEVEL, HS_LS_HS_CONFIG, GS_REGISTERS = 4 };

static float float_bits(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

void rt_local_shader_decode(const uint32_t words[RT_LOCAL_SHADER_WORDS], rt_local_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1], resources2 = words[RESOURCES_2];
    *shader = (rt_local_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .vgpr_inputs = field(resources, 24, 2),
        .user_sgprs = field(resources2, 1, 5),
        .lds_bytes = field(resources2, 7, 9) * LDS_UNIT,
    };
}

void rt_hull_shader_decode(const uint32_t words[RT_HULL_SHADER_WORDS], rt_hull_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1], resources2 = words[RESOURCES_2];
    const uint32_t tf = words[HS_TF_PARAM], config = words[HS_LS_HS_CONFIG];
    *shader = (rt_hull_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .user_sgprs = field(resources2, 1, 5),
        .off_chip = (int)field(resources2, 7, 1),
        .workgroup_size_enabled = (int)field(resources2, 8, 1),
        .domain = field(tf, 0, 2), .partitioning = field(tf, 2, 3), .topology = field(tf, 5, 3),
        .max_level = float_bits(words[HS_MAX_LEVEL]), .min_level = float_bits(words[HS_MIN_LEVEL]),
        .patches = field(config, 0, 8), .input_points = field(config, 8, 6), .output_points = field(config, 14, 6),
    };
}

void rt_geometry_stage_shader_decode(const uint32_t *words, int geometry, rt_geometry_stage_shader *shader)
{
    const uint32_t resources = words[RESOURCES_1], resources2 = words[RESOURCES_2];
    *shader = (rt_geometry_stage_shader){
        .code = program_address(words),
        .vgprs = (field(resources, 0, 6) + 1) * VGPR_UNIT,
        .sgprs = (field(resources, 6, 4) + 1) * SGPR_UNIT,
        .user_sgprs = field(resources2, 1, 5),
        .resources = {resources, resources2},
    };
    if (geometry) memcpy(shader->registers, words + GS_REGISTERS, sizeof shader->registers);
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
    /* With a reader (runtime/render_placed.h): the first two words of memory at each call's offset when
       a submission first took it (calls[0..marked)), for a submission of the buffer again with no new
       calls (taken_count) */
    uint64_t *marks;
    size_t marked, marks_capacity;
    uint64_t used;                   /* the submission clock when it was last recorded into or taken */
} call_list;

static pthread_mutex_t lists_lock = PTHREAD_MUTEX_INITIALIZER;
static call_list *lists;
static size_t list_count, list_capacity;

/* The lists by buffer address: an open-addressed table of list index + 1 (0 free), a power of two at
   least twice the lists, rebuilt when it grows and after an eviction. */
static uint32_t *list_index;
static size_t index_size;

/* The game carves command buffers from rings, so buffer addresses keep changing and the record
   would grow without end. Every LIST_SWEEP_PERIOD submissions, the lists not used for LIST_IDLE
   submissions (several hundred frames: a buffer the game still submits is taken far more often)
   are forgotten. */
enum { LIST_SWEEP_PERIOD = 4096, LIST_IDLE = 65536 };
static uint64_t submissions;

/* Calls placed by guest address (rt_render_record_*_at), waiting for the submission whose buffer
   holds that address. They are kept in a ring in the order they were placed. When the ring comes
   round to a call still waiting, a call whose memory no longer holds its NOP packet belongs to a
   buffer that was never submitted and is dropped; one whose packet is still there (or any, without
   a reader) may belong to a buffer the game has yet to submit, and the ring grows instead, up to
   PLACED_MAX, unless PLACED_KEEP calls have been placed since (an abandoned buffer whose memory was
   never reused). The clinic places two binds a draw, thousands a frame, while the submissions run
   behind: its compute buffers take binds placed about 12,000 placements earlier. A fixed 4096
   dropped binds of buffers still to come, and their draws and dispatches ran with the shaders bound
   before them. Each waiting call is also linked into a bucket by its
   address's 4 KiB page, so that a submission finds the calls inside its buffer, and a call placed
   again at an address finds the older one, without looking at the rest. */
typedef struct {
    uint64_t address;
    uint64_t sequence; /* placements before it: the order calls were placed in */
    uint32_t header;   /* the NOP header at the address when the call was placed (with a reader) */
    int waiting;       /* placed and not yet taken, replaced or overwritten */
    int32_t previous, next; /* its bucket's other waiting calls, -1 at the ends */
    rt_render_call call;
} placed_call;

enum { PLACED_FIRST = 4096, PLACED_MAX = 1 << 18, PLACED_KEEP = 1 << 16, PLACED_BUCKETS = 4096, PLACED_PAGE_SHIFT = 12 };
static placed_call *placed;
static int32_t *placed_found;           /* take_placed's scratch, placed_capacity entries */
static size_t placed_capacity, ring_next; /* ring_next % placed_capacity: the next slot to fill, the oldest */
static int32_t placed_buckets[PLACED_BUCKETS]; /* the first waiting call of each bucket + 1 (0: none) */
static uint64_t placements;
static rt_render_word_reader read_word; /* runtime/render_placed.h */

/* PM4 type-3 packet headers (AMD's public PM4 format): type bits 30-31, the count of the dwords after
   the header minus one in bits 16-29, the opcode in bits 8-15; NOP is opcode 0x10. */
#define PM4_TYPE3_NOP_MASK 0xc000ff00u
#define PM4_TYPE3_NOP 0xc0001000u
#define PM4_COUNT(header) (((header) >> 16) & 0x3fffu)

void rt_render_check_placed_calls(rt_render_word_reader read)
{
    pthread_mutex_lock(&lists_lock);
    read_word = read;
    pthread_mutex_unlock(&lists_lock);
}

/* Whether the memory at a placed call still holds the NOP packet it was placed on. Called with lists_lock held. */
static int still_placed(const placed_call *p)
{
    if (!read_word) return 1;
    if (read_word(p->address) != p->header || (p->header & PM4_TYPE3_NOP_MASK) != PM4_TYPE3_NOP) return 0;
    const uint32_t body = PM4_COUNT(p->header) + 1;
    for (uint32_t i = 1; i <= body; i++)
        if (read_word(p->address + 4ull * i)) return 0;
    return 1;
}

static size_t index_slot(uint64_t buffer)
{
    return (size_t)(((buffer >> 4) * 0x9e3779b97f4a7c15ull) >> 32) & (index_size - 1);
}

/* Enters every list in the index. Called with lists_lock held. */
static void fill_index(void)
{
    memset(list_index, 0, index_size * sizeof *list_index);
    for (size_t i = 0; i < list_count; i++) {
        size_t slot = index_slot(lists[i].buffer);
        while (list_index[slot]) slot = (slot + 1) & (index_size - 1);
        list_index[slot] = (uint32_t)(i + 1);
    }
}

/* Gives the index size slots. Returns -1 when there is no memory (the old index stays). */
static int grow_index(size_t size)
{
    uint32_t *table = malloc(size * sizeof *table);
    if (!table) return -1;
    free(list_index);
    list_index = table, index_size = size;
    fill_index();
    return 0;
}

/* The buffer's list, or NULL. Called with lists_lock held. */
static call_list *find_list(uint64_t buffer)
{
    if (!index_size) return NULL;
    for (size_t slot = index_slot(buffer); list_index[slot]; slot = (slot + 1) & (index_size - 1))
        if (lists[list_index[slot] - 1].buffer == buffer) return &lists[list_index[slot] - 1];
    return NULL;
}

/* The buffer's list, made when it has none; NULL when there is no memory. Called with lists_lock held. */
static call_list *list_for(uint64_t buffer)
{
    call_list *list = find_list(buffer);
    if (list) return list;
    if (list_count == list_capacity) {
        const size_t capacity = list_capacity ? list_capacity * 2 : 256;
        call_list *grown = realloc(lists, capacity * sizeof *lists);
        if (!grown) return NULL;
        lists = grown, list_capacity = capacity;
    }
    if (2 * (list_count + 1) > index_size && grow_index(index_size ? index_size * 2 : 1024)) return NULL;
    lists[list_count] = (call_list){.buffer = buffer, .used = submissions};
    size_t slot = index_slot(buffer);
    while (list_index[slot]) slot = (slot + 1) & (index_size - 1);
    list_index[slot] = (uint32_t)(list_count + 1);
    return &lists[list_count++];
}

/* Forgets the lists idle for LIST_IDLE submissions. Called with lists_lock held. */
static void sweep_lists(void)
{
    if (submissions < LIST_IDLE) return;
    size_t kept = 0;
    for (size_t i = 0; i < list_count; i++) {
        if (lists[i].used + LIST_IDLE < submissions) {
            free(lists[i].calls), free(lists[i].marks);
            continue;
        }
        lists[kept++] = lists[i];
    }
    if (kept == list_count) return;
    list_count = kept;
    fill_index();
}

/* A submission took the list: the clock advances, sweeping now and then. Called with lists_lock held. */
static void count_submission(void)
{
    if (++submissions % LIST_SWEEP_PERIOD == 0) sweep_lists();
}

/* Whether the list has room for needed calls, growing it if not. */
static int reserve(call_list *list, size_t needed)
{
    if (needed <= list->capacity) return 1;
    size_t capacity = list->capacity ? list->capacity : 64;
    while (capacity < needed) capacity *= 2;
    rt_render_call *grown = realloc(list->calls, capacity * sizeof *grown);
    if (!grown) return 0;
    list->calls = grown, list->capacity = capacity;
    return 1;
}

static int has_room(call_list *list) { return reserve(list, list->count + 1); }

/* Appends a call to its buffer's list; a smaller offset than the last call's starts the list over. */

static int record(uint64_t buffer, const rt_render_call *call, char *error, size_t error_size)
{
    pthread_mutex_lock(&lists_lock);
    call_list *list = list_for(buffer);
    int result = -1;
    if (list) {
        if (list->submitted || (list->count && call->offset < list->calls[list->count - 1].offset)) list->count = 0;
        list->submitted = 0;
        list->used = submissions;
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

/* NUM_INSTANCES as the game wrote it: 0 included, which the GPU draws as one instance (the executors). */
int rt_render_record_instances(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t error_size)
{
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

static size_t page_bucket(uint64_t page) { return (size_t)((page * 0x9e3779b97f4a7c15ull) >> 52) & (PLACED_BUCKETS - 1); }
static size_t placed_bucket(uint64_t address) { return page_bucket(address >> PLACED_PAGE_SHIFT); }

/* Takes a waiting call out of its bucket. Called with lists_lock held. */
static void unlink_placed(int32_t i)
{
    placed_call *p = &placed[i];
    if (p->previous >= 0) placed[p->previous].next = p->next;
    else placed_buckets[placed_bucket(p->address)] = p->next + 1;
    if (p->next >= 0) placed[p->next].previous = p->previous;
    p->waiting = 0;
}

static void link_placed(int32_t at)
{
    const size_t bucket = placed_bucket(placed[at].address);
    const int32_t first = placed_buckets[bucket] - 1;
    placed[at].previous = -1, placed[at].next = first;
    if (first >= 0) placed[first].previous = at;
    placed_buckets[bucket] = at + 1;
}

/* Gives the ring capacity slots, the waiting calls whose packets are still in memory first, oldest
   first (the stale ones, and those placed PLACED_KEEP calls ago, are dropped). 0, or -1 when there is no memory (the ring stays as it was).
   Called with lists_lock held. */
static int grow_placed(size_t capacity)
{
    placed_call *grown = calloc(capacity, sizeof *grown);
    int32_t *found = malloc(capacity * sizeof *found);
    if (!grown || !found) {
        free(grown), free(found);
        return -1;
    }
    size_t kept = 0;
    for (size_t i = 0; i < placed_capacity; i++) {
        const placed_call *p = &placed[(ring_next + i) % placed_capacity];
        if (p->waiting && placements - p->sequence < PLACED_KEEP && still_placed(p)) grown[kept++] = *p;
    }
    free(placed), free(placed_found);
    placed = grown, placed_found = found, placed_capacity = capacity, ring_next = kept;
    memset(placed_buckets, 0, sizeof placed_buckets);
    for (size_t i = 0; i < kept; i++) link_placed((int32_t)i);
    return 0;
}

/* The slot for a new placed call: the ring's oldest, dropping the call there when it is stale, or
   past the ring's end once grown; -1 when there is no memory. Called with lists_lock held. */
static int32_t placed_slot(void)
{
    if (!placed_capacity && grow_placed(PLACED_FIRST)) return -1;
    int32_t at = (int32_t)(ring_next % placed_capacity);
    if (placed[at].waiting && placements - placed[at].sequence < PLACED_KEEP && still_placed(&placed[at]) &&
        placed_capacity < PLACED_MAX && !grow_placed(2 * placed_capacity))
        at = (int32_t)(ring_next % placed_capacity);
    if (placed[at].waiting) unlink_placed(at); /* stale, or the ring is at its limit */
    ring_next++;
    return at;
}

static int place(uint64_t address, const rt_render_call *call)
{
    pthread_mutex_lock(&lists_lock);
    const size_t bucket = placed_bucket(address);
    uint32_t header = 0;
    if (read_word) {
        header = read_word(address);
        /* an older call at this address: its memory has been written again since */
        for (int32_t i = placed_buckets[bucket] - 1, next; i >= 0; i = next) {
            next = placed[i].next;
            if (placed[i].address == address) unlink_placed(i);
        }
    }
    const int32_t at = placed_slot();
    if (at >= 0) {
        placed[at] = (placed_call){.address = address, .sequence = placements++, .header = header, .waiting = 1, .call = *call};
        link_placed(at);
    }
    pthread_mutex_unlock(&lists_lock);
    return at >= 0 ? 0 : -1;
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

int rt_render_record_local_shader_at(uint64_t address, const uint32_t words[RT_LOCAL_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_LOCAL_SHADER};
    rt_local_shader_decode(words, &call.u.local_shader);
    return place(address, &call);
}

int rt_render_record_hull_shader_at(uint64_t address, const uint32_t words[RT_HULL_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_HULL_SHADER};
    rt_hull_shader_decode(words, &call.u.hull_shader);
    return place(address, &call);
}

int rt_render_record_export_shader_at(uint64_t address, const uint32_t words[RT_EXPORT_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_EXPORT_SHADER};
    rt_geometry_stage_shader_decode(words, 0, &call.u.geometry_stage_shader);
    return place(address, &call);
}

int rt_render_record_geometry_shader_at(uint64_t address, const uint32_t words[RT_GEOMETRY_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_GEOMETRY_SHADER};
    rt_geometry_stage_shader_decode(words, 1, &call.u.geometry_stage_shader);
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

/* SET_BASE writes the address's bits 3-31 and 32-39 (the library's writer, 0x014742f0, masks them so):
   the base the GPU uses is those bits alone. */
int rt_render_record_indirect_base(uint64_t buffer, uint32_t offset, uint64_t address, char *error, size_t error_size)
{
    static const uint64_t base_low_mask = 0xfffffff8u, base_high_mask = 0xffu;
    rt_render_call call = {.kind = RT_CALL_INDIRECT_BASE, .offset = offset};
    call.u.indirect_base.address = (address & base_low_mask) | ((address >> 32) & base_high_mask) << 32;
    return record(buffer, &call, error, error_size);
}

int rt_render_record_dispatch_indirect(uint64_t buffer, uint32_t offset, uint32_t data_offset, char *error, size_t error_size)
{
    if (data_offset % sizeof(uint32_t)) {
        snprintf(error, error_size, "an indirect dispatch's arguments at %#x bytes from the base: not a whole word", data_offset);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_DISPATCH_INDIRECT, .offset = offset};
    call.u.dispatch_indirect.data_offset = data_offset;
    return record(buffer, &call, error, error_size);
}

int rt_render_record_draw_indirect(uint64_t buffer, uint32_t offset, uint32_t data_offset, rt_shader_stage stage, uint32_t base_vertex_sgpr,
                                   uint32_t start_instance_sgpr, char *error, size_t error_size)
{
    if (data_offset % sizeof(uint32_t)) {
        snprintf(error, error_size, "an indirect draw's arguments at %#x bytes from the base: not a whole word", data_offset);
        return -1;
    }
    rt_render_call call = {.kind = RT_CALL_DRAW_INDIRECT, .offset = offset};
    call.u.draw_indirect.data_offset = data_offset, call.u.draw_indirect.stage = stage;
    call.u.draw_indirect.base_vertex_sgpr = base_vertex_sgpr, call.u.draw_indirect.start_instance_sgpr = start_instance_sgpr;
    return record(buffer, &call, error, error_size);
}

int rt_render_record_pixel_shader_at(uint64_t address, const uint32_t words[RT_PIXEL_SHADER_WORDS])
{
    rt_render_call call = {.kind = RT_CALL_PIXEL_SHADER};
    rt_pixel_shader_decode(words, &call.u.pixel_shader);
    return place(address, &call);
}

/* Orders waiting calls by address, and those at one address in the order they were placed. */
static int placed_before(int32_t a, int32_t b)
{
    return placed[a].address != placed[b].address ? placed[a].address < placed[b].address
                                                  : placed[a].sequence < placed[b].sequence;
}

static int compare_placed(const void *a, const void *b)
{
    const int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return placed_before(x, y) ? -1 : placed_before(y, x);
}

/* A buffer's calls arrive nearly in order (page by page, each page in the order the command buffer
   was written): an insertion sort, which gives up to qsort when they turn out not to be. */
static void sort_placed(int32_t *found, size_t count)
{
    size_t moves = 0;
    const size_t move_limit = 8 * count + 64;
    for (size_t i = 1; i < count; i++) {
        const int32_t key = found[i];
        size_t at = i;
        while (at && placed_before(key, found[at - 1])) found[at] = found[at - 1], at--, moves++;
        found[at] = key;
        if (moves > move_limit) {
            qsort(found, count, sizeof *found, compare_placed);
            return;
        }
    }
}

/* Gathers the waiting calls inside [buffer, buffer + size) into found (placed_capacity entries),
   ordered by address. Called with lists_lock held. */
static size_t find_placed(uint64_t buffer, uint32_t size, int32_t *found)
{
    size_t count = 0;
    const uint64_t first_page = buffer >> PLACED_PAGE_SHIFT, last_page = (buffer + size - 1) >> PLACED_PAGE_SHIFT;
    if (last_page - first_page >= PLACED_BUCKETS) { /* a buffer over every bucket: the whole ring */
        for (int32_t i = 0; i < (int32_t)placed_capacity; i++)
            if (placed[i].waiting && placed[i].address - buffer < size) found[count++] = i;
    } else {
        for (uint64_t page = first_page; page <= last_page; page++) {
            const size_t page_start = count;
            for (int32_t i = placed_buckets[page_bucket(page)] - 1; i >= 0; i = placed[i].next)
                /* a bucket also holds other pages' calls: only those of this page, so each is found once */
                if (placed[i].address >> PLACED_PAGE_SHIFT == page && placed[i].address - buffer < size)
                    found[count++] = i;
            for (size_t a = page_start, b = count; a + 1 < b; a++, b--) { /* newest first: reversed */
                const int32_t swap = found[a];
                found[a] = found[b - 1], found[b - 1] = swap;
            }
        }
    }
    sort_placed(found, count);
    return count;
}

/* Moves the placed calls inside [buffer, buffer + size) into the buffer's list, each at its offset
   after the calls already there at that offset, in the order they were placed. Called with
   lists_lock held. */
static void take_placed(uint64_t buffer, uint32_t size)
{
    int32_t *const found = placed_found;
    if (!size || !placed_capacity) return;
    size_t count = find_placed(buffer, size, found), taken = 0;
    for (size_t i = 0; i < count; i++) {
        if (still_placed(&placed[found[i]])) found[taken++] = found[i];
        else unlink_placed(found[i]); /* placed in an earlier use of this memory: dropped */
    }
    if (!taken) return;
    call_list *list = list_for(buffer);
    if (!list) return; /* no memory: they wait for the next submission */
    if (list->submitted) list->count = 0; /* a new use of the buffer */
    list->submitted = 0;
    if (!reserve(list, list->count + taken)) return; /* no memory: they wait */
    /* merged from the end: a placed call goes after the calls at its offset */
    size_t recorded = list->count, out = list->count + taken;
    while (taken) {
        const placed_call *p = &placed[found[taken - 1]];
        const uint32_t offset = (uint32_t)(p->address - buffer);
        if (recorded && list->calls[recorded - 1].offset > offset) {
            list->calls[--out] = list->calls[--recorded];
            continue;
        }
        list->calls[--out] = p->call;
        list->calls[out].offset = offset;
        unlink_placed(found[--taken]);
        list->count++;
    }
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

/* The two words at a call's offset in its buffer (the second only inside the size). Called with lists_lock held. */
static uint64_t mark_at(uint64_t buffer, uint32_t offset, uint32_t size)
{
    const uint64_t second = offset + 8ull <= size ? read_word(buffer + offset + 4) : 0;
    return read_word(buffer + offset) | second << 32;
}

/* How many of a list's calls a submission of `size` bytes takes: those before the size. A list taken
   before and given no call since is either the same commands submitted again, or a new use of the
   memory in a layout nothing recorded (the game writes some buffers from packets alone, DMA and
   memory writes): with a reader, it is taken again only where the memory still holds what it held at
   each call's offset when first taken, and otherwise its calls belong to the earlier use and are
   forgotten (their user data would land on a later dispatch). Called with lists_lock held. */
static size_t taken_count(call_list *list, uint32_t size)
{
    size_t n = 0;
    while (n < list->count && list->calls[n].offset < size) n++;
    if (!read_word || !n) return n;
    if (!list->submitted) list->marked = 0; /* calls recorded since: a new use, marked afresh */
    for (size_t i = 0; i < list->marked && i < n; i++)
        if (list->marks[i] != mark_at(list->buffer, list->calls[i].offset, size)) {
            list->count = list->marked = 0;
            return 0;
        }
    if (n > list->marked) {
        if (n > list->marks_capacity) {
            uint64_t *grown = realloc(list->marks, list->capacity * sizeof *grown);
            if (!grown) return n; /* no memory to mark them: taken unchecked */
            list->marks = grown, list->marks_capacity = list->capacity;
        }
        for (size_t i = list->marked; i < n; i++) list->marks[i] = mark_at(list->buffer, list->calls[i].offset, size);
        list->marked = n;
    }
    return n;
}

const rt_render_call *rt_render_submitted(uint64_t buffer, uint32_t size, size_t *count)
{
    pthread_mutex_lock(&lists_lock);
    take_placed(buffer, size);
    const rt_render_call *calls = NULL;
    *count = 0;
    call_list *list = find_list(buffer);
    if (list) {
        calls = list->calls;
        *count = taken_count(list, size);
        list->submitted = 1;
        list->used = submissions;
    }
    count_submission();
    pthread_mutex_unlock(&lists_lock);
    return calls;
}

rt_render_call *rt_render_take(uint64_t buffer, uint32_t size, size_t *count)
{
    pthread_mutex_lock(&lists_lock);
    take_placed(buffer, size);
    rt_render_call *copy = NULL;
    *count = 0;
    call_list *list = find_list(buffer);
    if (list) {
        const size_t n = taken_count(list, size);
        list->submitted = 1;
        list->used = submissions;
        if (n && (copy = malloc(n * sizeof *copy))) {
            memcpy(copy, list->calls, n * sizeof *copy);
            *count = n;
        } else if (n) {
            *count = SIZE_MAX; /* no memory */
        }
    }
    count_submission();
    pthread_mutex_unlock(&lists_lock);
    return copy;
}

void rt_render_forget(void)
{
    pthread_mutex_lock(&lists_lock);
    for (size_t i = 0; i < list_count; i++) free(lists[i].calls), free(lists[i].marks);
    free(lists), free(list_index);
    lists = NULL, list_count = list_capacity = 0;
    list_index = NULL, index_size = 0;
    submissions = 0;
    free(placed), free(placed_found);
    placed = NULL, placed_found = NULL, placed_capacity = ring_next = 0;
    memset(placed_buckets, 0, sizeof placed_buckets);
    placements = 0;
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
