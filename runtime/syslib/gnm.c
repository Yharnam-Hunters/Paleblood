/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: GNM (GPU commands), libSceGnmDriver. Graphics are rebuilt at the engine level
   (issue #39), so these calls answer what the engine expects back; the GPU is not emulated.

   Engine addresses below are the instructions that call each import (for a wrapper, the call
   inside it), not function entries: the decompilation tracks those functions in
   symbols/functions.csv, which runtime code must not name (tools/check_agnostic.py).

   sceGnmGetTheTessellationFactorRingBufferBaseAddress: the OpenOrbis homebrew SDK's GnmDriver.h
   only names it. The executable's only call (0x02ad3fb4) takes a 64-bit address and maps 128 KiB
   of direct memory there (sceKernelMapDirectMemory with that address as a hint, protection 0x33,
   alignment 0x10000) for the GPU's tessellation factors. The PS4 answers a fixed address of its
   own; the runtime answers one it reserves for this, below 2^40 and clear of the executable,
   modules and thread objects. It is the runtime's choice, not the PS4's value: the engine only
   maps it and hands it back to the GPU calls. */
#include "runtime/process.h"
#include "runtime/renderer.h"
#include "runtime/syslib.h"
#include "runtime/video_out.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TESSELLATION_FACTOR_RING_BASE 0xfe00000000ull

static uint64_t tessellation_factor_ring_base(void)
{
    return TESSELLATION_FACTOR_RING_BASE;
}

RT_SYSLIB("libSceGnmDriver", sceGnmGetTheTessellationFactorRingBufferBaseAddress, tessellation_factor_ring_base);

/* sceGnmValidateCommandBuffers(count, buffers, sizes, constant buffers, constant sizes): asks the GPU
   driver's debug validation layer to check command buffers. Paleblood has no such layer: the GPU is
   not emulated, and the command buffers are not what it draws from. The executable's start-up probe
   (calling at 0x02abb94b, from the graphics set-up) validates a tiny 16-byte buffer and
   treats exactly one result, 0x80d13fff, as "validation is not available"; the set-up then keeps
   the engine's validation option off (it stores the probe's answer AND its configuration flag). The
   runtime gives that answer for every call, which is true of Paleblood and is the value the game's
   own code names; no buffer is read or written. That a retail console answers the same is an
   inference (the probe exists so retail builds run without validation), not an observation. */
#define GNM_VALIDATION_NOT_AVAILABLE 0x80d13fff

static int32_t validate_command_buffers(uint32_t count, void **buffers, uint32_t *sizes,
                                        void **constant_buffers, uint32_t *constant_sizes)
{
    (void)count;
    (void)buffers;
    (void)sizes;
    (void)constant_buffers;
    (void)constant_sizes;
    return (int32_t)GNM_VALIDATION_NOT_AVAILABLE;
}

RT_SYSLIB("libSceGnmDriver", sceGnmValidateCommandBuffers, validate_command_buffers);

/* sceGnmDispatchInitDefaultHardwareState and sceGnmDrawInitDefaultHardwareState200(command buffer,
   dwords available): write the packets that put the GPU's compute or graphics state in its default,
   and return how many dwords they wrote. The engine's wrappers (0x01489c94, five callers including
   the graphics set-up; 0x014728b4, seven callers) reserve 256 dwords, pass the cursor
   and the room left, and advance their cursor by the count; nothing checks the count or reads the
   packets back: only the GPU does. Paleblood has no GPU to set up (graphics are rebuilt at the engine
   level, issue #39, and the renderer sets its own state), so these write nothing and answer 0
   dwords, which is what they did. */
static uint32_t init_default_hardware_state(uint32_t *command_buffer, uint64_t dwords_available)
{
    (void)command_buffer;
    (void)dwords_available;
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmDispatchInitDefaultHardwareState, init_default_hardware_state);
RT_SYSLIB("libSceGnmDriver", sceGnmDrawInitDefaultHardwareState200, init_default_hardware_state);

/* Calls that write a fixed-size packet into space the engine has reserved (the engine passes that
   size and advances its cursor by it when the call answers 0) fill the space with one PM4 type-3 NOP
   packet instead: a header 0xc0001000 with the count of the dwords that follow (minus one) in bits
   16-29, as AMD's public PM4 packet format defines it. The command buffer stays well-formed for
   anything that reads it, and nothing in it acts: Paleblood draws through its own renderer. */
#define PM4_TYPE3_NOP 0xc0001000u
#define PM4_COUNT_SHIFT 16

static void fill_with_nop(const char *function, uint32_t *command_buffer, uint32_t dwords)
{
    if (!command_buffer || dwords < 2)
        rt_fatal(function, "no reserved space for its packet");
    command_buffer[0] = PM4_TYPE3_NOP | ((dwords - 2) << PM4_COUNT_SHIFT);
    for (uint32_t i = 1; i < dwords; i++)
        command_buffer[i] = 0;
}

/* sceGnmSetCsShader(command buffer, dwords, shader): binds a compute shader. The engine's wrapper
   (0x0148a8b0, four callers including a set-up) reserves 25 dwords, passes 25 and
   advances by 25 dwords only when the answer is 0; nothing else reads the answer. The shader is its
   register block (7 words: COMPUTE_PGM_LO, PGM_HI, PGM_RSRC1, PGM_RSRC2, NUM_THREAD_X, _Y, _Z, as
   shadPS4's gnmdriver.cpp writes them), handed to the renderer like the vertex and pixel shaders'. */
static int32_t set_cs_shader(uint32_t *command_buffer, uint32_t dwords, const uint32_t *registers)
{
    static const char fn[] = "sceGnmSetCsShader";
    if (!registers) rt_fatal(fn, "no compute shader registers: the answer is not established");
    fill_with_nop(fn, command_buffer, dwords);
    rt_renderer_bind_shader(fn, RT_RENDERER_COMPUTE_SHADER, command_buffer, registers);
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSetCsShader, set_cs_shader);

/* The other command-buffer writers the engine calls, each through one thin wrapper that reserves a
   fixed or computed number of dwords at its cursor, passes the cursor and that count first, and
   advances the cursor by the count (when the call answers 0, or always). OpenOrbis' GnmDriver.h
   only names them; the shapes come from the wrappers:
     shader stages (the shader's registers): SetEsShader 0x014755a9 (20 dwords), SetGsShader
       0x01475a10 (29), SetHsShader 0x01475619 (30), SetLsShader 0x01475819 (23), SetPsShader
       0x01475400 (40), SetVsShader 0x014754c9 (29), and UpdateGsShader 0x01475a70, UpdateHsShader
       0x01475689, UpdatePsShader 0x01475460, UpdateVsShader 0x01475539 with the same counts;
     debug markers, for GPU debugging tools (counts from the label's length): InsertPushMarker
       0x01478038, InsertPopMarker 0x01478119 (6), InsertSetMarker 0x014774c8 and 0x0148ae68,
       InsertPushColorMarker 0x014780bf and 0x0148b77f, InsertSetColorMarker 0x0147754f and
       0x0148aeef;
     InsertWaitFlipDone 0x01478489 (7): makes the GPU wait until the display no longer shows a
       buffer before drawing into it.
   Paleblood has no GPU to hold shader registers, no GPU debugger to read markers, and no GPU that
   could draw into a buffer early, so each fills its reserved space with one PM4 NOP packet and
   answers 0, as sceGnmSetCsShader does: the command buffer stays well-formed and nothing in it
   acts. The arguments after the count are not used. */
static int32_t write_nothing_for_the_gpu(uint32_t *command_buffer, uint32_t dwords)
{
    fill_with_nop("a GNM command-buffer writer", command_buffer, dwords);
    return 0;
}
/* The vertex and pixel shader calls also hand the renderer the shader they bind (runtime/renderer.h),
   placed where they write in the command buffer:
     sceGnmSetVsShader / sceGnmUpdateVsShader(command buffer, dwords, registers, modifier);
     sceGnmSetPsShader / sceGnmUpdatePsShader(command buffer, dwords, registers).
   The argument order is shadPS4's libSceGnmDriver (gnmdriver.cpp), and a boot shows the registers
   argument holding the 7 and 12 words of runtime/render_calls.h (the code address, resources, export
   and input settings, matching the shaders' own code). A null pixel shader block means no pixel
   shader. The vertex shader's modifier (set when it calls a fetch shader, which the game's do)
   replaces two fields of its PGM_RSRC1, the SGPR count (bits 6-9) and the input VGPR count
   (bits 24-25): PGM_RSRC1 & 0xfcfffc3f | modifier, as shadPS4 applies it; a modifier with any other
   bit set is refused there, and stops here by name. */
#define VS_MODIFIER_FIELDS 0x030003c0u /* PGM_RSRC1's SGPRS and VGPR_COMP_CNT */
static int32_t set_vs_shader(uint32_t *command_buffer, uint32_t dwords, const uint32_t *registers, uint32_t modifier,
                             const char *function)
{
    if (!registers) rt_fatal(function, "no vertex shader registers: the answer is not established");
    if (modifier & ~VS_MODIFIER_FIELDS) rt_fatal(function, "a shader modifier with bits outside PGM_RSRC1's SGPRS and VGPR_COMP_CNT");
    uint32_t modified[7];
    memcpy(modified, registers, sizeof modified);
    if (modifier) modified[2] = (modified[2] & ~VS_MODIFIER_FIELDS) | modifier;
    fill_with_nop(function, command_buffer, dwords);
    rt_renderer_bind_shader(function, RT_RENDERER_VERTEX_SHADER, command_buffer, modified);
    return 0;
}

static int32_t set_ps_shader(uint32_t *command_buffer, uint32_t dwords, const uint32_t *registers, const char *function)
{
    fill_with_nop(function, command_buffer, dwords);
    rt_renderer_bind_shader(function, RT_RENDERER_PIXEL_SHADER, command_buffer, registers);
    return 0;
}

static int32_t gnm_set_vs_shader(uint32_t *b, uint32_t n, const uint32_t *r, uint32_t m) { return set_vs_shader(b, n, r, m, "sceGnmSetVsShader"); }
static int32_t gnm_update_vs_shader(uint32_t *b, uint32_t n, const uint32_t *r, uint32_t m) { return set_vs_shader(b, n, r, m, "sceGnmUpdateVsShader"); }
static int32_t gnm_set_ps_shader(uint32_t *b, uint32_t n, const uint32_t *r) { return set_ps_shader(b, n, r, "sceGnmSetPsShader"); }
static int32_t gnm_update_ps_shader(uint32_t *b, uint32_t n, const uint32_t *r) { return set_ps_shader(b, n, r, "sceGnmUpdatePsShader"); }

RT_SYSLIB("libSceGnmDriver", sceGnmSetVsShader, gnm_set_vs_shader);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateVsShader, gnm_update_vs_shader);
RT_SYSLIB("libSceGnmDriver", sceGnmSetPsShader, gnm_set_ps_shader);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdatePsShader, gnm_update_ps_shader);
RT_SYSLIB("libSceGnmDriver", sceGnmSetEsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetGsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetHsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetLsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateGsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateHsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPushMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPopMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertSetMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPushColorMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertSetColorMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertWaitFlipDone, write_nothing_for_the_gpu);

/* sceGnmIsUserPaEnabled(): whether the GPU's user performance-analysis mode is on. The engine wraps
   it as "on when nonzero" (0x01489c44) and asks from many draw-command builders, which then add the extra packets that mode needs. Paleblood has no such GPU mode, so the
   answer is 0, off; that a retail console also answers off is an inference (it is a development
   feature), not an observation. */
static int32_t is_user_pa_enabled(void)
{
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmIsUserPaEnabled, is_user_pa_enabled);

/* sceGnmMapComputeQueue(pipe, queue, ring, ring size in dwords, read pointer): OpenOrbis' GnmDriver.h
   only names it, so the shape comes from the engine's wrapper (0x01489aa4, called from its compute
   queue setup). The wrapper passes the pipe and queue from the queue object, the ring
   buffer, its size in dwords and the address of the GPU's read pointer; a negative result is
   failure, and otherwise the wrapper keeps the result as the queue's id and sets the read pointer
   to 0. The setup then reports success only when that id is not 0. The runtime does not run the
   GPU, so the ring is never read: it records each queue for the engine-level graphics layer and
   answers ids from 1. Mapping the same pipe and queue twice and a missing ring or read pointer
   stop by name. */
enum { MAX_COMPUTE_QUEUES = 64 };

typedef struct {
    uint32_t pipe, queue;
    void *ring;
    uint32_t ring_dwords;
    volatile uint32_t *read_pointer;
} compute_queue;

static pthread_mutex_t queues_lock = PTHREAD_MUTEX_INITIALIZER;
static compute_queue queues[MAX_COMPUTE_QUEUES];
static int queue_count;

static int32_t map_compute_queue(uint32_t pipe, uint32_t queue, void *ring, uint32_t ring_dwords,
                                 volatile uint32_t *read_pointer)
{
    static const char fn[] = "sceGnmMapComputeQueue";
    if (!ring || !ring_dwords || !read_pointer) rt_fatal(fn, "no ring, size or read pointer: the error value is not established");
    pthread_mutex_lock(&queues_lock);
    for (int i = 0; i < queue_count; i++)
        if (queues[i].pipe == pipe && queues[i].queue == queue)
            rt_fatal(fn, "a pipe and queue mapped twice: the answer is not established");
    if (queue_count == MAX_COMPUTE_QUEUES) rt_fatal(fn, "more compute queues than the runtime records");
    queues[queue_count] = (compute_queue){pipe, queue, ring, ring_dwords, read_pointer};
    const int32_t id = ++queue_count; /* ids from 1: the engine treats 0 as failure */
    pthread_mutex_unlock(&queues_lock);
    return id;
}

RT_SYSLIB("libSceGnmDriver", sceGnmMapComputeQueue, map_compute_queue);

/* sceGnmDingDong(queue id, write offset in dwords): tells the GPU that the ring of a compute queue
   holds new work up to the write offset. OpenOrbis' GnmDriver.h only names it; the shape comes from
   the engine's compute submit (0x0148c987), which copies one 4-dword INDIRECT_BUFFER packet per
   command buffer into the ring (its packet writer), wraps to the ring's start when
   the end is full, then rings the doorbell with the queue id from sceGnmMapComputeQueue and the new
   write offset. Before writing, it checks the free space against the read pointer the GPU advances.

   TEMPORARY STAND-IN (maintainer decision, 4 November 2026): Paleblood has no GPU, and the engine's
   own renderer is to replace this layer (issue #39). Until then the doorbell completes the work at
   once, on the calling thread: it follows each indirect buffer and acts only on the packets whose
   effect the CPU can see. A boot on 4 November found four kinds in the engine's compute buffers
   (packet types from AMD's public PM4 packet format):
     NOP and SET_SH_REG      skipped: padding and shader registers, with no GPU to hold them;
     DISPATCH_DIRECT         skipped: compute shaders do not run, so their output is never written;
     RELEASE_MEM             performed: the "work done" value goes to its memory address, which is
                             what the CPU waits on.
   Each indirect buffer is then handed to the renderer, which drops its recorded calls without
   running them (rt_renderer_skip, runtime/renderer.h). Then the read pointer moves to the write
   offset, so the ring never fills. Any other packet, or a
   completion this stand-in does not perform, stops by name. */
#define PM4_TYPE_SHIFT 30
#define PM4_TYPE3 3u
#define PM4_OPCODE_SHIFT 8
#define PM4_OPCODE_MASK 0xffu
#define PM4_COUNT_MASK 0x3fffu
#define PM4_HEADER_AND_COUNT_BIAS 2 /* the count field is the payload length minus one */

#define PM4_NOP 0x10u
#define PM4_DISPATCH_DIRECT 0x15u
#define PM4_WAIT_REG_MEM 0x3cu
#define PM4_INDIRECT_BUFFER 0x3fu
#define PM4_EVENT_WRITE 0x46u
#define PM4_EVENT_WRITE_EOP 0x47u
#define PM4_RELEASE_MEM 0x49u
#define PM4_ACQUIRE_MEM 0x58u
#define PM4_SET_SH_REG 0x76u
#define PM4_DRAW_INDEX_AUTO 0x2du
#define PM4_NUM_INSTANCES 0x2fu
#define PM4_SET_CONTEXT_REG 0x69u
#define PM4_SET_UCONFIG_REG 0x79u
#define PM4_DMA_DATA 0x50u
#define PM4_INDEX_TYPE 0x2au
#define PM4_INDEX_BASE 0x26u
#define PM4_INDEX_BUFFER_SIZE 0x13u
#define PM4_DRAW_INDEX_2 0x27u
#define PM4_DRAW_INDEX_OFFSET_2 0x35u

/* DMA_DATA (AMD's public cikd.h): control; source address (or data); destination address;
   command, with the byte count in bits 0-20. */
#define DMA_DATA_DWORDS 7
#define DMA_DATA_SOURCE_SELECT(control) ((control) >> 29 & 3u)  /* 0 memory, 1 GDS, 2 data, 3 memory through L2 */
#define DMA_DATA_DESTINATION_SELECT(control) ((control) >> 20 & 3u) /* 0 by the command's DAS, 1 GDS, 3 memory through L2 */
#define DMA_DATA_SWAPS(command) ((command) >> 22 & 0xfu)       /* source and destination byte swaps */
#define DMA_DATA_SOURCE_IS_REGISTER (1u << 26)                 /* SAS */
#define DMA_DATA_DESTINATION_IS_REGISTER (1u << 27)            /* DAS */
#define DMA_DATA_SOURCE_FIXED (1u << 28)                       /* SAIC: the source is not incremented */
#define DMA_DATA_DESTINATION_FIXED (1u << 29)                  /* DAIC: the destination is not incremented */
#define CP_DMA_READ_TAGS_BYTE_ADDRESS (0xc08bu * 4)             /* mmCP_DMA_READ_TAGS, gfx_7_2_d.h */

#define EVENT_WRITE_WITHOUT_MEMORY_DWORDS 2
#define EVENT_WRITE_EOP_DWORDS 6
#define EVENT_WRITE_EOP_DATA_SHIFT 29

#define WAIT_REG_MEM_DWORDS 7
#define WAIT_FUNCTION_MASK 7u
#define WAIT_ON_MEMORY 0x10u

#define INDIRECT_BUFFER_DWORDS 4
#define INDIRECT_BUFFER_ADDRESS_HIGH_MASK 0xffffu
#define INDIRECT_BUFFER_SIZE_MASK 0xfffffu
#define MAX_INDIRECT_BUFFER_DEPTH 4

#define RELEASE_MEM_DWORDS 7
#define RELEASE_MEM_DESTINATION_SHIFT 16
#define RELEASE_MEM_DESTINATION_MASK 3u
#define RELEASE_MEM_TO_MEMORY 0u
#define RELEASE_MEM_DATA_SHIFT 29
#define RELEASE_MEM_DATA_NONE 0u
#define RELEASE_MEM_DATA_32_BITS 1u
#define RELEASE_MEM_DATA_64_BITS 2u
#define RELEASE_MEM_ADDRESS_HIGH_MASK 0xffffu

typedef struct {
    uint32_t event_control;
    uint32_t data_control; /* destination in bits 16-17, data size in bits 29-31 */
    uint32_t address_low, address_high;
    uint32_t data_low, data_high;
} release_mem_packet;

/* The GPU's steps after a submitted command buffer's work (runtime/renderer.h): while a submission's
   packets are read, its "work done" writes and memory waits are collected here instead of performed,
   and the renderer performs them once the buffer's drawing calls have run. NULL (compute rings, and
   any other caller) performs them at once. */
typedef struct {
    rt_gpu_step *steps;
    size_t count, capacity;
} step_list;
static _Thread_local step_list *collecting;
static _Thread_local uint32_t packet_offset; /* the top-level packet being read, bytes into its buffer */

static void collect(const char *fn, void (*perform)(const rt_gpu_step *), const uint64_t words[6])
{
    step_list *const list = collecting;
    if (list->count == list->capacity) {
        const size_t capacity = list->capacity ? list->capacity * 2 : 8;
        rt_gpu_step *const grown = realloc(list->steps, capacity * sizeof *grown);
        if (!grown) rt_fatal(fn, "no memory for the GPU's steps");
        list->steps = grown, list->capacity = capacity;
    }
    rt_gpu_step *const step = &list->steps[list->count++];
    step->perform = perform, step->function = fn, step->offset = packet_offset;
    memcpy(step->words, words, sizeof step->words);
}

static void perform_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high, uint32_t data_low,
                               uint32_t data_high);

static void completion_step(const rt_gpu_step *step)
{
    const uint64_t *w = step->words;
    perform_completion(step->function, (uint32_t)w[0], (uint32_t)w[1], (uint32_t)w[2], (uint32_t)w[3], (uint32_t)w[4]);
}

/* A "work done" write: the packet's own 32- or 64-bit value to a memory address (the address's
   high bits are the low 16 of address_high), after the work before it (collected while a
   submission is read). Other kinds of value (a GPU clock, for one) stop. */
static void write_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high,
                             uint32_t data_low, uint32_t data_high)
{
    if (collecting) {
        const uint64_t words[6] = {data_size, address_low, address_high, data_low, data_high, 0};
        collect(fn, completion_step, words);
        return;
    }
    perform_completion(fn, data_size, address_low, address_high, data_low, data_high);
}

static void perform_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high, uint32_t data_low,
                               uint32_t data_high)
{
    if (data_size == RELEASE_MEM_DATA_NONE) return;
    void *address = (void *)(uintptr_t)(address_low | (uint64_t)(address_high & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32);
    if (!address) rt_fatal(fn, "a completion write without an address");
    if (data_size == RELEASE_MEM_DATA_32_BITS)
        __atomic_store_n((uint32_t *)address, data_low, __ATOMIC_RELEASE);
    else if (data_size == RELEASE_MEM_DATA_64_BITS)
        __atomic_store_n((uint64_t *)address, data_low | (uint64_t)data_high << 32, __ATOMIC_RELEASE);
    else
        rt_fatal(fn, "a completion writing a value other than the packet's own data");
}

static void release_memory(const char *fn, const release_mem_packet *packet)
{
    const uint32_t destination = (packet->data_control >> RELEASE_MEM_DESTINATION_SHIFT) & RELEASE_MEM_DESTINATION_MASK;
    if (destination != RELEASE_MEM_TO_MEMORY) rt_fatal(fn, "a RELEASE_MEM to a destination other than memory");
    write_completion(fn, packet->data_control >> RELEASE_MEM_DATA_SHIFT, packet->address_low, packet->address_high,
                     packet->data_low, packet->data_high);
}

/* WAIT_REG_MEM makes the GPU wait until a value in memory compares as asked with a reference
   (under a mask). It is a step after the work before it (collected while a submission is read), and
   the renderer's thread waits for it, polling every 100 microseconds; one that does not hold within
   two seconds (the CPU it waits for may be waiting on this GPU work), a wait performed at once
   that does not already hold, and one on a register stop by name. */
typedef struct {
    uint32_t control; /* compare function in bits 0-2, memory (not a register) in bit 4 */
    uint32_t address_low, address_high;
    uint32_t reference, mask, poll_interval;
} wait_reg_mem_packet;

static int wait_holds(const char *fn, const wait_reg_mem_packet *packet);

static void wait_step(const rt_gpu_step *step)
{
    const wait_reg_mem_packet packet = {(uint32_t)step->words[0], (uint32_t)step->words[1], (uint32_t)step->words[2],
                                        (uint32_t)step->words[3], (uint32_t)step->words[4], (uint32_t)step->words[5]};
    enum { POLL_NANOSECONDS = 100000, POLLS = 20000 }; /* 100 us each: two seconds */
    const struct timespec poll = {0, POLL_NANOSECONDS};
    for (int i = 0; !wait_holds(step->function, &packet); i++) {
        if (i == POLLS) rt_fatal(step->function, "a GPU wait on memory that did not come to hold what it waits for");
        nanosleep(&poll, NULL);
    }
}

static void check_wait(const char *fn, const wait_reg_mem_packet *packet)
{
    if (!(packet->control & WAIT_ON_MEMORY)) rt_fatal(fn, "a GPU wait on a register: the stand-in has none");
    if (!(packet->address_low | (packet->address_high & RELEASE_MEM_ADDRESS_HIGH_MASK))) rt_fatal(fn, "a GPU wait without an address");
    if (collecting) {
        const uint64_t words[6] = {packet->control, packet->address_low, packet->address_high, packet->reference, packet->mask,
                                   packet->poll_interval};
        collect(fn, wait_step, words);
        return;
    }
    if (!wait_holds(fn, packet)) rt_fatal(fn, "a GPU wait on memory that does not yet hold what it waits for");
}

/* Whether a WAIT_REG_MEM's compare holds now. */
static int wait_holds(const char *fn, const wait_reg_mem_packet *packet)
{
    const volatile uint32_t *address = (const volatile uint32_t *)(uintptr_t)(packet->address_low |
                                       (uint64_t)(packet->address_high & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32);
    const uint32_t value = __atomic_load_n(address, __ATOMIC_ACQUIRE) & packet->mask, reference = packet->reference;
    int holds;
    switch (packet->control & WAIT_FUNCTION_MASK) {
    case 0: holds = 1; break;
    case 1: holds = value < reference; break;
    case 2: holds = value <= reference; break;
    case 3: holds = value == reference; break;
    case 4: holds = value != reference; break;
    case 5: holds = value >= reference; break;
    case 6: holds = value > reference; break;
    default: rt_fatal(fn, "a GPU wait with a compare function the format does not define");
    }
    return holds;
}

/* The one DMA_DATA form the stand-in knows: a read from memory into CP_DMA_READ_TAGS, which only
   brings the source into the GPU's cache. Any other form stops by name. */
static void check_dma_data(const char *fn, const uint32_t *packet)
{
    const uint32_t control = packet[1], destination_low = packet[4], destination_high = packet[5], command = packet[6];
    const uint32_t source = DMA_DATA_SOURCE_SELECT(control);
    const int read_tags = (source == 0 || source == 3) && DMA_DATA_DESTINATION_SELECT(control) == 0 &&
                          (command & DMA_DATA_DESTINATION_IS_REGISTER) && (command & DMA_DATA_DESTINATION_FIXED) &&
                          !(command & (DMA_DATA_SOURCE_IS_REGISTER | DMA_DATA_SOURCE_FIXED)) && !DMA_DATA_SWAPS(command) &&
                          destination_low == CP_DMA_READ_TAGS_BYTE_ADDRESS && destination_high == 0;
    if (!read_tags) rt_fatal(fn, "a DMA_DATA other than a read into CP_DMA_READ_TAGS: the stand-in does not copy or fill");
}

/* Stops naming the packet's opcode: the next packet kind to understand. */
static _Noreturn void unknown_packet(const char *fn, uint32_t opcode, uint32_t length)
{
    static char message[96];
    snprintf(message, sizeof message, "a PM4 packet the stand-in does not know (opcode 0x%02x, %u dwords)", opcode, length);
    rt_fatal(fn, message);
}

static void complete_packets(const char *fn, const uint32_t *packets, uint32_t dwords, int depth, int is_ring);

/* Completes an indirect buffer's packets. One a compute ring points to is also handed to the
   renderer, which drops its recorded calls (rt_renderer_skip): compute queues do not run there yet. */
static void follow_indirect_buffer(const char *fn, const uint32_t *packet, int depth, int from_ring)
{
    if (depth == MAX_INDIRECT_BUFFER_DEPTH) rt_fatal(fn, "indirect buffers nested deeper than the stand-in follows");
    const uint32_t *buffer = (const uint32_t *)(uintptr_t)(packet[1] |
                             (uint64_t)(packet[2] & INDIRECT_BUFFER_ADDRESS_HIGH_MASK) << 32);
    const uint32_t dwords = packet[3] & INDIRECT_BUFFER_SIZE_MASK;
    if (!buffer && dwords) rt_fatal(fn, "an INDIRECT_BUFFER without an address");
    complete_packets(fn, buffer, dwords, depth + 1, 0);
    if (from_ring) rt_renderer_skip(fn, buffer, dwords * (uint32_t)sizeof(uint32_t));
}

/* Acts on each packet in turn. A compute ring holds only INDIRECT_BUFFER packets; a command buffer
   holds the kinds listed above and at sceGnmSubmitCommandBuffers. */
static void complete_packets(const char *fn, const uint32_t *packets, uint32_t dwords, int depth, int is_ring)
{
    for (uint32_t at = 0; at < dwords;) {
        const uint32_t header = packets[at];
        if (header >> PM4_TYPE_SHIFT != PM4_TYPE3) rt_fatal(fn, "a packet other than PM4 type 3");
        const uint32_t opcode = (header >> PM4_OPCODE_SHIFT) & PM4_OPCODE_MASK;
        const uint32_t length = ((header >> PM4_COUNT_SHIFT) & PM4_COUNT_MASK) + PM4_HEADER_AND_COUNT_BIAS;
        if (length > dwords - at) rt_fatal(fn, "a packet running past the end of its buffer");
        const uint32_t *packet = packets + at;
        if (!depth) packet_offset = at * (uint32_t)sizeof(uint32_t); /* a step inside an indirect buffer comes at its packet */
        if (opcode == PM4_INDIRECT_BUFFER && length == INDIRECT_BUFFER_DWORDS) {
            follow_indirect_buffer(fn, packet, depth, is_ring);
        } else if (is_ring) {
            rt_fatal(fn, "a compute ring packet other than INDIRECT_BUFFER");
        } else if (opcode == PM4_RELEASE_MEM && length == RELEASE_MEM_DWORDS) {
            release_mem_packet release;
            memcpy(&release, packet + 1, sizeof release);
            release_memory(fn, &release);
        } else if (opcode == PM4_EVENT_WRITE_EOP && length == EVENT_WRITE_EOP_DWORDS) {
            write_completion(fn, packet[3] >> EVENT_WRITE_EOP_DATA_SHIFT, packet[2], packet[3], packet[4], packet[5]);
        } else if (opcode == PM4_WAIT_REG_MEM && length == WAIT_REG_MEM_DWORDS) {
            wait_reg_mem_packet wait;
            memcpy(&wait, packet + 1, sizeof wait);
            check_wait(fn, &wait);
        } else if (opcode == PM4_DMA_DATA && length == DMA_DATA_DWORDS) {
            check_dma_data(fn, packet);
        } else if (opcode == PM4_EVENT_WRITE && length != EVENT_WRITE_WITHOUT_MEMORY_DWORDS) {
            unknown_packet(fn, opcode, length); /* an event that also writes memory */
        } else if (opcode != PM4_NOP && opcode != PM4_SET_SH_REG && opcode != PM4_DISPATCH_DIRECT &&
                   opcode != PM4_ACQUIRE_MEM && opcode != PM4_EVENT_WRITE && opcode != PM4_SET_CONTEXT_REG &&
                   opcode != PM4_SET_UCONFIG_REG && opcode != PM4_NUM_INSTANCES && opcode != PM4_DRAW_INDEX_AUTO &&
                   opcode != PM4_INDEX_TYPE && opcode != PM4_INDEX_BASE && opcode != PM4_INDEX_BUFFER_SIZE &&
                   opcode != PM4_DRAW_INDEX_2 && opcode != PM4_DRAW_INDEX_OFFSET_2) {
            unknown_packet(fn, opcode, length);
        }
        at += length;
    }
}

static uint32_t completed_offset[MAX_COMPUTE_QUEUES]; /* where each ring's unread work starts, in dwords */

static void ding_dong(uint32_t queue_id, uint32_t write_offset)
{
    static const char fn[] = "sceGnmDingDong";
    pthread_mutex_lock(&queues_lock);
    if (queue_id == 0 || queue_id > (uint32_t)queue_count) rt_fatal(fn, "a queue id sceGnmMapComputeQueue did not answer");
    const compute_queue *queue = &queues[queue_id - 1];
    uint32_t *const done = &completed_offset[queue_id - 1];
    if (write_offset > queue->ring_dwords) rt_fatal(fn, "a write offset past the end of the ring");
    const uint32_t *ring = queue->ring;
    if (write_offset < *done) { /* the engine filled the ring's end and wrapped to its start */
        complete_packets(fn, ring + *done, queue->ring_dwords - *done, 0, 1);
        *done = 0;
    }
    complete_packets(fn, ring + *done, write_offset - *done, 0, 1);
    *done = write_offset;
    __atomic_store_n(queue->read_pointer, write_offset, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&queues_lock);
}

RT_SYSLIB("libSceGnmDriver", sceGnmDingDong, ding_dong);

/* sceGnmSubmitCommandBuffers(count, draw command buffers, their sizes in bytes, constant command
   buffers, their sizes): hands command buffers to the GPU's graphics queue. OpenOrbis' GnmDriver.h
   only names it; the shape comes from the engine's submit (0x02ab0c23, one of eleven callers),
   which passes one or two draw command buffers, their byte sizes rounded down to whole dwords, and
   no constant buffers, and ignores the answer. Another caller, 0x0149bf3a (through the GPU
   library's wrapper at 0x01489a10), passes the constant arrays always, with an entry filled only
   when that draw buffer has one: an entry of no address and 0 bytes is no constant buffer.

   TEMPORARY STAND-IN, as for sceGnmDingDong above: the packets are read on the calling thread when
   the buffer is submitted, and the call returns at once. What the guest can observe of the GPU's
   progress becomes a step after the buffer's drawing calls (rt_gpu_step, runtime/renderer.h), which
   the renderer performs on its own thread once those calls have run, in submission order, as the
   PS4's GPU works beside the CPU; with the display off the steps are performed at once. The first
   boot to reach it (4 November 2026) found, besides the compute kinds, these (AMD's public PM4
   format):
     EVENT_WRITE_EOP              a step: the end-of-work value goes to its memory address;
     WAIT_REG_MEM                 a step: the renderer's thread waits for the memory to hold what
                                  the GPU waits for (two seconds at most);
     ACQUIRE_MEM, EVENT_WRITE     skipped: cache control and pipeline events, with no GPU caches
                                  (an EVENT_WRITE that also writes memory stops);
     SET_CONTEXT_REG, SET_UCONFIG_REG, NUM_INSTANCES, DRAW_INDEX_AUTO
                                  skipped: draw state and draws (the first frame's);
     INDEX_TYPE, INDEX_BASE, INDEX_BUFFER_SIZE, DRAW_INDEX_2, DRAW_INDEX_OFFSET_2
                                  skipped: an indexed draw's state (index size, buffer address and
                                  length) and the draw itself (found 6 November 2026); like the
                                  other draws they only read memory;
     DMA_DATA to CP_DMA_READ_TAGS skipped: memory read into the command processor's read tags,
                                  the destination not incremented (found 6 November 2026, a
                                  100-byte read). It changes no memory and no state a shader
                                  sees, so it only brings the source into the GPU's cache, and the
                                  stand-in has none. Any other DMA_DATA stops.
   The packets themselves draw nothing. Each buffer then goes to the renderer (runtime/renderer.h)
   with its steps, each at its packet's offset: it runs the drawing calls recorded before a step,
   then the step, then the calls after it, so a wait holds before the draws after it read memory. A constant command buffer
   that holds anything, and any other packet, stops by name. */
#define MAX_SUBMITTED_BUFFERS 64

/* A flip after a submission's last buffer: video out shows the buffer once the work is done. */
typedef struct {
    int32_t handle, buffer, mode;
    int64_t argument;
} flip_request;

static void flip_step(const rt_gpu_step *step)
{
    rt_video_out_flip(step->function, (int32_t)step->words[0], (int32_t)step->words[1], (int32_t)step->words[2],
                      (int64_t)step->words[3]);
}

static void collect_flip(const char *fn, step_list *steps, const flip_request *flip)
{
    const uint64_t words[6] = {(uint32_t)flip->handle, (uint32_t)flip->buffer, (uint32_t)flip->mode, (uint64_t)flip->argument, 0, 0};
    step_list *const saved = collecting;
    collecting = steps;
    packet_offset = RT_GPU_STEP_AFTER_ALL; /* after every call of the last buffer */
    collect(fn, flip_step, words);
    collecting = saved;
}

static int32_t submit(const char *fn, uint32_t count, void **draw_buffers, const uint32_t *draw_bytes, void **constant_buffers,
                      const uint32_t *constant_bytes, const flip_request *flip)
{
    if (!count || count > MAX_SUBMITTED_BUFFERS || !draw_buffers || !draw_bytes)
        rt_fatal(fn, "no command buffers, or more than the stand-in takes: the answer is not established");
    if (!constant_buffers != !constant_bytes) rt_fatal(fn, "constant command buffers without their sizes, or sizes without buffers");
    for (uint32_t k = 0; k < count; k++) {
        if (constant_buffers && (constant_buffers[k] || constant_bytes[k]))
            rt_fatal(fn, "a constant command buffer: the stand-in does not run them");
    }
    for (uint32_t k = 0; k < count; k++) {
        if (!draw_buffers[k] && draw_bytes[k]) rt_fatal(fn, "a command buffer without an address");
        step_list steps = {0};
        collecting = &steps; /* its completion writes and waits come after its work */
        complete_packets(fn, draw_buffers[k], draw_bytes[k] / sizeof(uint32_t), 0, 0);
        collecting = NULL;
        if (flip && k == count - 1) collect_flip(fn, &steps, flip);
        rt_renderer_submit(fn, draw_buffers[k], draw_bytes[k], steps.steps, steps.count); /* copied: runs beside the title */
        free(steps.steps);
    }
    return 0;
}

static int32_t submit_command_buffers(uint32_t count, void **draw_buffers, const uint32_t *draw_bytes,
                                      void **constant_buffers, const uint32_t *constant_bytes)
{
    return submit("sceGnmSubmitCommandBuffers", count, draw_buffers, draw_bytes, constant_buffers, constant_bytes, NULL);
}

RT_SYSLIB("libSceGnmDriver", sceGnmSubmitCommandBuffers, submit_command_buffers);

/* sceGnmSubmitAndFlipCommandBuffers(count, draw command buffers, sizes, constant command buffers,
   sizes, video out handle, buffer index, flip mode, flip argument): submits a frame's last command
   buffers and shows one of the display's buffers once they are done. OpenOrbis' GnmDriver.h only
   names it; the shape comes from the engine's frame end (0x02ab0ee2 and 0x02ab0dff), which passes
   one or two draw command buffers, no constant buffers, and the port, buffer, mode and argument of
   its frame (the flip loop calling at 0x02ad6ee9), then calls sceGnmSubmitDone. The stand-in submits the buffers as
   sceGnmSubmitCommandBuffers does, with the flip as a step after the last one's work: the flip (as
   sceVideoOutSubmitFlip does it, with its flip event) happens once the frame is drawn, and the call
   returns at once. */
static int32_t submit_and_flip(uint32_t count, void **draw_buffers, const uint32_t *draw_bytes, void **constant_buffers,
                               const uint32_t *constant_bytes, int32_t video_out_handle, int32_t buffer, int32_t mode,
                               int64_t argument)
{
    const flip_request flip = {video_out_handle, buffer, mode, argument};
    return submit("sceGnmSubmitAndFlipCommandBuffers", count, draw_buffers, draw_bytes, constant_buffers, constant_bytes, &flip);
}

/* sceGnmSubmitDone(void): tells the system the frame's submissions are finished, after each
   submit-and-flip (0x02ab0ee2, 0x02ab0dff), which ignore the answer. The submitted work goes on on
   the renderer's thread and needs nothing more; it answers 0. */
static int32_t submit_done(void)
{
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSubmitAndFlipCommandBuffers, submit_and_flip);
RT_SYSLIB("libSceGnmDriver", sceGnmSubmitDone, submit_done);
