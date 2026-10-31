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
