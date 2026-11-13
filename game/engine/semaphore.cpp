// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's semaphore (Dantelion2 DLSemaphore): a vtable, the system semaphore handle
// and whether it was created.
//
// - engine_semaphore_wait_02a3ae00 (0x02a3ae00) takes one unit, waiting forever when the timeout
//   is -1, else up to the timeout in microseconds. Returns -3 on ETIMEDOUT, else 0. Errors are
//   reported from their own message on through the ones after it in the source (not found,
//   canceled, bad timeout address, deleted, invalid count, unknown), as the shipped game does.
// - engine_semaphore_signal_02a3b0d0 (0x02a3b0d0) gives `count` units back. EINVAL is ignored; a
//   missing semaphore and any other error are reported. Returns 0.
// Both report a semaphore that was never created and go on.
#include <cstddef>
#include <cstdint>

#include "engine.h"
#include "runtime/original.h"

namespace {

struct Semaphore {
    const void *vtable;
    uintptr_t handle;           // the system semaphore id, in an 8-byte slot
    bool created;
};
static_assert(offsetof(Semaphore, handle) == 0x8 && offsetof(Semaphore, created) == 0x10);

// The executable's call stubs for sceKernelWaitSema and sceKernelSignalSema.
RT_ORIGINAL(0x02fbec78, sce_wait_sema, int32_t(uintptr_t handle, int32_t count, uint32_t *timeout_us));
RT_ORIGINAL(0x02fbec88, sce_signal_sema, int32_t(uintptr_t handle, int32_t count));

// SCE error codes (0x80020000 plus the FreeBSD error number)
constexpr int32_t sce_esrch = static_cast<int32_t>(0x80020003), sce_eacces = static_cast<int32_t>(0x8002000d),
                  sce_efault = static_cast<int32_t>(0x8002000e), sce_einval = static_cast<int32_t>(0x80020016),
                  sce_etimedout = static_cast<int32_t>(0x8002003c), sce_ecanceled = static_cast<int32_t>(0x80020055);

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\ps4\DLSemaphore.cpp"
RT_GLOBAL(0x04c68aaa, source_file, const char);
// "Sync object not created"
RT_GLOBAL(0x04c68c95, message_not_created, const char);
// "Semaphore is not found.(SCE_KERNEL_ERROR_ESRCH)"
RT_GLOBAL(0x04c68c65, message_not_found, const char);
// "Semaphore was canceled.(SCE_KERNEL_ERROR_ECANCELED)"
RT_GLOBAL(0x04c68cad, message_canceled, const char);
// "usec points invalid address.(SCE_KERNEL_ERROR_EFAULT)"
RT_GLOBAL(0x04c68ce1, message_bad_timeout_address, const char);
// "Semaphore was deleted.(SCE_KERNEL_ERROR_EACCES)"
RT_GLOBAL(0x04c68d17, message_deleted, const char);
// "invalid needCount.(SCE_KERNEL_ERROR_EINVAL)"
RT_GLOBAL(0x04c68d47, message_invalid_count, const char);
// "Unknown Error."
RT_GLOBAL(0x04c68c56, message_unknown_error, const char);

constexpr int32_t line_wait_not_created = 0xa3, line_wait_not_found = 0xb7, line_wait_canceled = 0xb9,
                  line_wait_bad_timeout_address = 0xbb, line_wait_deleted = 0xbd, line_wait_invalid_count = 0xbf,
                  line_wait_unknown_error = 0xc1;
constexpr int32_t line_signal_not_created = 0x112, line_signal_not_found = 0x11a, line_signal_unknown_error = 0x11f;

template <typename Message> void report(int32_t line, const Message &message)
{
    engine::fatal_error(source_file.address(), line, message.address());
}

void check_created(const Semaphore *semaphore, int32_t line)
{
    if (!semaphore->created)
        report(line, message_not_created);
}

// A wait error's reports: from its own message through the ones after it.
void report_wait_error(int32_t result)
{
    switch (result) {
    case sce_esrch:
        report(line_wait_not_found, message_not_found);
        [[fallthrough]];
    case sce_ecanceled:
        report(line_wait_canceled, message_canceled);
        [[fallthrough]];
    case sce_efault:
        report(line_wait_bad_timeout_address, message_bad_timeout_address);
        [[fallthrough]];
    case sce_eacces:
        report(line_wait_deleted, message_deleted);
        [[fallthrough]];
    case sce_einval:
        report(line_wait_invalid_count, message_invalid_count);
        [[fallthrough]];
    default:
        report(line_wait_unknown_error, message_unknown_error);
    }
}

}  // namespace

extern "C" int32_t bb_engine_semaphore_wait_02a3ae00(Semaphore *semaphore, int32_t timeout_us)
{
    constexpr int32_t wait_forever = -1;
    constexpr int32_t timed_out = -3;
    check_created(semaphore, line_wait_not_created);
    uint32_t timeout = static_cast<uint32_t>(timeout_us);
    const int32_t result = sce_wait_sema(semaphore->handle, 1, timeout_us == wait_forever ? nullptr : &timeout);
    if (result == 0)
        return 0;
    if (result == sce_etimedout)
        return timed_out;
    report_wait_error(result);
    return 0;
}

extern "C" int32_t bb_engine_semaphore_signal_02a3b0d0(Semaphore *semaphore, int32_t count)
{
    check_created(semaphore, line_signal_not_created);
    const int32_t result = sce_signal_sema(semaphore->handle, count);
    if (result == 0 || result == sce_einval)
        return 0;
    if (result == sce_esrch)
        report(line_signal_not_found, message_not_found);
    else
        report(line_signal_unknown_error, message_unknown_error);
    return 0;
}
