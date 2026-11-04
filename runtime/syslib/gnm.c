/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: GNM (GPU commands), libSceGnmDriver. Graphics are rebuilt at the engine level
   (issue #39), so these calls answer what the engine expects back; the GPU is not emulated.

   sceGnmGetTheTessellationFactorRingBufferBaseAddress: the OpenOrbis homebrew SDK's GnmDriver.h
   only names it. The executable's only call (0x02ad3fb4) takes a 64-bit address and maps 128 KiB
   of direct memory there (sceKernelMapDirectMemory with that address as a hint, protection 0x33,
   alignment 0x10000) for the GPU's tessellation factors. The PS4 answers a fixed address of its
   own; the runtime answers one it reserves for this, below 2^40 and clear of the executable,
   modules and thread objects. It is the runtime's choice, not the PS4's value: the engine only
   maps it and hands it back to the GPU calls. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

#define TESSELLATION_FACTOR_RING_BASE 0xfe00000000ull

static uint64_t tessellation_factor_ring_base(void)
{
    return TESSELLATION_FACTOR_RING_BASE;
}

RT_SYSLIB("libSceGnmDriver", sceGnmGetTheTessellationFactorRingBufferBaseAddress, tessellation_factor_ring_base);

/* sceGnmValidateCommandBuffers(count, buffers, sizes, constant buffers, constant sizes): asks the GPU
   driver's debug validation layer to check command buffers. Paleblood has no such layer: the GPU is
   not emulated, and the command buffers are not what it draws from. The executable's start-up probe
   (0x02abb8e0, called from the graphics set-up at 0x0256b0f0) validates a tiny 16-byte buffer and
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
   and return how many dwords they wrote. The engine's wrappers (0x01489c50, five callers including
   the graphics set-up at 0x02ab1130; 0x01472870, seven callers) reserve 256 dwords, pass the cursor
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
   (0x0148a870, four callers including the set-up at 0x02ab4230) reserves 25 dwords, passes 25 and
   advances by 25 dwords only when the answer is 0; nothing else reads the answer. */
static int32_t set_cs_shader(uint32_t *command_buffer, uint32_t dwords, const void *shader)
{
    (void)shader;
    fill_with_nop("sceGnmSetCsShader", command_buffer, dwords);
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSetCsShader, set_cs_shader);

/* sceGnmIsUserPaEnabled(): whether the GPU's user performance-analysis mode is on. The engine wraps
   it as "on when nonzero" (0x01489c40) and asks from many draw-command builders (0x01474b50 and
   others), which then add the extra packets that mode needs. Paleblood has no such GPU mode, so the
   answer is 0, off; that a retail console also answers off is an inference (it is a development
   feature), not an observation. */
static int32_t is_user_pa_enabled(void)
{
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmIsUserPaEnabled, is_user_pa_enabled);

/* sceGnmMapComputeQueue(pipe, queue, ring, ring size in dwords, read pointer): OpenOrbis' GnmDriver.h
   only names it, so the shape comes from the engine's wrapper (0x01489a80, called from its compute
   queue setup at 0x0148c7b0). The wrapper passes the pipe and queue from the queue object, the ring
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
