// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the 0x1d0-byte job the language object's step at 0x01d53050 gives a load (kind 11). Its
// constructor is in localize_job.cpp. What the job does is not established: fields are named
// after their offsets.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/load.h"
#include "runtime/original.h"

namespace menu::localize {

// A member the job's constructor hands to 0x02256c30, which fills it in (0x110 bytes: up to the
// flags at +0x1c0).
struct JobMember {
    uint8_t unknown_0x00[0x110];
};

// The job starts like every load job: its vtable and a reference count. The fields written by
// the constructor are named; the gaps are never written by it.
struct LocalizeJob {
    const engine::LoadJobVtable *vtable;
    uint32_t references;
    uint32_t unknown_0x0c;
    uint32_t unknown_0x10;
    uint32_t unknown_0x14;
    uint8_t unknown_0x18;
    uint8_t unknown_0x19[0x7];
    const void *second_vtable;
    const void *third_vtable;
    uint8_t unknown_0x30[0x48];
    uint64_t unknown_0x78;
    int32_t unknown_0x80;
    uint8_t unknown_0x84[0x4];
    const void *fourth_vtable;
    uint64_t unknown_0x90;
    uint8_t unknown_0x98;
    uint8_t unknown_0x99[0x7];
    uint64_t unknown_0xa0;
    engine::Load *load;   // created by the constructor when the loads are idle
    JobMember member;
    uint8_t unknown_0x1c0;
    uint8_t unknown_0x1c1;
    uint8_t unknown_0x1c2[0x6];
    uint64_t unknown_0x1c8;
};
static_assert(offsetof(LocalizeJob, references) == 0x8 && offsetof(LocalizeJob, unknown_0x0c) == 0xc);
static_assert(offsetof(LocalizeJob, unknown_0x18) == 0x18 && offsetof(LocalizeJob, second_vtable) == 0x20);
static_assert(offsetof(LocalizeJob, third_vtable) == 0x28 && offsetof(LocalizeJob, unknown_0x78) == 0x78);
static_assert(offsetof(LocalizeJob, unknown_0x80) == 0x80 && offsetof(LocalizeJob, fourth_vtable) == 0x88);
static_assert(offsetof(LocalizeJob, unknown_0x98) == 0x98 && offsetof(LocalizeJob, load) == 0xa8);
static_assert(offsetof(LocalizeJob, member) == 0xb0 && offsetof(LocalizeJob, unknown_0x1c0) == 0x1c0);
static_assert(offsetof(LocalizeJob, unknown_0x1c8) == 0x1c8 && sizeof(LocalizeJob) == 0x1d0);

// The job's constructor (replaced in localize_job.cpp), for the step that creates the load.
RT_ORIGINAL(0x01d59fb0, job_construct, void(LocalizeJob *));

}  // namespace menu::localize
