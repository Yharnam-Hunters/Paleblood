/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. The audio job manager (libSceAjm): compressed audio decoded in batches
   of jobs that the title builds in a buffer and hands over.

   Facts, from shadPS4's libSceAjm (ajm.h, ajm.cpp, ajm_batch.cpp, ajm_context.cpp, ajm_instance.cpp,
   ajm_at9.*, ajm_error.h, ajm_result.h), checked against bbhost (a host for one title, whose
   observations of the title's FMOD are noted) and PS4Recomp; implemented here independently. The
   ATRAC9 configuration word's layout and tables are LibAtrac9's (MIT).
   - Contexts: sceAjmInitialize(0, &context), sceAjmFinalize(context). Codecs registered per
     context: sceAjmModuleRegister(context, codec, 0), 0 MP3, 1 ATRAC9, 2 AAC, below 23.
   - Instances: sceAjmInstanceCreate(context, codec, flags, &instance), the id the slot index with
     the codec at bit 14; flags bits 0-2 a revision (not 0), 3-6 the channels, 7-9 the sample
     format (0 16-bit), bit 10 gapless looping. The title creates ATRAC9 instances with 0x409 to
     0x441: revision 1, 1 to 8 channels, 16-bit, gapless looping (0x01118d9a).
   - Batches: the title writes jobs with sceAjmBatchJobControlBufferRa and
     sceAjmBatchJobRunBufferRa, each returning the next free position; a job is a sequence of
     chunks, each a 32-bit header (identifier in bits 0-5, a 20-bit payload in bits 6-25): the job
     (payload the instance, then the body's size), an optional return address (6), the input (1 run,
     2 control) and output (17 run, 18 control) buffers { header, size, pointer } and the flags (3
     control, 4 run; payload the flags' high word, then the low word). A control job is 0x30 bytes
     (0x40 with a return address), a run job 0x40 (0x50); the title's FMOD lays jobs out at those
     fixed strides. sceAjmBatchStartBuffer(context, batch, size, priority, error, &id) runs them
     (a size not a multiple of 8 is malformed); sceAjmBatchWait(context, id, timeout in ms, error),
     sceAjmBatchCancel(context, id), sceAjmBatchErrorDump(batch info, error).
   - Job flags: run bits 11 (get codec info) and 12 (multiple frames), control bits 13 (reset), 14
     (initialize), 15 (resample), sideband bits 45 (gapless), 46 (format), 47 (stream). A job's
     output control buffer starts with its result { result bits, internal result }, then a stream
     result { input consumed, output written, total samples }, the format, the gapless state, the
     frame count and the codec info, as its flags ask. The ATRAC9 codec info is { superframe bytes,
     frames per superframe, bytes left in the superframe, samples per frame }.
   - Errors: 0x80930002 context, 0x80930003 instance, 0x80930004 batch, 0x80930005 parameter,
     0x80930006 no batch id left, 0x80930007 no instance left, 0x80930009 codec already
     registered, 0x8093000A codec not registered, 0x8093000B revision 0, 0x80930011 malformed batch,
     0x80930017 cancelled.

   The runtime runs a batch's jobs when it is started, on the calling thread (bbhost does the same
   for this title), so a wait finds it finished. Control jobs are modelled (initialize with the
   ATRAC9 configuration word, reset, gapless), and so is the codec info, all derived from the
   configuration. Decoding audio is not provided yet: a run job with input stops by name. Other
   codecs, resampling, the format input and the statistics instance stop by name too. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AJM_ERROR_INVALID_CONTEXT ((int32_t)0x80930002u)
#define AJM_ERROR_INVALID_INSTANCE ((int32_t)0x80930003u)
#define AJM_ERROR_INVALID_BATCH ((int32_t)0x80930004u)
#define AJM_ERROR_INVALID_PARAMETER ((int32_t)0x80930005u)
#define AJM_ERROR_OUT_OF_MEMORY ((int32_t)0x80930006u)
#define AJM_ERROR_OUT_OF_RESOURCES ((int32_t)0x80930007u)
#define AJM_ERROR_CODEC_ALREADY_REGISTERED ((int32_t)0x80930009u)
#define AJM_ERROR_CODEC_NOT_REGISTERED ((int32_t)0x8093000Au)
#define AJM_ERROR_WRONG_REVISION_FLAG ((int32_t)0x8093000Bu)
#define AJM_ERROR_MALFORMED_BATCH ((int32_t)0x80930011u)
#define AJM_ERROR_CANCELLED ((int32_t)0x80930017u)

enum { CODEC_ATRAC9 = 1, CODEC_LIMIT = 23, MAX_CONTEXTS = 16, MAX_INSTANCES = 128, MAX_BATCHES = 1023 };
enum { INSTANCE_INDEX_MASK = 0x3FFF, INSTANCE_CODEC_SHIFT = 14, STATISTICS_INSTANCE = 0x80000 };

/* Chunk identifiers and the job flags (u64). */
enum { CHUNK_JOB = 0, CHUNK_INPUT_RUN = 1, CHUNK_INPUT_CONTROL = 2, CHUNK_CONTROL_FLAGS = 3, CHUNK_RUN_FLAGS = 4,
       CHUNK_RETURN_ADDRESS = 6, CHUNK_INLINE = 7, CHUNK_OUTPUT_RUN = 17, CHUNK_OUTPUT_CONTROL = 18 };
#define FLAG_GET_CODEC_INFO (1ull << 11)
#define FLAG_MULTIPLE_FRAMES (1ull << 12)
#define FLAG_RESET (1ull << 13)
#define FLAG_INITIALIZE (1ull << 14)
#define FLAG_RESAMPLE (1ull << 15)
#define FLAG_GAPLESS (1ull << 45)
#define FLAG_FORMAT (1ull << 46)
#define FLAG_STREAM (1ull << 47)
#define CONTROL_FLAGS_KEPT 0x000060000000E7FFull /* what a control job keeps of its flags */
#define RUN_FLAGS_KEPT 0x0000E00000001FFFull     /* and a run job */

typedef struct { int32_t result, internal_result; } sideband_result;
typedef struct { int32_t input_consumed, output_written; uint64_t total_samples; } sideband_stream;
typedef struct { uint32_t total_samples; uint16_t skip_samples, skipped_samples; } sideband_gapless;
typedef struct { uint32_t superframe_bytes, frames_in_superframe, next_frame_bytes, frame_samples; } atrac9_codec_info;
_Static_assert(sizeof(sideband_result) == 8 && sizeof(sideband_stream) == 16 && sizeof(sideband_gapless) == 8, "sidebands");
_Static_assert(sizeof(atrac9_codec_info) == 16, "the ATRAC9 codec info is 16 bytes");

typedef struct {
    int used;
    uint32_t codec;
    uint64_t flags;
    int configured;               /* an initialize gave it a configuration word */
    uint8_t config[4];
    atrac9_codec_info info;
    sideband_gapless gapless;
    uint64_t total_samples;       /* decoded since the last reset */
} ajm_instance;

typedef struct {
    int used;
    uint32_t registered;          /* a bit per codec */
    ajm_instance instances[MAX_INSTANCES + 1]; /* by index; 0 is never an index */
    uint8_t batch_used[MAX_BATCHES + 1];
} ajm_context;

static pthread_mutex_t ajm_lock = PTHREAD_MUTEX_INITIALIZER;
static ajm_context contexts[MAX_CONTEXTS + 1]; /* by id; 0 is never an id */

static ajm_context *context_of(uint32_t id) { return id >= 1 && id <= MAX_CONTEXTS && contexts[id].used ? &contexts[id] : NULL; }

static int32_t ajm_initialize(int64_t reserved, uint32_t *context)
{
    if (reserved || !context) return AJM_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&ajm_lock);
    uint32_t id = 1;
    while (id <= MAX_CONTEXTS && contexts[id].used) id++;
    if (id > MAX_CONTEXTS) rt_fatal("sceAjmInitialize", "more contexts than the runtime keeps: the answer is not established");
    memset(&contexts[id], 0, sizeof contexts[id]);
    contexts[id].used = 1;
    *context = id;
    pthread_mutex_unlock(&ajm_lock);
    return 0;
}

static int32_t ajm_finalize(uint32_t context)
{
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    if (c) c->used = 0;
    pthread_mutex_unlock(&ajm_lock);
    return c ? 0 : AJM_ERROR_INVALID_CONTEXT;
}

static int32_t ajm_module_register(uint32_t context, uint32_t codec, int64_t reserved)
{
    if (reserved || codec >= CODEC_LIMIT) return AJM_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    const int32_t result = !c ? AJM_ERROR_INVALID_CONTEXT : (c->registered & (1u << codec)) ? AJM_ERROR_CODEC_ALREADY_REGISTERED : 0;
    if (!result) c->registered |= 1u << codec;
    pthread_mutex_unlock(&ajm_lock);
    return result;
}

static int32_t ajm_module_unregister(uint32_t context, uint32_t codec)
{
    if (codec >= CODEC_LIMIT) return AJM_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    const int32_t result = !c ? AJM_ERROR_INVALID_CONTEXT : !(c->registered & (1u << codec)) ? AJM_ERROR_CODEC_NOT_REGISTERED : 0;
    if (!result) c->registered &= ~(1u << codec);
    pthread_mutex_unlock(&ajm_lock);
    return result;
}

static int32_t ajm_instance_create(uint32_t context, uint32_t codec, uint64_t flags, uint32_t *instance)
{
    static const char fn[] = "sceAjmInstanceCreate";
    if (codec >= CODEC_LIMIT || !instance) return AJM_ERROR_INVALID_PARAMETER;
    if (!(flags & 7)) return AJM_ERROR_WRONG_REVISION_FLAG;
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    int32_t result = !c ? AJM_ERROR_INVALID_CONTEXT : !(c->registered & (1u << codec)) ? AJM_ERROR_CODEC_NOT_REGISTERED : 0;
    if (!result && codec != CODEC_ATRAC9) rt_fatal(fn, "a codec other than ATRAC9: not provided");
    if (!result && ((flags >> 7) & 7)) rt_fatal(fn, "a sample format other than 16-bit: not provided");
    uint32_t index = 1;
    while (!result && index <= MAX_INSTANCES && c->instances[index].used) index++;
    if (!result && index > MAX_INSTANCES) result = AJM_ERROR_OUT_OF_RESOURCES;
    if (!result) {
        c->instances[index] = (ajm_instance){.used = 1, .codec = codec, .flags = flags};
        *instance = index | codec << INSTANCE_CODEC_SHIFT;
    }
    pthread_mutex_unlock(&ajm_lock);
    return result;
}

static ajm_instance *instance_of(ajm_context *c, uint32_t id) /* with ajm_lock held */
{
    const uint32_t index = id & INSTANCE_INDEX_MASK;
    return c && index >= 1 && index <= MAX_INSTANCES && c->instances[index].used &&
           c->instances[index].codec == id >> INSTANCE_CODEC_SHIFT ? &c->instances[index] : NULL;
}

static int32_t ajm_instance_destroy(uint32_t context, uint32_t instance)
{
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    ajm_instance *i = instance_of(c, instance);
    const int32_t result = !c ? AJM_ERROR_INVALID_CONTEXT : !i ? AJM_ERROR_INVALID_INSTANCE : 0;
    if (i) i->used = 0;
    pthread_mutex_unlock(&ajm_lock);
    return result;
}

/* ---------------------------------------------------------------- building jobs */

static uint32_t chunk_header(uint32_t identifier, uint32_t payload) { return (identifier & 0x3F) | (payload & 0xFFFFF) << 6; }

static uint8_t *put_u32(uint8_t *at, uint32_t value) { memcpy(at, &value, 4); return at + 4; }
static uint8_t *put_u64(uint8_t *at, uint64_t value) { memcpy(at, &value, 8); return at + 8; }

static uint8_t *put_buffer(uint8_t *at, uint32_t identifier, const void *pointer, uint64_t size)
{
    at = put_u32(at, chunk_header(identifier, 0));
    at = put_u32(at, (uint32_t)size);
    return put_u64(at, (uint64_t)(uintptr_t)pointer);
}

static uint8_t *put_flags(uint8_t *at, uint32_t identifier, uint64_t flags)
{
    at = put_u32(at, chunk_header(identifier, (uint32_t)(flags >> 32)));
    return put_u32(at, (uint32_t)flags);
}

static uint8_t *put_job_start(uint8_t *at, uint32_t instance, uint32_t body_bytes, const void *return_address)
{
    at = put_u32(at, chunk_header(CHUNK_JOB, instance));
    at = put_u32(at, body_bytes + (return_address ? 16 : 0));
    return return_address ? put_buffer(at, CHUNK_RETURN_ADDRESS, return_address, 0) : at;
}

static void *ajm_job_control(void *position, uint32_t instance, uint64_t flags, void *sideband_in, uint64_t sideband_in_size,
                             void *sideband_out, uint64_t sideband_out_size, void *return_address)
{
    if (instance == STATISTICS_INSTANCE) rt_fatal("sceAjmBatchJobControlBufferRa", "statistics jobs are not provided");
    uint8_t *at = put_job_start(position, instance, 0x28, return_address);
    at = put_buffer(at, CHUNK_INPUT_CONTROL, sideband_in, sideband_in_size);
    at = put_flags(at, CHUNK_CONTROL_FLAGS, flags & CONTROL_FLAGS_KEPT);
    return put_buffer(at, CHUNK_OUTPUT_CONTROL, sideband_out, sideband_out_size);
}

static void *ajm_job_run(void *position, uint32_t instance, uint64_t flags, void *data_in, uint64_t data_in_size,
                         void *data_out, uint64_t data_out_size, void *sideband_out, uint64_t sideband_out_size, void *return_address)
{
    uint8_t *at = put_job_start(position, instance, 0x38, return_address);
    at = put_buffer(at, CHUNK_INPUT_RUN, data_in, data_in_size);
    at = put_flags(at, CHUNK_RUN_FLAGS, flags & RUN_FLAGS_KEPT);
    at = put_buffer(at, CHUNK_OUTPUT_RUN, data_out, data_out_size);
    return put_buffer(at, CHUNK_OUTPUT_CONTROL, sideband_out, sideband_out_size);
}

/* ---------------------------------------------------------------- running jobs */

typedef struct {
    uint32_t instance;
    uint64_t flags;
    const uint8_t *input_control, *input_run;
    uint64_t input_control_bytes, input_run_bytes;
    uint8_t *output_control;
    uint64_t output_control_bytes;
    int has_output_run;
} job;

/* The ATRAC9 configuration word (LibAtrac9's ReadConfigData), read most significant bit first:
   0xFE, a 4-bit sample rate index, a 3-bit channel configuration, a 0 bit, 11 bits of frame bytes
   minus one, a 2-bit superframe index. 0 when it is valid. */
static int read_atrac9_config(const uint8_t config[4], atrac9_codec_info *info)
{
    static const uint8_t frame_samples_power[16] = {6, 6, 7, 7, 7, 8, 8, 8, 6, 6, 7, 7, 7, 8, 8, 8};
    const uint32_t word = (uint32_t)config[0] << 24 | (uint32_t)config[1] << 16 | (uint32_t)config[2] << 8 | config[3];
    const uint32_t header = word >> 24, rate_index = (word >> 20) & 0xF, channel_config = (word >> 17) & 7;
    const uint32_t validation = (word >> 16) & 1, frame_bytes = ((word >> 5) & 0x7FF) + 1, superframe_index = (word >> 3) & 3;
    if (header != 0xFE || validation || channel_config > 5) return -1;
    const uint32_t frames = 1u << superframe_index;
    *info = (atrac9_codec_info){.superframe_bytes = frame_bytes * frames, .frames_in_superframe = frames,
                                .next_frame_bytes = frame_bytes * frames, .frame_samples = 1u << frame_samples_power[rate_index]};
    return 0;
}

/* Reads the job's chunks at `at` (its body of `bytes`); stops by name on what it does not know. */
static void parse_job(const char *fn, const uint8_t *at, uint32_t bytes, job *out)
{
    const uint8_t *end = at + bytes;
    while (at < end) {
        uint32_t header, low;
        memcpy(&header, at, 4);
        const uint32_t identifier = header & 0x3F, payload = (header >> 6) & 0xFFFFF;
        if (identifier == CHUNK_CONTROL_FLAGS || identifier == CHUNK_RUN_FLAGS) {
            memcpy(&low, at + 4, 4);
            out->flags = (uint64_t)payload << 32 | low;
            at += 8;
            continue;
        }
        uint32_t size;
        uint64_t pointer;
        memcpy(&size, at + 4, 4);
        memcpy(&pointer, at + 8, 8);
        switch (identifier) {
        case CHUNK_INPUT_CONTROL: out->input_control = (const uint8_t *)(uintptr_t)pointer, out->input_control_bytes = size; break;
        case CHUNK_INPUT_RUN:
            if (out->input_run) rt_fatal(fn, "a job with more than one input buffer: not provided");
            out->input_run = (const uint8_t *)(uintptr_t)pointer, out->input_run_bytes = size;
            break;
        case CHUNK_OUTPUT_RUN: out->has_output_run = 1; break;
        case CHUNK_OUTPUT_CONTROL: out->output_control = (uint8_t *)(uintptr_t)pointer, out->output_control_bytes = size; break;
        case CHUNK_RETURN_ADDRESS: break;
        default: rt_fatal(fn, "a job chunk the runtime does not know");
        }
        at += 16;
    }
}

/* Appends one output sideband if it fits. */
static void put_sideband(uint8_t **at, const uint8_t *end, const void *data, size_t size)
{
    if (*at && (size_t)(end - *at) >= size) memcpy(*at, data, size), *at += size;
}

static void run_job(const char *fn, ajm_context *c, const job *j)
{
    ajm_instance *instance = instance_of(c, j->instance);
    sideband_result result = {0};
    if (!instance) result.result = (int32_t)0x80000004u; /* fatal, invalid parameter: no such instance */
    const uint8_t *in = j->input_control, *in_end = in ? in + j->input_control_bytes : NULL;
    if (instance) {
        if (j->flags & (FLAG_RESAMPLE | FLAG_FORMAT) && in) rt_fatal(fn, "resampling and format inputs are not provided");
        if (j->flags & FLAG_RESET) {
            instance->total_samples = 0;
            instance->gapless = (sideband_gapless){0};
            instance->info.next_frame_bytes = instance->info.superframe_bytes;
        }
        if ((j->flags & FLAG_GAPLESS) && in && in_end - in >= (long)sizeof(sideband_gapless)) {
            sideband_gapless gapless;
            memcpy(&gapless, in, sizeof gapless);
            in += sizeof gapless;
            instance->gapless.total_samples = gapless.total_samples, instance->gapless.skip_samples = gapless.skip_samples;
        }
        if (j->flags & FLAG_INITIALIZE) {
            if (!in || in_end - in < 8) rt_fatal(fn, "an initialize without its 8-byte parameters");
            memcpy(instance->config, in, 4);
            instance->configured = read_atrac9_config(instance->config, &instance->info) == 0;
            if (!instance->configured) result.result |= 0x2; /* invalid data */
            instance->total_samples = 0;
        }
        if (j->input_run && j->input_run_bytes) rt_fatal(fn, "decoding ATRAC9 audio is not provided yet");
        if ((j->flags & FLAG_GET_CODEC_INFO) && !instance->configured) result.result |= 0x1; /* not initialized */
    }
    uint8_t *out = j->output_control, *end = out ? out + j->output_control_bytes : NULL;
    put_sideband(&out, end, &result, sizeof result);
    if (!instance) return;
    if (j->flags & FLAG_STREAM) {
        const sideband_stream stream = {.total_samples = instance->total_samples};
        put_sideband(&out, end, &stream, sizeof stream);
    }
    if (j->flags & FLAG_GAPLESS) put_sideband(&out, end, &instance->gapless, sizeof instance->gapless);
    if (j->flags & FLAG_MULTIPLE_FRAMES) {
        const uint32_t frames[2] = {0, 0};
        put_sideband(&out, end, frames, sizeof frames);
    }
    if ((j->flags & FLAG_GET_CODEC_INFO) && instance->configured) put_sideband(&out, end, &instance->info, sizeof instance->info);
}

static int32_t ajm_batch_start(uint32_t context, const uint8_t *batch, uint32_t size, int32_t priority, void *error, uint32_t *batch_id)
{
    static const char fn[] = "sceAjmBatchStartBuffer";
    (void)priority; /* jobs run at once, in order */
    (void)error;    /* written only on a failing job, which the runtime does not report through it */
    if (size & 7) return AJM_ERROR_MALFORMED_BATCH;
    if (!batch_id || (!batch && size)) return AJM_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    int32_t result = c ? 0 : AJM_ERROR_INVALID_CONTEXT;
    uint32_t id = 1;
    while (!result && id <= MAX_BATCHES && c->batch_used[id]) id++;
    if (!result && id > MAX_BATCHES) result = AJM_ERROR_OUT_OF_MEMORY;
    for (uint32_t at = 0; !result && at < size;) {
        uint32_t header, bytes;
        memcpy(&header, batch + at, 4);
        memcpy(&bytes, batch + at + 4, 4);
        if (at + 8 + (uint64_t)bytes > size) rt_fatal(fn, "a job running past the end of its batch");
        const uint32_t identifier = header & 0x3F;
        if (identifier == CHUNK_JOB) {
            job j = {.instance = (header >> 6) & 0xFFFFF};
            parse_job(fn, batch + at + 8, bytes, &j);
            run_job(fn, c, &j);
        } else if (identifier != CHUNK_INLINE) {
            rt_fatal(fn, "a batch chunk other than a job or inline data");
        }
        at += 8 + ((bytes + 7) & ~7u);
    }
    if (!result) c->batch_used[id] = 1, *batch_id = id;
    pthread_mutex_unlock(&ajm_lock);
    return result;
}

/* The batch ran when it was started: a wait finds it finished and frees its id. */
static int32_t ajm_batch_wait(uint32_t context, uint32_t batch_id, uint32_t timeout_ms, void *error)
{
    (void)timeout_ms, (void)error;
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    const int valid = c && batch_id >= 1 && batch_id <= MAX_BATCHES && c->batch_used[batch_id];
    if (valid) c->batch_used[batch_id] = 0;
    pthread_mutex_unlock(&ajm_lock);
    return !c ? AJM_ERROR_INVALID_CONTEXT : valid ? 0 : AJM_ERROR_INVALID_BATCH;
}

/* Cancelling a batch that has already run does nothing (shadPS4). */
static int32_t ajm_batch_cancel(uint32_t context, uint32_t batch_id)
{
    pthread_mutex_lock(&ajm_lock);
    ajm_context *c = context_of(context);
    const int valid = c && batch_id >= 1 && batch_id <= MAX_BATCHES && c->batch_used[batch_id];
    pthread_mutex_unlock(&ajm_lock);
    return !c ? AJM_ERROR_INVALID_CONTEXT : valid ? 0 : AJM_ERROR_INVALID_BATCH;
}

/* A debugging aid: reports the batch error record { code, job, command offset, return address }. */
static int32_t ajm_batch_error_dump(const void *batch_info, const int32_t *error)
{
    (void)batch_info;
    if (error) fprintf(stderr, "runtime: sceAjmBatchErrorDump: error 0x%08x\n", (unsigned)error[0]);
    return 0;
}

RT_SYSLIB("libSceAjm", sceAjmInitialize, ajm_initialize);
RT_SYSLIB("libSceAjm", sceAjmFinalize, ajm_finalize);
RT_SYSLIB("libSceAjm", sceAjmModuleRegister, ajm_module_register);
RT_SYSLIB("libSceAjm", sceAjmModuleUnregister, ajm_module_unregister);
RT_SYSLIB("libSceAjm", sceAjmInstanceCreate, ajm_instance_create);
RT_SYSLIB("libSceAjm", sceAjmInstanceDestroy, ajm_instance_destroy);
RT_SYSLIB("libSceAjm", sceAjmBatchJobControlBufferRa, ajm_job_control);
RT_SYSLIB("libSceAjm", sceAjmBatchJobRunBufferRa, ajm_job_run);
RT_SYSLIB("libSceAjm", sceAjmBatchStartBuffer, ajm_batch_start);
RT_SYSLIB("libSceAjm", sceAjmBatchWait, ajm_batch_wait);
RT_SYSLIB("libSceAjm", sceAjmBatchCancel, ajm_batch_cancel);
RT_SYSLIB("libSceAjm", sceAjmBatchErrorDump, ajm_batch_error_dump);
