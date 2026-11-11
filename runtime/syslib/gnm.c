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
#include "runtime/equeue.h"
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
   stop by name.

   sceGnmMapComputeQueueWithPriority(pipe, queue, ring, ring size, read pointer, priority): the
   same, with a priority; the engine's wrapper (0x01489ac0) treats the answer as above. The runtime
   runs no GPU queues, so the priority changes nothing (shadPS4's gnmdriver.cpp and the deadinside28
   and droogie bbhost ports also ignore it, facts only). sceGnmUnmapComputeQueue(id): the engine's
   teardown (0x0148c81c) passes a queue's id and ignores the answer; the runtime forgets the queue,
   answers 0, and hands its id out again to a later map. An id that names no mapped queue stops by
   name. */
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
    int slot = queue_count; /* the first unmapped slot, else a new one */
    for (int i = queue_count - 1; i >= 0; i--) {
        if (queues[i].ring && queues[i].pipe == pipe && queues[i].queue == queue)
            rt_fatal(fn, "a pipe and queue mapped twice: the answer is not established");
        if (!queues[i].ring) slot = i;
    }
    if (slot == MAX_COMPUTE_QUEUES) rt_fatal(fn, "more compute queues than the runtime records");
    queues[slot] = (compute_queue){pipe, queue, ring, ring_dwords, read_pointer};
    if (slot == queue_count) queue_count++;
    pthread_mutex_unlock(&queues_lock);
    return slot + 1; /* ids from 1: the engine treats 0 as failure */
}

static int32_t map_compute_queue_with_priority(uint32_t pipe, uint32_t queue, void *ring, uint32_t ring_dwords,
                                               volatile uint32_t *read_pointer, uint32_t priority)
{
    (void)priority;
    return map_compute_queue(pipe, queue, ring, ring_dwords, read_pointer);
}

static uint32_t completed_offset[MAX_COMPUTE_QUEUES]; /* where each ring's unread work starts, in dwords */

static int32_t unmap_compute_queue(uint32_t queue_id)
{
    pthread_mutex_lock(&queues_lock);
    if (queue_id == 0 || queue_id > (uint32_t)queue_count || !queues[queue_id - 1].ring)
        rt_fatal("sceGnmUnmapComputeQueue", "a queue id that names no mapped queue");
    queues[queue_id - 1].ring = NULL; /* free: a later map takes the slot */
    completed_offset[queue_id - 1] = 0;
    pthread_mutex_unlock(&queues_lock);
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmMapComputeQueue, map_compute_queue);
RT_SYSLIB("libSceGnmDriver", sceGnmMapComputeQueueWithPriority, map_compute_queue_with_priority);
RT_SYSLIB("libSceGnmDriver", sceGnmUnmapComputeQueue, unmap_compute_queue);

/* sceGnmDingDong(queue id, write offset in dwords): tells the GPU that the ring of a compute queue
   holds new work up to the write offset. OpenOrbis' GnmDriver.h only names it; the shape comes from
   the engine's compute submit (0x0148c987), which copies one 4-dword INDIRECT_BUFFER packet per
   command buffer into the ring (its packet writer), wraps to the ring's start when
   the end is full, then rings the doorbell with the queue id from sceGnmMapComputeQueue and the new
   write offset. Before writing, it checks the free space against the read pointer the GPU advances.

   TEMPORARY STAND-IN (maintainer decision, 4 November 2026): the engine's own renderer is to
   replace this layer (issue #39). Until then the doorbell reads each indirect buffer on the calling
   thread, as sceGnmSubmitCommandBuffers below reads a graphics buffer, and hands it to the renderer
   like a submitted one: its dispatches run (the compute shaders' recorded calls, on the renderer's
   one queue, in submission order with the graphics work), and its completions are steps after its
   work. A boot on 4 November found four kinds in the engine's compute buffers (packet types from
   AMD's public PM4 packet format):
     NOP and SET_SH_REG      skipped: padding and shader registers (the renderer's record has them);
     DISPATCH_DIRECT         run by the renderer, with the buffer;
     RELEASE_MEM             a step: the "work done" value goes to its memory address once the
                             buffer's work has run (at once with the display off).
   Then the read pointer moves to the write offset, so the ring never fills. Any other packet, or a
   completion this stand-in does not perform, stops by name. The character creation screen needs
   the queues' work: they fill the colour volume, images and light data its passes read. */
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
#define INTERRUPT_SELECT_SHIFT 24      /* EVENT_WRITE_EOP and RELEASE_MEM: the interrupt the GPU raises */
#define EOP_INTERRUPT_SELECT_MASK 3u   /* two bits in EVENT_WRITE_EOP */
#define RELEASE_MEM_INTERRUPT_SELECT_MASK 7u /* three in RELEASE_MEM */

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

/* Graphics events: sceGnmAddEqEvent(queue, id, user data) and sceGnmDeleteEqEvent(queue, id).

   The GPU raises an interrupt when an end-of-work packet asks for one, and the system turns it into
   an event on every kernel event queue the title registered for it. Facts from shadPS4's
   gnmdriver.cpp and liverpool.cpp (facts only): the event's filter is the graphics core (-14), its
   ident the id the title registered, and its data that id too, which is what sceGnmGetEqEventType
   reads back; the ids are interrupt numbers, 0x40 for the graphics queue's end of pipe
   (EVENT_WRITE_EOP) and the compute pipe's number (0 to 6) for a compute queue's RELEASE_MEM.
   AMD's public PM4 format puts the interrupt select in bits 24 and up of the packet's data control
   word: 0 none, 1 interrupt only, 2 interrupt once the write is confirmed (and, for RELEASE_MEM, 3,
   which shadPS4 also takes as an interrupt).

   The runtime raises the events in the GPU's completion step (perform_completion below), which the
   renderer performs on its own thread once the work before the packet has run (runtime/renderer.h),
   or at once with the display off: so the event comes after the work, never before it. A packet
   in a graphics submission raises 0x40; one in a compute queue's buffer raises the queue's pipe.
   AddEqEvent registers (graphics core, id) on the queue (again: new user data) and answers 0;
   DeleteEqEvent removes it. No queue answers EBADF (0x80020009), as in shadPS4. The executable
   imports both, but no instruction or relocated pointer in it reaches its wrappers
   (0x01489a40, 0x01489a50): the stock game never asks for these events. Deleting an event that is
   not registered (FreeBSD's kevent answers ENOENT; shadPS4 ignores it) stops by name. */
#define SCE_KERNEL_ERROR_EBADF ((int32_t)0x80020009u)
enum { GRAPHICS_END_OF_PIPE_INTERRUPT = 0x40, COMPUTE_PIPES = 7, MAX_GRAPHICS_EVENTS = 32 };
enum { INTERRUPT_NONE = 0, INTERRUPT_ONLY = 1, INTERRUPT_AFTER_WRITE = 2, INTERRUPT_RELEASE_MEM_OTHER = 3 };

typedef struct {
    rt_equeue *queue;
    uint64_t id;
} graphics_event;

static pthread_mutex_t events_lock = PTHREAD_MUTEX_INITIALIZER;
static graphics_event graphics_events[MAX_GRAPHICS_EVENTS];
static int graphics_event_count;

static int32_t add_eq_event(rt_equeue *queue, uint64_t id, void *udata)
{
    static const char fn[] = "sceGnmAddEqEvent";
    if (!queue) return SCE_KERNEL_ERROR_EBADF;
    pthread_mutex_lock(&events_lock);
    int i = 0;
    while (i < graphics_event_count && !(graphics_events[i].queue == queue && graphics_events[i].id == id)) i++;
    if (i == graphics_event_count) {
        if (graphics_event_count == MAX_GRAPHICS_EVENTS) rt_fatal(fn, "more graphics events than the runtime records");
        graphics_events[graphics_event_count++] = (graphics_event){queue, id};
    }
    if (rt_equeue_add(queue, RT_EVFILT_GRAPHICS_CORE, id, udata)) rt_fatal(fn, "the queue is full");
    pthread_mutex_unlock(&events_lock);
    return 0;
}

static int32_t delete_eq_event(rt_equeue *queue, uint64_t id)
{
    static const char fn[] = "sceGnmDeleteEqEvent";
    if (!queue) return SCE_KERNEL_ERROR_EBADF;
    pthread_mutex_lock(&events_lock);
    int i = 0;
    while (i < graphics_event_count && !(graphics_events[i].queue == queue && graphics_events[i].id == id)) i++;
    if (i == graphics_event_count) rt_fatal(fn, "an event that is not registered: the error value is not established");
    graphics_events[i] = graphics_events[--graphics_event_count];
    rt_equeue_remove(queue, RT_EVFILT_GRAPHICS_CORE, id);
    pthread_mutex_unlock(&events_lock);
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmAddEqEvent, add_eq_event);
RT_SYSLIB("libSceGnmDriver", sceGnmDeleteEqEvent, delete_eq_event);

/* Triggers every registered event for an interrupt, with the id as its data. */
static void raise_interrupt(uint64_t id)
{
    pthread_mutex_lock(&events_lock);
    for (int i = 0; i < graphics_event_count; i++)
        if (graphics_events[i].id == id) rt_equeue_trigger(graphics_events[i].queue, RT_EVFILT_GRAPHICS_CORE, id, (int64_t)id);
    pthread_mutex_unlock(&events_lock);
}

/* Which interrupt a completion packet raises: the graphics queue's end of pipe, or, while a
   compute queue's ring is completed (sceGnmDingDong), that queue's pipe. */
static _Thread_local int32_t completing_pipe = -1;

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
    step->perform = perform, step->function = fn, step->offset = packet_offset, step->flags = 0, step->holds = NULL;
    memcpy(step->words, words, sizeof step->words);
}

/* What a completion packet asks the GPU to raise once its write is done: an interrupt select, and
   the interrupt's number. Packed into one step word. */
typedef struct {
    uint32_t select, number;
} completion_interrupt;

static void perform_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high, uint32_t data_low,
                               uint32_t data_high, completion_interrupt interrupt);

enum { INTERRUPT_NUMBER_SHIFT = 32 };

static void completion_step(const rt_gpu_step *step)
{
    const uint64_t *w = step->words;
    const completion_interrupt interrupt = {(uint32_t)w[5], (uint32_t)(w[5] >> INTERRUPT_NUMBER_SHIFT)};
    perform_completion(step->function, (uint32_t)w[0], (uint32_t)w[1], (uint32_t)w[2], (uint32_t)w[3], (uint32_t)w[4], interrupt);
}

/* A "work done" write: the packet's own 32- or 64-bit value to a memory address (the address's
   high bits are the low 16 of address_high), after the work before it (collected while a
   submission is read). Other kinds of value (a GPU clock, for one) stop. */
static void write_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high,
                             uint32_t data_low, uint32_t data_high, uint32_t interrupt_select)
{
    const completion_interrupt interrupt = {interrupt_select,
                                            completing_pipe < 0 ? GRAPHICS_END_OF_PIPE_INTERRUPT : (uint32_t)completing_pipe};
    if (collecting) {
        const uint64_t words[6] = {data_size, address_low, address_high, data_low, data_high,
                                   interrupt.select | (uint64_t)interrupt.number << INTERRUPT_NUMBER_SHIFT};
        collect(fn, completion_step, words);
        collecting->steps[collecting->count - 1].flags = RT_GPU_STEP_DEFERRABLE; /* a write after work: may come later, in order */
        return;
    }
    perform_completion(fn, data_size, address_low, address_high, data_low, data_high, interrupt);
}

static void write_completion_value(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high,
                                   uint32_t data_low, uint32_t data_high);

/* The write, then the interrupt it asks for (graphics events, above). */
static void perform_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high, uint32_t data_low,
                               uint32_t data_high, completion_interrupt interrupt)
{
    write_completion_value(fn, data_size, address_low, address_high, data_low, data_high);
    if (interrupt.select == INTERRUPT_NONE) return;
    if (interrupt.select != INTERRUPT_ONLY && interrupt.select != INTERRUPT_AFTER_WRITE && interrupt.select != INTERRUPT_RELEASE_MEM_OTHER)
        rt_fatal(fn, "an interrupt select the GPU's packet format does not define");
    if (interrupt.number >= COMPUTE_PIPES && interrupt.number != GRAPHICS_END_OF_PIPE_INTERRUPT)
        rt_fatal(fn, "an interrupt from a compute pipe above 6: its number is not established");
    raise_interrupt(interrupt.number);
}

static void write_completion_value(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high,
                                   uint32_t data_low, uint32_t data_high)
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
                     packet->data_low, packet->data_high,
                     (packet->data_control >> INTERRUPT_SELECT_SHIFT) & RELEASE_MEM_INTERRUPT_SELECT_MASK);
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

static int wait_compare(const char *fn, const wait_reg_mem_packet *packet, uint32_t value);
static void completion_step(const rt_gpu_step *step);

/* rt_gpu_step's holds for a collected WAIT_REG_MEM: with pending NULL, whether it holds now; otherwise
   whether `pending`, an earlier completion write not yet performed, writes the address it waits on
   with a value that makes it hold (its low 32 bits, which the wait reads). */
static int wait_step_holds(const rt_gpu_step *step, const rt_gpu_step *pending)
{
    const wait_reg_mem_packet packet = {(uint32_t)step->words[0], (uint32_t)step->words[1], (uint32_t)step->words[2],
                                        (uint32_t)step->words[3], (uint32_t)step->words[4], (uint32_t)step->words[5]};
    if (!pending) return wait_holds(step->function, &packet);
    if (pending->perform != completion_step) return 0;
    const uint32_t size = (uint32_t)pending->words[0];
    if (size != RELEASE_MEM_DATA_32_BITS && size != RELEASE_MEM_DATA_64_BITS) return 0;
    const uint64_t written = (uint32_t)pending->words[1] | (uint64_t)((uint32_t)pending->words[2] & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32;
    const uint64_t waited = packet.address_low | (uint64_t)(packet.address_high & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32;
    return written == waited && wait_compare(step->function, &packet, (uint32_t)pending->words[3] & packet.mask);
}

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
        collecting->steps[collecting->count - 1].holds = wait_step_holds;
        return;
    }
    if (!wait_holds(fn, packet)) rt_fatal(fn, "a GPU wait on memory that does not yet hold what it waits for");
}

/* Whether a WAIT_REG_MEM's compare holds now. */
static int wait_holds(const char *fn, const wait_reg_mem_packet *packet)
{
    const volatile uint32_t *address = (const volatile uint32_t *)(uintptr_t)(packet->address_low |
                                       (uint64_t)(packet->address_high & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32);
    return wait_compare(fn, packet, __atomic_load_n(address, __ATOMIC_ACQUIRE) & packet->mask);
}

/* Whether a WAIT_REG_MEM's compare holds for a (masked) value. */
static int wait_compare(const char *fn, const wait_reg_mem_packet *packet, uint32_t value)
{
    const uint32_t reference = packet->reference;
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

/* Completes an indirect buffer's packets. One a compute ring points to is a submission of its own:
   its completions are collected as steps after its work, and the renderer runs it (sceGnmDingDong). */
static void follow_indirect_buffer(const char *fn, const uint32_t *packet, int depth, int from_ring)
{
    if (depth == MAX_INDIRECT_BUFFER_DEPTH) rt_fatal(fn, "indirect buffers nested deeper than the stand-in follows");
    const uint32_t *buffer = (const uint32_t *)(uintptr_t)(packet[1] |
                             (uint64_t)(packet[2] & INDIRECT_BUFFER_ADDRESS_HIGH_MASK) << 32);
    const uint32_t dwords = packet[3] & INDIRECT_BUFFER_SIZE_MASK;
    if (!buffer && dwords) rt_fatal(fn, "an INDIRECT_BUFFER without an address");
    if (!from_ring) {
        complete_packets(fn, buffer, dwords, depth + 1, 0);
        return;
    }
    step_list steps = {0};
    step_list *const saved = collecting;
    const uint32_t saved_offset = packet_offset;
    collecting = &steps; /* its steps' offsets count in this buffer, as a submitted buffer's do */
    complete_packets(fn, buffer, dwords, 0, 0);
    collecting = saved, packet_offset = saved_offset;
    rt_renderer_submit(fn, buffer, dwords * (uint32_t)sizeof(uint32_t), steps.steps, steps.count);
    free(steps.steps);
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
            const uint32_t interrupt_select = (packet[3] >> INTERRUPT_SELECT_SHIFT) & EOP_INTERRUPT_SELECT_MASK;
            if (interrupt_select == INTERRUPT_RELEASE_MEM_OTHER) rt_fatal(fn, "an EVENT_WRITE_EOP interrupt select of 3, which the packet format does not define");
            write_completion(fn, packet[3] >> EVENT_WRITE_EOP_DATA_SHIFT, packet[2], packet[3], packet[4], packet[5], interrupt_select);
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

static void ding_dong(uint32_t queue_id, uint32_t write_offset)
{
    static const char fn[] = "sceGnmDingDong";
    pthread_mutex_lock(&queues_lock);
    if (queue_id == 0 || queue_id > (uint32_t)queue_count || !queues[queue_id - 1].ring)
        rt_fatal(fn, "a queue id sceGnmMapComputeQueue did not answer");
    const compute_queue *queue = &queues[queue_id - 1];
    uint32_t *const done = &completed_offset[queue_id - 1];
    if (write_offset > queue->ring_dwords) rt_fatal(fn, "a write offset past the end of the ring");
    const uint32_t *ring = queue->ring;
    completing_pipe = (int32_t)queue->pipe; /* its completions raise the pipe's interrupt */
    if (write_offset < *done) { /* the engine filled the ring's end and wrapped to its start */
        complete_packets(fn, ring + *done, queue->ring_dwords - *done, 0, 1);
        *done = 0;
    }
    complete_packets(fn, ring + *done, write_offset - *done, 0, 1);
    completing_pipe = -1;
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

/* A flip after a submission's last buffer: video out shows the buffer once the work is done. */
typedef struct {
    int32_t handle, buffer, mode;
    int64_t argument;
} flip_request;

/* Performed once the work before it is done (on the renderer's thread when it draws): the flip is
   queued (rt_video_out_queue_flip) and waits for its vertical blank without holding up the work
   after it. */
static void flip_step(const rt_gpu_step *step)
{
    rt_video_out_queue_flip(step->function, (int32_t)step->words[0], (int32_t)step->words[1], (int32_t)step->words[2],
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
    /* any number of buffers: shadPS4 checks no count, and the character creation screen submits 70
       in one call; none, or no arrays, has no established answer */
    if (!count || !draw_buffers || !draw_bytes) rt_fatal(fn, "no command buffers: the answer is not established");
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

/* sceGnmRequestFlipAndSubmitDone(command buffer, its size in bytes, video out handle, buffer
   index, flip mode, flip argument): a flip with no drawing work before it, then the frame's
   submissions are done. The shape is shadPS4's (gnmdriver.cpp, which also has the workload variant;
   facts only): the system writes its flip request into the command buffer the title gives (at
   least 0x100 bytes, else 0x80d11001, not enough resources) and submits it. droogie's host takes
   (handle, buffer, mode, argument) instead; the GNM header shadPS4 follows and its workload variant
   agree on the six-argument form, so the runtime takes that. The executable imports it, but no
   instruction or relocated pointer reaches its wrapper (0x01489b70). The runtime writes one NOP
   packet over the first 0x100 bytes (its flip is a step, not a packet) and submits that buffer
   with the flip as the step after it, as sceGnmSubmitAndFlipCommandBuffers does. No buffer stops
   by name. */
#define GNM_ERROR_SUBMISSION_NOT_ENOUGH_RESOURCES ((int32_t)0x80d11001u)
enum { FLIP_REQUEST_BYTES = 0x100 };

static int32_t request_flip_and_submit_done(uint32_t *command_buffer, uint32_t bytes, int32_t video_out_handle, int32_t buffer,
                                            int32_t mode, int64_t argument)
{
    static const char fn[] = "sceGnmRequestFlipAndSubmitDone";
    if (bytes < FLIP_REQUEST_BYTES) return GNM_ERROR_SUBMISSION_NOT_ENOUGH_RESOURCES;
    if (!command_buffer) rt_fatal(fn, "no command buffer: the error value is not established");
    fill_with_nop(fn, command_buffer, FLIP_REQUEST_BYTES / sizeof(uint32_t));
    void *buffers[1] = {command_buffer};
    const uint32_t sizes[1] = {FLIP_REQUEST_BYTES};
    const flip_request flip = {video_out_handle, buffer, mode, argument};
    return submit(fn, 1, buffers, sizes, NULL, NULL, &flip);
}

RT_SYSLIB("libSceGnmDriver", sceGnmSubmitAndFlipCommandBuffers, submit_and_flip);
RT_SYSLIB("libSceGnmDriver", sceGnmRequestFlipAndSubmitDone, request_flip_and_submit_done);
RT_SYSLIB("libSceGnmDriver", sceGnmSubmitDone, submit_done);

/* Queries and debug calls with no GPU work behind them.

   sceGnmAreSubmitsAllowed(): whether command buffers may be submitted now. The engine's wrapper
   (0x01489b60) reads it as "allowed when nonzero". The console refuses submissions only while the
   system holds the GPU (shadPS4's gnmdriver.cpp answers "no submission lock held"; the droogie
   bbhost port answers 1, facts only); the runtime never holds it, so the answer is 1.

   sceGnmFlushGarlic(): makes the CPU's writes to GPU memory visible to the GPU. The engine calls it
   before freeing GPU memory and after patching command buffers (0x0073e927, 0x0147861c and others).
   The runtime's GPU reads the same memory as the CPU, so a memory fence is all it takes (shadPS4 and
   bbhost do nothing).

   sceGnmGetEqEventType(event): the type of a graphics event an event queue returned, which is the
   event's data field (shadPS4's gnmdriver.cpp and the deadinside28 port agree, facts only; the
   engine's wrapper 0x01489a60 keeps its low byte). The event is FreeBSD's struct kevent (OpenOrbis
   _types/kernel.h): data is the 64-bit field at offset 16. No event stops by name.

   sceGnmSetGsRingSizes(esgs, gsvs): the sizes of the geometry shader rings. shadPS4 (facts only)
   accepts sizes from 4 MiB to 8 MiB in whole MiB and answers 0x08000000 otherwise; the runtime runs
   no geometry shader stage, so it checks the same and answers 0.

   The mip statistics report (sceGnmSetupMipStatsReport(buffer, size, intervals, reports, force),
   sceGnmRequestMipStatsReportAndReset(), sceGnmDisableMipStatsReport()) is a profiling feature.
   Following shadPS4: setup answers 0x80000001 for no buffer or one not 64-byte aligned (shadPS4
   also refuses addresses past 40 bits, the console GPU's range; the runtime's memory is the host's,
   so that part is left out), 0x80000002 for a size under 0x1040 or not 0x40 past a 2 KiB multiple, and otherwise zeroes
   the buffer, writes 0x40 to its first word and answers 0; the other two answer 0.

   The Razor GPU capture tools exist only on development kits. On a retail console (shadPS4's
   answers, "not available in retail firmware"): sceRazorIsLoaded and sceGnmDriverCaptureInProgress
   answer 0, sceGnmDriverTriggerCapture answers 0x80d15001 (Razor not loaded), and the four
   sceRazorCapture calls answer 0x80d1500f (capture failed). */
static int32_t are_submits_allowed(void)
{
    return 1;
}

static void flush_garlic(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

enum { KEVENT_DATA_OFFSET = 16 };

static int32_t get_eq_event_type(const uint8_t *event)
{
    if (!event) rt_fatal("sceGnmGetEqEventType", "no event: the answer is not established");
    int64_t data;
    memcpy(&data, event + KEVENT_DATA_OFFSET, sizeof data);
    return (int32_t)data;
}

enum { GS_RING_MIN = 0x400000, GS_RING_MAX = 0x800000, GS_RING_ALIGNMENT = 0x100000 };
#define GS_RING_SIZE_INVALID ((int32_t)0x08000000)

static int gs_ring_size_ok(uint32_t size)
{
    return size >= GS_RING_MIN && size <= GS_RING_MAX && !(size & (GS_RING_ALIGNMENT - 1));
}

static int32_t set_gs_ring_sizes(uint32_t esgs, uint32_t gsvs)
{
    return gs_ring_size_ok(esgs) && gs_ring_size_ok(gsvs) ? 0 : GS_RING_SIZE_INVALID;
}

#define MIP_STATS_BAD_BUFFER ((int32_t)0x80000001u)
#define MIP_STATS_BAD_SIZE ((int32_t)0x80000002u)
enum { MIP_STATS_MIN_SIZE = 0x1040, MIP_STATS_SIZE_MASK = 0x7ff, MIP_STATS_SIZE_REMAINDER = 0x40, MIP_STATS_HEADER = 0x40 };
#define MIP_STATS_ALIGNMENT_MASK 0x3fu /* 64-byte aligned */

static int32_t setup_mip_stats_report(void *buffer, uint32_t size, uint8_t intervals, uint8_t reports, uint32_t force)
{
    (void)intervals, (void)reports, (void)force;
    if (!buffer || ((uintptr_t)buffer & MIP_STATS_ALIGNMENT_MASK)) return MIP_STATS_BAD_BUFFER;
    if (size < MIP_STATS_MIN_SIZE || (size & MIP_STATS_SIZE_MASK) != MIP_STATS_SIZE_REMAINDER) return MIP_STATS_BAD_SIZE;
    memset(buffer, 0, size);
    const uint32_t header = MIP_STATS_HEADER;
    memcpy(buffer, &header, sizeof header);
    return 0;
}

static int32_t answer_zero(void)
{
    return 0;
}

#define CAPTURE_RAZOR_NOT_LOADED ((int32_t)0x80d15001u)
#define CAPTURE_FAILED_INTERNAL ((int32_t)0x80d1500fu)

static int32_t trigger_capture(void)
{
    return CAPTURE_RAZOR_NOT_LOADED;
}

static int32_t razor_capture(void)
{
    return CAPTURE_FAILED_INTERNAL;
}

/* Two more queries the executable imports but never reaches (no instruction or relocated pointer
   reaches their wrappers, 0x01489a70 and 0x01489b40):

   sceGnmGetEqTimeStamp: shadPS4 takes no arguments and answers 0; droogie's host takes (queue, id,
   uint64_t *) and also answers 0, writing a time through the pointer. They agree on the answer and
   not on the arguments, so the runtime answers 0 and writes nothing (writing through an argument
   the call may not have would be worse).

   sceGnmDebugHardwareStatus: a development query. shadPS4 answers 0 with no arguments; droogie's
   host zeroes 64 bytes through a pointer and answers 0. As above: 0, nothing written.

   sceGnmLogicalCuIndexToPhysicalCuIndex(shader engine, logical CU index) and
   sceGnmLogicalCuMaskToPhysicalCuMask(shader engine, logical CU mask): the GPU's compute units as
   the title numbers them, and as the hardware does. The engine's performance counter set-up calls
   them (0x01482804 and eleven more for the index, 0x014818fa and 0x01481eb4 for the mask), passing
   the shader engine first and reading back a byte and a 16-bit mask, only when sceGnmIsUserPaEnabled
   answers on, which it never does. The runtime has no physical units to map to, so the mapping is
   the identity: the index or mask comes back unchanged (maintainer's ruling of 2026-11-10;
   shadPS4 answers the mask unchanged too, and droogie's host the first argument). */
static uint32_t logical_cu_index_to_physical(uint32_t shader_engine, uint32_t logical_index)
{
    (void)shader_engine;
    return logical_index;
}

static uint32_t logical_cu_mask_to_physical(uint32_t shader_engine, uint32_t logical_mask)
{
    (void)shader_engine;
    return logical_mask;
}

RT_SYSLIB("libSceGnmDriver", sceGnmGetEqTimeStamp, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceGnmDebugHardwareStatus, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceGnmLogicalCuIndexToPhysicalCuIndex, logical_cu_index_to_physical);
RT_SYSLIB("libSceGnmDriver", sceGnmLogicalCuMaskToPhysicalCuMask, logical_cu_mask_to_physical);
RT_SYSLIB("libSceGnmDriver", sceGnmAreSubmitsAllowed, are_submits_allowed);
RT_SYSLIB("libSceGnmDriver", sceGnmFlushGarlic, flush_garlic);
RT_SYSLIB("libSceGnmDriver", sceGnmGetEqEventType, get_eq_event_type);
RT_SYSLIB("libSceGnmDriver", sceGnmSetGsRingSizes, set_gs_ring_sizes);
RT_SYSLIB("libSceGnmDriver", sceGnmSetupMipStatsReport, setup_mip_stats_report);
RT_SYSLIB("libSceGnmDriver", sceGnmRequestMipStatsReportAndReset, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceGnmDisableMipStatsReport, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceRazorIsLoaded, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceGnmDriverCaptureInProgress, answer_zero);
RT_SYSLIB("libSceGnmDriver", sceGnmDriverTriggerCapture, trigger_capture);
RT_SYSLIB("libSceGnmDriver", sceRazorCaptureImmediate, razor_capture);
RT_SYSLIB("libSceGnmDriver", sceRazorCaptureSinceLastFlip, razor_capture);
RT_SYSLIB("libSceGnmDriver", sceRazorCaptureCommandBuffersOnlyImmediate, razor_capture);
RT_SYSLIB("libSceGnmDriver", sceRazorCaptureCommandBuffersOnlySinceLastFlip, razor_capture);
