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

typedef struct {
    uint32_t local_size[3];  /* the workgroup's size */
    unsigned user_sgprs;     /* s0 .. s(user_sgprs - 1) start as the user data (push constants) */
    int workgroup_id_x_sgpr; /* the SGPR that starts as the workgroup's x index, or -1 */
    uint32_t push_constant_offset;
    uint64_t address_offset;
    uint64_t error_address;   /* the run-time stop word (GCN_RUNTIME_STOP_*), or 0: run-time cases stop translation */
    const gcn_image_binding *images;
    unsigned image_count;
    uint32_t *user_data_read; /* when not NULL, set on success to the user SGPRs read before written (User data read, below) */
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
    uint32_t *user_data_read; /* when not NULL, set on success to the user SGPRs read before written (User data read, below) */
} gcn_pixel_layout;

int gcn_translate_pixel(const uint32_t *code, size_t dwords, const gcn_pixel_layout *layout,
                        uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
