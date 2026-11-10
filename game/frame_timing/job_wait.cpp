// SPDX-License-Identifier: GPL-2.0-or-later
// Waiting for a background job and taking its result. A job is finished when its worker sets the
// state to job_state_finished; the waits poll that state with a 1 ms kernel sleep in between.
#include <atomic>
#include <cstdint>

#include "job.h"
#include "time_of_day.h"

namespace {

using namespace frame_timing::job;
using frame_timing::time_of_day::kernel_usleep;

constexpr uint32_t poll_interval_microseconds = 1000;

// How 0x024bd150 treats a job that is not done yet.
enum class TakeMode : int32_t {
    poll = 0,   // return null
    // any other value but take: wait for the worker
    take = 2,   // take the result without checking
};

// The state is written by another thread; the load is a plain read, as in the original.
int32_t job_state(Job *job)
{
    return std::atomic_ref<int32_t>(job->state).load(std::memory_order_relaxed);
}

void wait_until_finished(Job *job)
{
    while (job_state(job) != job_state_finished) kernel_usleep(poll_interval_microseconds);
}

bool is_done(Job *job)
{
    JobLock &lock = job_lock.get();
    lock.vtable->lock(&lock, wait_forever);
    const bool done = job->flags & job_flag_done;
    lock.vtable->unlock(&lock);
    return done;
}

// Counts one more holder of the result and reads it, under the job lock.
void *take_result(Job *job)
{
    JobLock &lock = job_lock.get();
    lock.vtable->lock(&lock, wait_forever);
    ++job->reference_count;
    void *result = job->result_count ? (*job->results)->value : job->single_result;
    lock.vtable->unlock(&lock);
    return result;
}

}  // namespace

// Blocks until the job is finished. One caller, in the file loader (0x02a70b1b).
extern "C" void bb_frame_timing_job_wait(Job *job)
{
    wait_until_finished(job);
}

// The handle's result: null without a job, null for a poll of a job that is not done, after a
// wait otherwise. Each step reads the handle's job again, as the original does. Called from the
// task system (0x01370cc1, 0x01371631, 0x0138a969).
extern "C" void *bb_frame_timing_job_take_result(JobHandle *handle, int32_t mode)
{
    Job *job = handle->job;
    if (!job) return nullptr;
    if (mode != static_cast<int32_t>(TakeMode::take)) {
        const bool done = is_done(job);
        if (!done) {
            if (mode == static_cast<int32_t>(TakeMode::poll)) return nullptr;
            wait_until_finished(handle->job);
        }
    }
    return take_result(handle->job);
}
