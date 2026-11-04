/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GNM system library: graphics are rebuilt at the engine level (issue #39).
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/video_out.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TESSELLATION_FACTOR_RING_BASE 0xfe00000000ull

static uint64_t tessellation_factor_ring_base(void)
{
    return TESSELLATION_FACTOR_RING_BASE;
}

RT_SYSLIB("libSceGnmDriver", sceGnmGetTheTessellationFactorRingBufferBaseAddress, tessellation_factor_ring_base);

/* Command validation is unavailable; no command-buffer memory is accessed.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
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

/* The engine-level renderer owns graphics state, so no initialization packets are emitted.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
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

/* Reserve a well-formed PM4 NOP in place of a compute shader bind.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
static int32_t set_cs_shader(uint32_t *command_buffer, uint32_t dwords, const void *shader)
{
    (void)shader;
    fill_with_nop("sceGnmSetCsShader", command_buffer, dwords);
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSetCsShader, set_cs_shader);

/* Shader binds and debug markers preserve their reservations with PM4 NOP packets.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
static int32_t write_nothing_for_the_gpu(uint32_t *command_buffer, uint32_t dwords)
{
    fill_with_nop("a GNM command-buffer writer", command_buffer, dwords);
    return 0;
}
RT_SYSLIB("libSceGnmDriver", sceGnmSetEsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetGsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetHsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetLsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetPsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmSetVsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateGsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateHsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdatePsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmUpdateVsShader, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPushMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPopMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertSetMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertPushColorMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertSetColorMarker, write_nothing_for_the_gpu);
RT_SYSLIB("libSceGnmDriver", sceGnmInsertWaitFlipDone, write_nothing_for_the_gpu);

/* User performance analysis is unavailable; answer off.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
static int32_t is_user_pa_enabled(void)
{
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmIsUserPaEnabled, is_user_pa_enabled);

/* Record the ring and its read pointer for the supported CPU-visible completion path.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
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

/* Complete the supported compute-ring packet effects immediately on the calling thread.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
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

/* A "work done" write: the packet's own 32- or 64-bit value to a memory address (the address's
   high bits are the low 16 of address_high). Other kinds of value (a GPU clock, for one) stop. */
static void write_completion(const char *fn, uint32_t data_size, uint32_t address_low, uint32_t address_high,
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
                     packet->data_low, packet->data_high);
}

/* WAIT_REG_MEM makes the GPU wait until a value in memory compares as asked with a reference
   (under a mask). Completing work at once leaves no later moment for the CPU to satisfy it, so the
   stand-in checks that it already holds; one that does not, or one on a register, stops by name. */
typedef struct {
    uint32_t control; /* compare function in bits 0-2, memory (not a register) in bit 4 */
    uint32_t address_low, address_high;
    uint32_t reference, mask, poll_interval;
} wait_reg_mem_packet;

static void check_wait(const char *fn, const wait_reg_mem_packet *packet)
{
    if (!(packet->control & WAIT_ON_MEMORY)) rt_fatal(fn, "a GPU wait on a register: the stand-in has none");
    const volatile uint32_t *address = (const volatile uint32_t *)(uintptr_t)(packet->address_low |
                                       (uint64_t)(packet->address_high & RELEASE_MEM_ADDRESS_HIGH_MASK) << 32);
    if (!address) rt_fatal(fn, "a GPU wait without an address");
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
    if (!holds) rt_fatal(fn, "a GPU wait on memory that does not yet hold what it waits for");
}

/* Stops naming the packet's opcode: the next packet kind to understand. */
static _Noreturn void unknown_packet(const char *fn, uint32_t opcode, uint32_t length)
{
    static char message[96];
    snprintf(message, sizeof message, "a PM4 packet the stand-in does not know (opcode 0x%02x, %u dwords)", opcode, length);
    rt_fatal(fn, message);
}

static void complete_packets(const char *fn, const uint32_t *packets, uint32_t dwords, int depth, int is_ring);

static void follow_indirect_buffer(const char *fn, const uint32_t *packet, int depth)
{
    if (depth == MAX_INDIRECT_BUFFER_DEPTH) rt_fatal(fn, "indirect buffers nested deeper than the stand-in follows");
    const uint32_t *buffer = (const uint32_t *)(uintptr_t)(packet[1] |
                             (uint64_t)(packet[2] & INDIRECT_BUFFER_ADDRESS_HIGH_MASK) << 32);
    const uint32_t dwords = packet[3] & INDIRECT_BUFFER_SIZE_MASK;
    if (!buffer && dwords) rt_fatal(fn, "an INDIRECT_BUFFER without an address");
    complete_packets(fn, buffer, dwords, depth + 1, 0);
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
        if (opcode == PM4_INDIRECT_BUFFER && length == INDIRECT_BUFFER_DWORDS) {
            follow_indirect_buffer(fn, packet, depth);
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
        } else if (opcode == PM4_EVENT_WRITE && length != EVENT_WRITE_WITHOUT_MEMORY_DWORDS) {
            unknown_packet(fn, opcode, length); /* an event that also writes memory */
        } else if (opcode != PM4_NOP && opcode != PM4_SET_SH_REG && opcode != PM4_DISPATCH_DIRECT &&
                   opcode != PM4_ACQUIRE_MEM && opcode != PM4_EVENT_WRITE && opcode != PM4_SET_CONTEXT_REG &&
                   opcode != PM4_SET_UCONFIG_REG && opcode != PM4_NUM_INSTANCES && opcode != PM4_DRAW_INDEX_AUTO) {
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

/* Complete supported graphics packet effects immediately; draws are skipped.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
#define MAX_SUBMITTED_BUFFERS 64

static int32_t submit_command_buffers(uint32_t count, void **draw_buffers, const uint32_t *draw_bytes,
                                      void **constant_buffers, const uint32_t *constant_bytes)
{
    static const char fn[] = "sceGnmSubmitCommandBuffers";
    if (!count || count > MAX_SUBMITTED_BUFFERS || !draw_buffers || !draw_bytes)
        rt_fatal(fn, "no command buffers, or more than the stand-in takes: the answer is not established");
    if (constant_buffers || constant_bytes) rt_fatal(fn, "constant command buffers: the stand-in does not run them");
    for (uint32_t k = 0; k < count; k++) {
        if (!draw_buffers[k] && draw_bytes[k]) rt_fatal(fn, "a command buffer without an address");
        complete_packets(fn, draw_buffers[k], draw_bytes[k] / sizeof(uint32_t), 0, 0);
    }
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSubmitCommandBuffers, submit_command_buffers);

/* Complete the submitted buffers and flip the registered video-out buffer.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
static int32_t submit_and_flip(uint32_t count, void **draw_buffers, const uint32_t *draw_bytes, void **constant_buffers,
                               const uint32_t *constant_bytes, int32_t video_out_handle, int32_t buffer, int32_t mode,
                               int64_t argument)
{
    submit_command_buffers(count, draw_buffers, draw_bytes, constant_buffers, constant_bytes);
    return rt_video_out_flip("sceGnmSubmitAndFlipCommandBuffers", video_out_handle, buffer, mode, argument);
}

/* Submissions already complete on the calling thread, so frame completion answers success.
   Executable-call evidence and limitations: docs/runtime/gnm-provenance.md. */
static int32_t submit_done(void)
{
    return 0;
}

RT_SYSLIB("libSceGnmDriver", sceGnmSubmitAndFlipCommandBuffers, submit_and_flip);
RT_SYSLIB("libSceGnmDriver", sceGnmSubmitDone, submit_done);
