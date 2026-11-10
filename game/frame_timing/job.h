// SPDX-License-Identifier: GPL-2.0-or-later
// A background job the engine waits on by polling its state, and the handle callers hold.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace frame_timing::job {

struct JobResultEntry {
    uint8_t unknown_0x00[0x10];
    void *value;
};
static_assert(offsetof(JobResultEntry, value) == 0x10);

struct Job {
    uint8_t unknown_0x00[0x10];
    int32_t state;   // set by the worker; job_state_finished when it is done (0x024bdd70 sets 1)
    uint8_t unknown_0x14[0x4];
    void *single_result;   // the result when result_count is zero
    uint8_t unknown_0x20[0x10];
    uint8_t reference_count;   // bumped by each caller that takes the result
    uint8_t flags;             // job_flag_done: the job has completed
    uint8_t unknown_0x32[0x4e];
    JobResultEntry **results;
    uint64_t result_count;
};
static_assert(offsetof(Job, state) == 0x10 && offsetof(Job, single_result) == 0x18);
static_assert(offsetof(Job, reference_count) == 0x30 && offsetof(Job, flags) == 0x31);
static_assert(offsetof(Job, results) == 0x80 && offsetof(Job, result_count) == 0x88);

struct JobHandle {
    uint8_t unknown_0x00[0x8];
    Job *job;
};
static_assert(offsetof(JobHandle, job) == 0x8);

constexpr int32_t job_state_finished = 2;
constexpr uint8_t job_flag_done = 0x01;

// The lock all jobs share (an engine lock object: lock with a timeout, unlock).
struct JobLock;
struct JobLockVtable {
    void *unknown_slots[3];
    void (*lock)(JobLock *, int32_t timeout);
    void *unknown_slot_4;
    void (*unlock)(JobLock *);
};
static_assert(offsetof(JobLockVtable, lock) == 0x18 && offsetof(JobLockVtable, unlock) == 0x28);
struct JobLock {
    const JobLockVtable *vtable;
};
constexpr int32_t wait_forever = -1;
RT_GLOBAL(0x05aa4578, job_lock, JobLock);

}  // namespace frame_timing::job
