/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GCN_SPIRV_H
#define RUNTIME_GCN_SPIRV_H

/* The shader translator (runtime/renderer/gcn_spirv.c): GCN (GFX7) code to SPIR-V, for the
   renderer (docs/RENDERER.md): compute, vertex and pixel shaders, with branches and loops. It
   covers vector and scalar arithmetic, compares and selects, buffer and constant loads and
   stores, and the vertex and pixel stages' inputs and outputs. Anything else stops translation
   with the instruction's name in the error.

   How GCN maps onto SPIR-V: one invocation per lane. Registers are function variables holding
   32-bit words (floats are their bits). A compare or carry result, which GCN writes as one bit per
   lane into VCC or an SGPR pair, is kept as this lane's bit. Memory is reached through the 48-bit
   base address in a buffer descriptor or SGPR pair, as SPIR-V physical storage buffer pointers
   (Vulkan's buffer device address; Metal device pointers through SPIRV-Cross). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GCN_USER_SGPR_MAX = 16, GCN_PUSH_CONSTANT_LIMIT = 128 };

/* Survey mode, for listing what the translator lacks (pbgcn_survey, docs/RENDERER.md "Coverage survey";
   never on in a draw): a translation meeting an instruction it cannot translate notes the reason and goes
   on with the next instruction instead of stopping, and fails at the end with every distinct reason in
   the error, one a line after the dword it was first met at ("@dword reason\n"). Process-wide. */
void gcn_spirv_set_survey(int on);

/* Every layout below also says where the shader finds its data:
     push_constant_offset  the byte offset of its user data (GCN_USER_SGPR_MAX words) in the push
                           constants, so that the stages of one draw keep theirs apart (a vertex
                           shader's at 0, a pixel shader's at 64); a multiple of 4, with the user
                           data within GCN_PUSH_CONSTANT_LIMIT bytes, the size every Vulkan device
                           guarantees;
     address_offset        what is added to every guest address the shader forms (a buffer
                           descriptor's base, an SGPR pair) to reach the same bytes on the GPU: the
                           guest memory is one GPU buffer, at a fixed distance from the guest's view
                           of it. 0 when the two coincide, as in the tests' own buffers. */

/* The run-time stop channel. Some of what a shader does is only known when it runs: a buffer
   access's data format comes from its descriptor. A shader meeting such a case it cannot do exactly
   writes a code to the 32-bit word at error_address (a GPU address, in the same space as the
   translated shader's buffer pointers) and skips the access; the caller checks the word after the
   work and stops by name. The code's kind is in bits 24-31, its details below. */
enum {
    GCN_RUNTIME_STOP_KIND_SHIFT = 24,
    /* buffer_load/store_format_*: details DATA_FORMAT << 8 | NUM_FORMAT << 4 | the access's components;
       bit 3 set when the descriptor's DST_SEL is not x, y, z, w, bit 7 for a store */
    GCN_RUNTIME_STOP_BUFFER_FORMAT = 1,
    /* a vertex or pixel shader read VCC as a lane mask after a scalar write of its halves left each
       lane's bit unknown (a lane has no known place in the wave there) */
    GCN_RUNTIME_STOP_VCC_UNKNOWN = 2,
    /* image_load/image_load_mip of a texel outside the image, or of a level past its last */
    GCN_RUNTIME_STOP_IMAGE_RANGE = 3,
    /* a tessellated draw of more patches than the LDS buffer or the factor ring holds (gcn_tessellation_layout);
       details: the draw's patch count, its low 24 bits */
    GCN_RUNTIME_STOP_TESSELLATION_LIMIT = 4,
    /* a draw with a geometry shader of more vertices or primitives than its rings or strip buffer hold
       (gcn_geometry_layout); details: the draw's vertex count, its low 24 bits */
    GCN_RUNTIME_STOP_GEOMETRY_LIMIT = 5,
    /* a read through an image whose descriptor has base address 0 but is not null (gcn_image_binding.unmapped):
       what the PS4 returns from unmapped memory is not established */
    GCN_RUNTIME_STOP_UNMAPPED_IMAGE = 7,
};

/* Images (MIMG). A shader names an image by the SGPRs holding its 8-word descriptor (T#), which the
   GPU reads as it runs. The translator turns each into an image the caller binds: descriptor set 0,
   binding i for images[i], whose shape the caller reads from the descriptor before translating
   (gcn_image_resources lists the descriptors a program uses and where each comes from).

   A descriptor comes from the user data (the SGPRs hold it from the start), or from memory: the
   program loads it with an s_load_dwordx8 (or a wider s_load covering it) at an immediate offset from a pointer in two user-data
   SGPRs, which the caller reads at draw time. One SGPR range can hold different descriptors at
   different MIMG instructions (the game's pixel shaders read a user-data image, then load another
   descriptor over it); each MIMG's own is found from the code before it, when that code runs straight
   through (no branch before the instruction, and none back to it). A descriptor written any other way
   stops by name, as does a MIMG instruction whose descriptor is not one of the bindings.

   A sampled binding is read through a sampled image (Vulkan's SAMPLED_IMAGE descriptor): its texel
   format and DST_SEL are the bound view's, and image_load and image_load_mip fetch from it (the level
   counted from the view's first). Storage images are for compute shaders, which write them. */
typedef struct {
    int from_memory;                  /* 0: in the user data from sgpr; 1: loaded from memory */
    unsigned sgpr;                    /* the descriptor's first SGPR where the MIMG instruction reads it */
    unsigned pointer_sgpr;            /* from memory: the user-data SGPR pair holding the pointer (48 bits) */
    uint32_t offset;                  /* from memory: the descriptor's byte offset from the pointer */
} gcn_resource_origin;

typedef enum { GCN_IMAGE_1D, GCN_IMAGE_2D, GCN_IMAGE_3D } gcn_image_dim;
typedef enum { GCN_IMAGE_FLOAT, GCN_IMAGE_UINT, GCN_IMAGE_SINT } gcn_image_sample_type;
typedef struct {
    gcn_resource_origin origin;
    gcn_image_dim dim;
    int arrayed;
    gcn_image_sample_type type;      /* how its texels read and write: floats (unorm, float, ...) or integers */
    unsigned components;             /* the format's components, 1 to 4 */
    int sampled;                     /* a sampled image (reads only), not a storage image */
    int cube;                        /* a cube, bound as a 2D array of its faces (dim 2D, arrayed) */
    uint32_t swizzle;                /* a storage image's DST_SEL (3 bits a component: 4-7 x, y, z, w), a permutation:
                                        a load's component k is the texel's DST_SEL[k] - 4, a store writes it there;
                                        0 is the identity (sampled images take theirs in the view) */
    int null_descriptor;             /* a sampled image whose descriptor is all 0 (a null descriptor): image_load reads 0 */
    int unmapped;                    /* a sampled image whose descriptor has base address 0 but is not null: a read through
                                        it writes GCN_RUNTIME_STOP_UNMAPPED_IMAGE (and reads 0); one never run is harmless */
} gcn_image_binding;

/* GCN_MAX_IMAGES: up to 16 images a program (Vulkan guarantees 16 sampled images per stage) */
enum { GCN_MAX_IMAGES = 16, GCN_IMAGE_READ = 1, GCN_IMAGE_WRITE = 2, GCN_IMAGE_SAMPLE = 4 /* read through image_sample */ };

typedef struct {
    gcn_resource_origin origin;
    unsigned access;                 /* GCN_IMAGE_READ | GCN_IMAGE_WRITE | GCN_IMAGE_SAMPLE */
} gcn_image_resource;

/* The buffer descriptors (V#, 4 words) a program's buffer and tbuffer stores write through, found the
   same way, by origin, into buffers (up to max): a caller can bound the memory a dispatch may store to
   (a store outside its descriptor's range writes nothing). *unknown is set when a store's descriptor has
   no such origin (or there are more than max): its stores may reach any memory. 0, or -1 with the reason
   (code that does not decode). */
int gcn_buffer_store_resources(const uint32_t *code, size_t dwords, gcn_resource_origin *buffers, unsigned max, unsigned *count, int *unknown,
                               char *error, size_t error_size);

/* The memory a program reads (not counting its images): each buffer descriptor its buffer and tbuffer
   loads, atomics and s_buffer_loads read through (pointer 0: the descriptor's range, found by origin as
   above), and each s_load from a user-data pointer pair the program does not write, at an immediate or
   literal offset (pointer 1: `bytes` from the pointer in s[origin.sgpr : origin.sgpr + 1] plus
   `offset`; a descriptor loaded from memory is read this way too). A caller can then bound what a
   dispatch reads, and write back only the render targets in that memory. *unknown is set when a load
   has neither (or there are more than max reads, or the program reads memory some other way): it may
   read any memory. 0, or -1 with the reason (code that does not decode). */
typedef struct {
    int pointer;
    gcn_resource_origin origin;
    uint32_t offset, bytes;
} gcn_memory_read;
int gcn_memory_reads(const uint32_t *code, size_t dwords, gcn_memory_read *reads, unsigned max, unsigned *count, int *unknown, char *error,
                     size_t error_size);

/* A table-driven copy: a compute program that loads its workgroup's entry of three 32-bit words (x, y,
   count) from a table buffer, indexed by its workgroup's x index, and in each thread (lane id v0, its
   local x index) loads the source buffer's element y + v0 and stores it as the destination buffer's
   element x + v0 (both indexed, IDXEN, with no offsets), threads past the count doing nothing. The game
   copies depth this way (the compute shader at 0x20d1b00100), through descriptors whose ranges span
   most of the memory; its table, written by the CPU, gives the copy's real source and destination.
   The match is strict: only those three buffer accesses, v_mov_b32, v_add_i32 and compares to build
   the indices, exec masking, waits and branches to the program's end, and no write to the
   descriptors' SGPRs or the workgroup index's. Elements past the count are taken as read and stored
   (lanes up to the workgroup's width), so the caller's ranges cover the copy. 1 and *shape when the
   program is one, 0 otherwise. */
typedef struct {
    unsigned table_sgpr, source_sgpr, destination_sgpr; /* each descriptor's first SGPR, in the user data */
    unsigned workgroup_sgpr;                           /* the SGPR holding the workgroup's x index */
    int table_format;                                  /* 1: the table is read by buffer_load_format_xyz, 0: buffer_load_dwordx3 */
} gcn_table_copy;
int gcn_table_copy_shape(const uint32_t *code, size_t dwords, gcn_table_copy *shape);

/* Samplers (S#, 4 words), which image_sample names by its SSAMP SGPRs, are found the same way (an
   s_load_dwordx4 for one from memory). The caller binds sampler j at set 0, binding image_count + j,
   and makes it from the descriptor. */
enum { GCN_MAX_SAMPLERS = 16 }; /* as GCN_MAX_IMAGES, Vulkan's least per-stage limit for each */

/* The distinct sampler descriptors a program's image_sample instructions use, by origin. 0 and
   *count, or -1 with the reason. */
int gcn_sampler_resources(const uint32_t *code, size_t dwords, gcn_resource_origin *samplers, unsigned max, unsigned *count, char *error,
                          size_t error_size);

/* The distinct image descriptors a program's MIMG instructions use, by origin, into resources (up to
   max). 0 and *count, or -1 with the reason (more images than max, code that does not decode, a
   descriptor whose origin is not one of the two above). */
int gcn_image_resources(const uint32_t *code, size_t dwords, gcn_image_resource *resources, unsigned max, unsigned *count, char *error,
                        size_t error_size);

/* The same for a program that calls a fetch shader (gcn_call_sgpr): the callee's code (callee_dwords
   long, through its s_setpc_b64 return), so that a descriptor the call leaves alone keeps its origin
   past the call. A descriptor the callee writes stops by name. */
int gcn_called_image_resources(const uint32_t *code, size_t dwords, const uint32_t *callee, size_t callee_dwords,
                               gcn_image_resource *resources, unsigned max, unsigned *count, char *error, size_t error_size);

/* The local data share (LDS): a workgroup's shared memory, which the ds_* instructions read and write at
   a byte address (the address VGPR plus the instruction's offsets). bytes is its size (0: a ds_*
   instruction stops translation); an access at or past it reads 0 and writes nothing, as the hardware
   bounds LDS accesses by the allocation. address 0 keeps it as workgroup shared memory (a compute
   shader's, gone when the workgroup ends); otherwise it is memory at that GPU address, workgroup g's at
   address + g * bytes, and lasts from one pass to the next (the tessellation stages, below). s_barrier
   waits for the workgroup's lanes and makes their LDS writes visible to each other. */
typedef struct {
    uint32_t bytes;
    uint64_t address;
} gcn_lds;

/* The global data share (GDS): 64 KB beside guest memory, the same for every shader (runtime/gpu_memory.h).
   A compute shader's ds_append and ds_consume with the GDS bit add their wave's count of active lanes
   to, or take it from, the dword at GDS byte M0[31:16] + the instruction's offset, and give every active
   lane the dword's old value; with v_mbcnt (its lane's rank among the wave's active lanes) each lane
   gets a slot of its own in an append buffer. gds_address is the GDS's GPU address (0: these stop
   translation). Here a wave is a subgroup of the Vulkan device: one subgroup lane adds the subgroup's
   count, and v_mbcnt with an EXEC half ranks a lane among the active lanes of its subgroup (exact where
   subgroups have 64 lanes; with 32-lane subgroups each holds the low half of a wave of its own), so the
   slots stay unique and dense. An access past the GDS changes nothing and gives 0. */
enum { GCN_GDS_BYTES = 64 << 10 };

typedef struct {
    uint32_t local_size[3];  /* the workgroup's size */
    unsigned user_sgprs;     /* s0 .. s(user_sgprs - 1) start as the user data (push constants) */
    uint32_t workgroup_id_enables; /* bits 0-2: the workgroup's x, y, z index each start in an SGPR, in that
                                      order right after the user data (COMPUTE_PGM_RSRC2's TGID_X/Y/Z_EN) */
    uint32_t push_constant_offset;
    uint64_t address_offset;
    uint64_t error_address;   /* the run-time stop word (GCN_RUNTIME_STOP_*), or 0: run-time cases stop translation */
    const gcn_image_binding *images;
    unsigned image_count;
    uint32_t *user_data_read; /* when not NULL, set on success to the user SGPRs read before written (User data read, below) */
    gcn_lds lds;              /* the workgroup's LDS (above) */
    uint64_t gds_address;     /* the GDS's GPU address (above), or 0 */
} gcn_compute_layout;

/* User data read: a translation can report which of the user SGPRs (s0 to s(user_sgprs - 1)) the program
   reads before it writes them, taking the instructions in program order (a read in one branch after a
   write in another counts as written). The caller can then refuse to run it on user data no call has
   set. */

/* Translates the compute program at code (dwords long, through its s_endpgm). On success 0, and
   *spirv holds a SPIR-V module the caller frees; otherwise -1 and error says why. The push
   constants hold the user data: GCN_USER_SGPR_MAX 32-bit words. Lane i of the workgroup starts
   with v0 = i (x), v1 = y and v2 = z. */
int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
                          uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* A vertex shader (GCN's hardware VS stage). Its only input is the vertex index; the shader's
   input settings say which VGPR the hardware puts it in. Exports: pos0 becomes the position,
   param n the output at location n. The position exports after pos0 carry what
   output_control (the shader's PA_CL_VS_OUT_CNTL) enables, in this order: the misc vector, then
   clip/cull-distance vectors 0 and 1 (distances 0-3 and 4-7). Clip distances become SPIR-V's
   ClipDistance; the misc vector and cull distances stop translation. */
typedef struct {
    unsigned user_sgprs;
    unsigned vertex_index_vgpr;
    uint32_t output_control;
    uint32_t push_constant_offset;
    uint64_t address_offset;
    uint64_t error_address;   /* the run-time stop word (GCN_RUNTIME_STOP_*), or 0: run-time cases stop translation */
    /* the fetch shader the program calls (gcn_call_sgpr), read from the user data's address by the
       caller, and the program's own guest address (for the return address the call saves); NULL when
       it makes no call */
    const uint32_t *callee;
    size_t callee_dwords;
    uint64_t code_address;
    /* the images it loads (image_load, image_load_mip: sampled images, read only), in set 0 from
       binding first_binding on, after the pixel shader's images and samplers */
    const gcn_image_binding *images;
    unsigned image_count;
    unsigned first_binding;
    uint32_t *user_data_read; /* when not NULL, set on success to the user SGPRs read before written (User data read, below) */
    /* a geometry shader's copy shader (Geometry shaders, below): its ring loads read the GSVS ring, here
       on the GPU; 0 for any other vertex shader */
    uint64_t gsvs_ring, gsvs_ring_bytes;
    /* instancing: the input VGPRs the hardware fills after the vertex index (VGPR_COMP_CNT, 0 to 3): v1 the
       instance index over VGT_INSTANCE_STEP_RATE_0, v2 over VGT_INSTANCE_STEP_RATE_1, v3 the instance index
       (shadPS4's translate.cpp lays them out so, facts only). step_rates holds the two registers, the ones
       set in step_rates_set (bit 0, bit 1); a program that reads v1 or v2 before writing it, its step rate
       not set or 0, stops translation (taking the instructions in program order, as User data read below) */
    unsigned instance_vgprs;
    uint32_t step_rates[2];
    unsigned step_rates_set;
} gcn_vertex_layout;

/* Calls. A vertex shader may start by calling a fetch shader, which loads its vertex inputs:
   s_swappc_b64 to the address in a pair of SGPRs, saving the return address in another pair; the fetch
   shader ends with s_setpc_b64 back to it. The translator inlines the callee in place of the call:
   exactly what the call runs when the callee is straight-line code ending with that return, which it
   checks (no branch, no image instruction, no write of the return pair). The target pair must be the
   user data's, untouched before the call (no branch before it either), and there may be one call.
   Returns 1 and the target pair's first SGPR in *sgpr when the program makes such a call, 0 when it
   makes none, -1 with the reason otherwise. */
int gcn_call_sgpr(const uint32_t *code, size_t dwords, unsigned *sgpr, char *error, size_t error_size);

int gcn_translate_vertex(const uint32_t *code, size_t dwords, const gcn_vertex_layout *layout,
                         uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* A pixel shader. Its inputs are the perspective-correct barycentrics at the pixel's centre (i, then j
   in the next VGPR), which v_interp reads (attribute n is the vertex output its SPI_PS_INPUT_CNTL
   names, below, interpolated by the GPU), and, when position_enable says so, the pixel's window position: x (bit 0) and y (bit 1) as
   floats at the pixel's centre, from position_vgpr on (y in the next VGPR when both), and, when
   front_face is set, whether the primitive faces the viewer, in front_face_vgpr: 1 or 0 with
   front_face_all_bits (SPI_BARYC_CNTL's FRONT_FACE_ALL_BITS), otherwise a float whose sign says it
   (1.0 front, -1.0 back), as shadPS4 fills that VGPR. Exports: mrt n becomes the output at location n.

   Which vertex shader output attribute n reads is SPI_PS_INPUT_CNTL_n's (input_controls; NULL: the
   output at location n). Its fields (AMD's gfx_7_2_sh_mask.h; shadPS4's PsInputControl the same):
   OFFSET bits 0-4, the vertex shader parameter read; DEFAULT_VAL bit 5 set, a constant instead, the
   one DEFAULT_VAL bits 8-9 pick ((0,0,0,0), (0,0,0,1), (1,1,1,0), (1,1,1,1)); FLAT_SHADE bit 10,
   the provoking vertex's value, not interpolated. */
enum { GCN_PIXEL_POSITION_X = 1, GCN_PIXEL_POSITION_Y = 2, GCN_PIXEL_INPUTS = 32 };
enum { GCN_PS_INPUT_OFFSET_MASK = 0x1f, GCN_PS_INPUT_USE_DEFAULT = 0x20, GCN_PS_INPUT_DEFAULT_SHIFT = 8, GCN_PS_INPUT_DEFAULT_MASK = 3,
       GCN_PS_INPUT_FLAT_SHADE = 0x400 };
typedef struct {
    unsigned user_sgprs;
    unsigned barycentric_vgpr;
    unsigned position_enable, position_vgpr;
    int front_face, front_face_all_bits;
    unsigned front_face_vgpr;
    uint32_t push_constant_offset;
    uint64_t address_offset;
    uint64_t error_address;   /* the run-time stop word (GCN_RUNTIME_STOP_*), or 0: run-time cases stop translation */
    const gcn_image_binding *images;
    unsigned image_count;
    const gcn_resource_origin *samplers;
    unsigned sampler_count;
    const uint32_t *input_controls; /* GCN_PIXEL_INPUTS SPI_PS_INPUT_CNTL words, or NULL (above) */
    uint32_t depth_export;    /* DB_SHADER_CONTROL's export enables (GCN_DEPTH_EXPORT_*), 0 when SPI_SHADER_Z_FORMAT exports nothing */
    uint32_t *user_data_read; /* when not NULL, set on success to the user SGPRs read before written (User data read, below) */
} gcn_pixel_layout;

/* DB_SHADER_CONTROL's export enables (AMD's gfx_7_2_sh_mask.h): Z_EXPORT_ENABLE bit 0,
   STENCIL_TEST_VAL_EXPORT_ENABLE 1, STENCIL_OP_VAL_EXPORT_ENABLE 2, MASK_EXPORT_ENABLE 8. With
   Z_EXPORT_ENABLE, the depth export's (exp mrtz, target 8) first component replaces the pixel's
   depth (FragDepth); the stencil and sample-mask exports stop translation; without any enable the
   export is ignored, as the hardware ignores it. */
enum { GCN_DEPTH_EXPORT_Z = 1u << 0, GCN_DEPTH_EXPORT_STENCIL = 3u << 1, GCN_DEPTH_EXPORT_MASK = 1u << 8 };

int gcn_translate_pixel(const uint32_t *code, size_t dwords, const gcn_pixel_layout *layout,
                        uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* Tessellation (docs/RENDERER.md, "Tessellation"). GCN runs a tessellated draw's control points
   through a local shader (LS), which writes them to LDS, and its patches through a hull shader (HS),
   which reads them there and writes the tessellation factors to the tessellation-factor ring; the
   hardware VS stage then runs as the domain shader for each point the tessellator makes, reading what
   the two left in LDS. Here the local and hull shaders run as compute passes over the same workgroups,
   whose LDS lives in a buffer (gcn_lds with an address), and the domain shader is the tessellation
   evaluation shader of a Vulkan tessellation pipeline, whose control shader takes the factors.

   The local shader: one workgroup per `patches` patches (VGT_LS_HS_CONFIG NUM_PATCHES), one lane per
   control point: lane l of workgroup g is control point l % input_points of patch
   g * patches + l / input_points (the draw's control point n = patch * input_points + its point). It
   starts with v0 the point's vertex index (the draw's first vertex plus n, or the index buffer's entry
   first + n), v1 = l (the hardware's RelAutoIndex, as Mesa's radeonsi names the LS inputs), v2 and v3 0
   (the instance: one instance only), and may call a fetch shader as a vertex shader does.
   The hull shader: the same workgroups, one lane per output control point: lane l is output point
   l % output_points of the workgroup's patch l / output_points; v0 = the patch's index in the draw,
   v1 = its index in the workgroup | the output point << 8; after the user SGPRs, the off-chip offset (0)
   when off_chip, then the workgroup's offset into the tessellation-factor ring, g * patches *
   factor_stride (shadPS4's frontend gives the HS these inputs, facts only). Its buffer accesses through a
   descriptor based in the ring (factor_ring, factor_ring_bytes long, a guest address) go to
   factor_buffer (a GPU address) instead. In both passes, lanes past the draw's patches start with EXEC
   clear. The draw's numbers are in memory, at the GPU address the push constants hold after the user
   data (two words, low first, at byte GCN_TESS_DRAW_OFFSET), so that an indirect draw's GPU-written count
   reaches them: GCN_TESS_DRAW_WORDS words, the draw's vertex count (its control points: patches = count /
   input_points), the first vertex (or first index), the index buffer's GPU address (low, high) and its
   index size in bytes (2 or 4; 0 for no index buffer). A draw of more patches than the LDS buffer's
   workgroups (lds_workgroups) or the factor ring hold runs none of them and writes
   GCN_RUNTIME_STOP_TESSELLATION_LIMIT to the stop word.
   The domain shader: v0 and v1 the point's (u, v) (TessCoord), v2 the patch's index in its workgroup and
   v3 its index in the draw (PrimitiveId); its LDS is workgroup PrimitiveId / patches's. Its exports are a
   vertex shader's (gcn_translate_vertex), and domain, partitioning and topology (VGT_TF_PARAM's, the
   GCN_TESS_* values) its execution modes; power-of-two partitioning is integer spacing, the control
   shader rounding the factors. */
enum { GCN_TESS_ISOLINES = 0, GCN_TESS_TRIANGLES = 1, GCN_TESS_QUADS = 2 };
enum { GCN_TESS_INTEGER = 0, GCN_TESS_POW2 = 1, GCN_TESS_FRACTIONAL_ODD = 2, GCN_TESS_FRACTIONAL_EVEN = 3 };
enum { GCN_TESS_POINTS = 0, GCN_TESS_LINES = 1, GCN_TESS_TRIANGLES_CW = 2, GCN_TESS_TRIANGLES_CCW = 3 };
enum { GCN_TESS_DRAW_OFFSET = 64, GCN_TESS_DRAW_VERTICES = 0, GCN_TESS_DRAW_FIRST, GCN_TESS_DRAW_INDEX_LOW, GCN_TESS_DRAW_INDEX_HIGH,
       GCN_TESS_DRAW_INDEX_SIZE, GCN_TESS_DRAW_WORDS };
typedef struct {
    unsigned user_sgprs;
    uint32_t patches, input_points, output_points; /* VGT_LS_HS_CONFIG */
    uint32_t push_constant_offset;   /* the domain shader's; the local and hull shaders' user data is at 0 */
    uint64_t address_offset;
    uint64_t error_address;
    gcn_lds lds;                     /* the workgroups' LDS, in a buffer (address not 0) */
    uint32_t lds_workgroups;         /* the workgroups the LDS buffer holds */
    /* the local shader's fetch shader and its own guest address, as gcn_vertex_layout's */
    const uint32_t *callee;
    size_t callee_dwords;
    uint64_t code_address;
    /* the hull shader's */
    int off_chip;                    /* SPI_SHADER_PGM_RSRC2_HS's OC_LDS_EN */
    uint32_t factor_stride;          /* bytes of factors a patch: 4 for each outer and inner factor */
    uint64_t factor_ring, factor_ring_bytes, factor_buffer;
    /* the domain shader's: as gcn_vertex_layout's, and VGT_TF_PARAM's fields */
    uint32_t output_control;
    const gcn_image_binding *images;
    unsigned image_count, first_binding;
    uint32_t domain, partitioning, topology;
    uint32_t *user_data_read;
} gcn_tessellation_layout;

int gcn_translate_local(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                        size_t *spirv_word_count, char *error, size_t error_size);
int gcn_translate_hull(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                       size_t *spirv_word_count, char *error, size_t error_size);
int gcn_translate_domain(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                         size_t *spirv_word_count, char *error, size_t error_size);

/* The tessellation pipeline's other two stages, which carry no game code: a vertex shader that does
   nothing (one control point a patch, which the control shader does not read), and the control shader,
   one output point, which reads patch PrimitiveId's factors from factor_buffer + PrimitiveId *
   factor_stride (where the hull pass left them) as 32-bit floats, clamps each to [min_level,
   max_level] (VGT_HOS_MIN/MAX_TESS_LEVEL) but keeps an outer factor of 0 or less (or NaN) at 0, which culls
   the patch (Direct3D 11's tessellator rule), rounds them up to a power of two for GCN_TESS_POW2, and
   writes them as the tessellation levels. The ring's order: quads outer 0-3 then inner 0-1, triangles
   outer 0-2 then inner, isolines the line detail then the line density (Mesa's radeonsi writes isolines'
   two outer levels reversed, facts only): Vulkan's outer level 0 is the density. Both return 0 and a
   module the caller frees, or -1 with the reason. */
typedef struct {
    uint32_t domain, partitioning;
    float min_level, max_level;
    uint64_t factor_buffer;
    uint32_t factor_stride;
} gcn_tessellation_control;
int gcn_tessellation_control_shader(const gcn_tessellation_control *control, uint32_t **spirv, size_t *spirv_word_count, char *error,
                                    size_t error_size);
int gcn_tessellation_vertex_shader(uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* Geometry shaders (docs/RENDERER.md, "Geometry shaders"). GCN runs a draw with a geometry shader
   (VGT_SHADER_STAGES_EN 0xb0) in three hardware stages that talk through two rings in memory: the export
   shader (ES) runs once per vertex and writes it to the ESGS ring; the geometry shader (GS) runs once per
   primitive, reads its vertices there, writes the vertices it makes to the GSVS ring and tells the
   hardware with s_sendmsg GS emit and cut which ones to draw, as strips; then the copy shader (the
   hardware VS stage) runs once per drawn vertex and reads it from the GSVS ring. Here the export and
   geometry shaders run as compute passes of 64-lane workgroups (one wave each) over ring buffers the
   layout gives, the emits and cuts become an index buffer of strips with restarts, and the copy shader
   is the vertex shader of an indexed draw over it.

   Ring accesses are the buffer_load/store_dword* instructions with both GLC and SLC set (shadPS4's
   ring_access_elimination.cpp tells them apart the same way, facts only); their descriptors are not
   read. The rings are laid out as the hardware lays them out (Mesa's radeonsi for Sea Islands, facts
   only): a store is swizzled, dword d of the access (its instruction offset plus its VGPR offset with
   OFFEN, over 4) of lane l at ring + soffset + d * 256 + l * 4, so each wave's block holds dword d of its
   64 lanes together; a load is linear, at ring + soffset + its instruction offset + its VGPR offset. An
   access outside its ring reads 0 or writes nothing.

   The export shader: lane l of workgroup g is the draw's vertex n = g * 64 + l (lanes past the draw's
   vertices start with EXEC clear); v0 is its vertex index as a local shader's (the draw's record, below),
   v1 to v3 are 0 (one instance), and after the user SGPRs comes its ESGS offset, g * esgs_itemsize * 256
   (VGT_ESGS_RING_ITEMSIZE dwords a vertex). It may call a fetch shader as a vertex shader does.
   The geometry shader: lane l of workgroup g is the draw's primitive p = g * 64 + l, of input_primitive's
   vertices (GCN_GS_INPUT_*: a triangle strip's primitive p takes vertices p, p + 1, p + 2, p + 1 first
   when p is odd, keeping the winding); v0, v1, v3 hold its vertices' ESGS offsets (in dwords: vertex n at
   (n / 64) * esgs_itemsize * 64 + n % 64, where the export pass wrote it), v2 the primitive's index,
   v4 to v7 0 (no adjacency, one instance); after the user SGPRs, its GSVS offset g * gsvs_itemsize * 256
   (VGT_GSVS_RING_ITEMSIZE dwords a primitive) and its wave's index g (the GS wave id, for M0).
   s_sendmsg GS emit (stream 0) appends the invocation's next vertex k (counted from 0 by its emits,
   the GSVS slot its stores at instruction offset (dword * max_vertices + k) * 4 wrote) to its strip: the
   index g * gsvs_itemsize * 64 + k * 64 + l, the copy shader's vertex index for that slot; emits past
   max_vertices (VGT_GS_MAX_VERT_OUT) are dropped, as the hardware drops them. GS cut ends the strip with a
   restart index (0xffffffff), emit-cut does both. Primitive p's strip is gcn_geometry_strip_indices
   (max_vertices) 32-bit indices at strip_buffer + p * 4 * that, which the caller fills with restarts
   first (a cut after a cut, or before any emit, adds nothing, so there is always room for a last restart
   between one primitive's strip and the next's). Both read the draw's numbers in its record, as the
   tessellation passes do (GCN_TESS_DRAW_*, at the address in the push constants after the user data), so
   that an indirect draw's GPU-written vertex count reaches them; the geometry pass's lane 0 (primitive 0)
   writes the strips' indexed draw there in turn, from word GCN_GS_DRAW_ARGUMENTS on, as Vulkan's
   VkDrawIndexedIndirectCommand: the primitives times gcn_geometry_strip_indices, 1 instance, then 0, 0, 0.
   A draw of more vertices or primitives than the rings or the strip buffer hold runs none of them, draws
   nothing and writes GCN_RUNTIME_STOP_GEOMETRY_LIMIT. Emits to another stream, and the export and geometry
   shaders' other ways of meeting the rings (an indexed ring access, a format access), stop translation.
   The copy shader is a vertex shader (gcn_vertex_layout) with its gsvs_ring set: its vertex index is the
   strip's, and its ring loads read the GSVS ring. */
enum { GCN_GS_DRAW_ARGUMENTS = 8, GCN_GS_DRAW_ARGUMENT_WORDS = 5, GCN_GS_DRAW_RECORD_WORDS = 16 };
enum { GCN_GS_INPUT_POINTS = 0, GCN_GS_INPUT_LINES = 1, GCN_GS_INPUT_TRIANGLES = 2, GCN_GS_INPUT_TRIANGLE_STRIP = 3,
       GCN_GS_WAVE_LANES = 64 };
typedef struct {
    unsigned user_sgprs;
    uint32_t input_primitive;        /* GCN_GS_INPUT_* */
    uint32_t esgs_itemsize, gsvs_itemsize, max_vertices; /* VGT_ESGS_RING_ITEMSIZE, VGT_GSVS_RING_ITEMSIZE, VGT_GS_MAX_VERT_OUT */
    uint64_t esgs_ring, esgs_ring_bytes, gsvs_ring, gsvs_ring_bytes; /* GPU addresses and sizes */
    uint64_t strip_buffer, strip_buffer_bytes;
    uint64_t address_offset;
    uint64_t error_address;
    /* the export shader's fetch shader and its own guest address, as gcn_vertex_layout's */
    const uint32_t *callee;
    size_t callee_dwords;
    uint64_t code_address;
    uint32_t *user_data_read;
} gcn_geometry_layout;

/* The 32-bit indices a primitive's strip takes: an index for each emitted vertex and a restart after
   each, at most, and one more. */
static inline uint32_t gcn_geometry_strip_indices(uint32_t max_vertices) { return 2 * max_vertices + 1; }

int gcn_translate_export(const uint32_t *code, size_t dwords, const gcn_geometry_layout *layout, uint32_t **spirv, size_t *spirv_word_count,
                         char *error, size_t error_size);
int gcn_translate_geometry(const uint32_t *code, size_t dwords, const gcn_geometry_layout *layout, uint32_t **spirv,
                           size_t *spirv_word_count, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
