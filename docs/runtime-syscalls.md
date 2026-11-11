# Runtime syscall behavior notes

This page records contracts used to implement small runtime calls in issues [#34](https://github.com/Yharnam-Hunters/Paleblood/issues/34) and [#41](https://github.com/Yharnam-Hunters/Paleblood/issues/41). Public standards and manuals describe portable or platform-specific API behavior; they do not by themselves prove every detail of the PS4 implementation. Keep the distinction below, and leave unresolved behavior unresolved until public documentation, our own reverse engineering, or observed behavior settles it.

## Evidence in this repository

The runtime registry distinguishes behavior it implements from diagnostics for behavior that is
still unknown. `RT_SYSLIB` entries are callable implementations and count toward coverage;
`RT_SYSLIB_UNRESOLVED` entries remain callable so a reached import can fail with a useful name,
but do not count as implemented or as supplied imports in progress reports.

`symbols/imports.csv` records `getpagesize` and `gettimeofday` as imports of `eboot` from `libScePosix`; `libc.elf` imports `clock_gettime`, `getpagesize`, `getpid`, and `gettimeofday` from `libkernel`. Import rows establish that the names are required, not their arguments, return values at every call site, or all clock IDs.

The eight hash-verified target images (`eboot.elf` and the seven modules, `tools/check_target.py`) all state `p_align` 0x4000 for every `PT_LOAD` segment, with `p_offset` congruent to `p_vaddr` modulo 0x4000 (the difference is exactly 0x4000 in each; `readelf -l` prints the headers).

The decompiled frame limiter calls `gettimeofday` with a null timezone argument. A separate [verified clock helper](../game/frame_timing/monotonic_clock.cpp) calls `sceKernelClockGettime` with clock ID `4`, named `CLOCK_MONOTONIC` in that code. This confirms one monotonic clock request in the game; it does not establish every ID passed through the imported `clock_gettime` wrapper, or that no other ID is used in unexamined code.

The [`sceKernelGetCompiledSdkVersion` contract](own-runtime-compiled-sdk.md) describes the
observed libkernel getter, the runtime's provider of mapped executable process-parameter
metadata, and the dedicated synthetic registry tests. It also distinguishes that metadata
provider from the unresolved console kernel syscall behavior.

## Interfaces and known behavior

| Function | Arguments and representation | Publicly documented result and errors | Evidence and open questions for PS4 |
|---|---|---|---|
| `getpagesize(void)` | No arguments. Returns the target's page size in bytes. The numeric value is platform-specific. | `getpagesize` is not in current POSIX; portable code is directed to `sysconf(_SC_PAGESIZE)`. Linux and FreeBSD document the byte count; their manuals do not define a general error return for this function. Linux exposes it as a libc interface, not necessarily a dedicated kernel syscall. FreeBSD notes the system page size may differ from the underlying hardware page size. | The import is present twice (`eboot` from `libScePosix`, `libc.elf` from `libkernel`), and both library names are registered against one implementation that answers **16384**, by the maintainer's ruling of 2026-11-10 (`runtime/syslib/kernel.c`, #528). It is a ruling, not a measurement: the target images bound the value without picking one (every `PT_LOAD` of the eight hash-verified ELFs has `p_align` 0x4000 with `p_offset` congruent to `p_vaddr` modulo 0x4000, so the ELF ABI's congruence rule limits the page size to a divisor of 16384), stock FreeBSD on AMD64 uses 4096, and no public source names the console's own value; every PS4 runtime and port we can cite answers 16 KiB. See "getpagesize: observed, inferred, unknown" below. What would still confirm it: one observed `getpagesize()` or `sysconf(_SC_PAGESIZE)` result on a console. |
| `getpid(void)` | No arguments. Returns a `pid_t` naming the calling process. | POSIX specifies that it succeeds and has no error return. Linux process IDs are scoped by PID namespaces; other systems have their own process-ID scope and lifecycle rules. | Imported by `libc.elf` from `libkernel`; libunwind passes the result to `sysctl`. The runtime answers a fixed id, **1000**, by the maintainer's ruling of 2026-11-10 (`runtime/syslib/kernel.c`, #528, see "Process and input" below): runs stay reproducible, and the host's process id is not the guest's. The console's own value and its scope are not established. |
| `clock_gettime(clock_id, tp)` | `clock_id` selects a clock; `tp` points to a `struct timespec`. On the POSIX interface, `tv_sec` is seconds and `tv_nsec` is nanoseconds in `[0, 1,000,000,000)`. The exact target structure layout still needs ABI evidence. | POSIX uses `0` for success and `-1` with `errno` for failure. An unsupported clock ID is an `EINVAL` case. Linux and BSD manuals document additional pointer-error details; use the target contract rather than assuming the host's errno mapping. | The imported wrapper is present. The verified `sceKernelClockGettime` call requests ID `4` (`CLOCK_MONOTONIC`). That is evidence for the SCE entry point only, not proof of the libc wrapper's IDs. Other IDs and exact PS4 error and pointer handling remain unresolved. Unknown IDs must not silently return a fabricated time. |
| `gettimeofday(tv, tz)` | `tv` points to a `struct timeval`: seconds since the Epoch and microseconds in `[0, 1,000,000)`. The historical timezone argument is passed as null at the confirmed game call site. The exact target structure layout still needs ABI evidence. | The wall clock can jump and is not a monotonic elapsed-time source. Historical POSIX specified a zero return on success and left errors undefined; Linux documents `-1`/`EFAULT` for inaccessible pointers and ignores the timezone argument. POSIX.1-2024 removed the interface. | The imported wrapper and game call sites are present; the game passes null for the second argument. Exact target behavior for bad pointers, non-null timezone arguments, and error mapping is not established. Do not silently ignore an error without evidence. |
| `sched_yield(void)` | No arguments. Returns `int`. | POSIX: the calling thread gives up the processor until it is again at the head of its thread list; `0` on success, `-1` with `errno` otherwise, and POSIX defines no error case for it. FreeBSD documents the same call. | Imported by `libc.elf` from `libkernel`. Implemented: a guest thread runs on a host thread, so the runtime yields the host thread and returns `0` (`runtime/syslib/kernel_thread.c`). A host failure has no FreeBSD error number to report, so it stops by name. Not observable in a unit test: that the host scheduler actually ran another thread. |
| `scePthreadYield` | Asks for the same yield through the thread library. The PS4-visible signature is not established. | FreeBSD's `pthread_yield(3)` is `void`; Linux's is `int` (always `0`). No PS4-specific public source gives the SCE name's declaration. | Imported by `eboot` and `libc.elf` from `libkernel`; no call site in this repository shows whether the caller reads a result. Registered with `RT_SYSLIB_UNRESOLVED`: the entry stops by name and does not count as implemented. What would settle it: a guest call site showing whether the return register is read after the call, or a PS4-specific public declaration. Do not assume an `int` or `0` from the FreeBSD or Linux analogues. |
| `memcpy(dest, src, n)`, `memset(dest, c, n)` (libSceLibcInternal) | `void *memcpy(void *, const void *, size_t)` and `void *memset(void *, int, size_t)`, the ISO C declarations. | ISO C 7.24.2.1 and 7.24.6.1: `memcpy` copies `n` bytes from `src` to `dest`; `memset` writes `c` converted to `unsigned char` into the first `n` bytes; both return `dest`. Overlapping `memcpy` regions and null pointers are undefined behaviour. | Imported by `libSceFios2.elf` from `libSceLibcInternal` (`symbols/imports.csv`). Implemented with the host's own functions, since the standard fixes the whole result and no target-specific value is involved (`runtime/syslib/kernel.c`). Not modelled: what the PS4 C library does for the undefined cases; no public source or call site establishes it. |
| `sceKernelUsleep(microseconds)` (libkernel), `usleep(microseconds)` (libScePosix) | One unsigned 32-bit count of microseconds, returning `int`. For `sceKernelUsleep` the guest fixes the shape: the verified call sites in `game/frame_timing/frame_limiter.cpp` and `game/render/swap_chain_present.cpp` declare it as `int(uint32_t microseconds)`, and their cases expect `0`. FreeBSD's `useconds_t` is 32-bit unsigned. | FreeBSD `usleep(3)`: suspends the thread for the given microseconds, `0` on success, `-1` with `EINTR` if a signal interrupts it. SUSv2 also let `usleep` fail with `EINVAL` from 1,000,000 on; FreeBSD and POSIX.1-2008 (which removed `usleep`) have no such limit. | `sceKernelUsleep` is imported by `eboot` and `libSceFios2.elf`, `usleep` by `eboot`. Implemented: the host thread sleeps at least the requested time and the call returns `0`; a host signal is not a guest event, so the sleep resumes with the time left (`runtime/syslib/kernel.c`). Open: `usleep` with 1,000,000 or more, which stops by name because no PS4-specific source says whether the guest sleeps or gets `EINVAL`; and the SCE error, if any, for `sceKernelUsleep`, which the guest's sites never check. |
| `sleep(seconds)` (libScePosix) | One unsigned count of seconds, returning `unsigned int`: `unsigned sleep(unsigned seconds)` in POSIX and FreeBSD's `sleep(3)`. No call site in the repository fixes the shape further. | POSIX: suspends the thread until the seconds have elapsed or a caught signal arrives; returns `0` when the time has elapsed, otherwise the unslept seconds; no errors are defined. FreeBSD's libc (`lib/libc/gen/sleep.c`) is a `nanosleep` that rounds the unslept remainder up to whole seconds; above `INT_MAX` it sleeps `INT_MAX` seconds and returns `seconds - INT_MAX` without any signal. | Imported by `eboot`. Implemented: the host thread sleeps at least the requested seconds and the call returns `0`; a host signal is not a guest event, so the sleep resumes with the time left, as for `usleep` (`runtime/syslib/kernel.c`). Open: more than `INT_MAX` seconds, which stops by name because POSIX has no early return without a signal and no PS4-specific source says whether the guest gets the full sleep or FreeBSD's remainder. |
| `nanosleep(rqtp, rmtp)` (libkernel) | `int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)` in POSIX and FreeBSD's `nanosleep(2)`, with FreeBSD amd64's `struct timespec` (two 64-bit fields, seconds and nanoseconds), the layout the `clock_gettime` entry uses. No call site in the repository fixes the shape further. | POSIX: suspends the thread until the requested time has elapsed and returns `0`; a caught signal returns `-1` with `EINTR` and writes the time left through a non-null `rmtp`; nanoseconds outside `[0, 1,000,000,000)` are `-1` with `EINVAL`; `rqtp` and `rmtp` may point to the same object. A negative second count is not defined. FreeBSD's `kern_time.c` (same in stable/9 and main): the `EINVAL` check first, then a zero or negative time returns `0` at once without sleeping; `nanosleep(2)` adds `EFAULT` for a pointer it cannot use. | Imported by `libc.elf`. Implemented: the host thread sleeps at least the requested time and the call returns `0`; a host signal is not a guest event, so the sleep resumes with the time left and `rmtp` is never written, as for `usleep` and `sleep`; the request is copied first, so `rqtp` and `rmtp` may be the same (`runtime/syslib/kernel.c`). Stops by name: the errno cases (no `rqtp`, nanoseconds out of range), since errno is not modelled, as for `clock_gettime`. Open: a negative second count, which stops by name because POSIX does not define it, FreeBSD returns `0` at once and the host answers `EINVAL`, and no PS4-specific source says which answer the guest gets. |
| `scePthreadRwlockInit(rwlock, attr, name)`, `scePthreadRwlockDestroy(rwlock)`, `scePthreadRwlockRdlock(rwlock)`, `scePthreadRwlockWrlock(rwlock)`, `scePthreadRwlockTryrdlock(rwlock)`, `scePthreadRwlockTrywrlock(rwlock)`, `scePthreadRwlockUnlock(rwlock)` (libkernel) | A pointer to the guest's rwlock variable (`Init` also an attribute pointer and a name, as `scePthreadMutexInit`); each returns `int32_t`. No call site in this repository shows these shapes; they come from the public [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h) declarations, supporting evidence for the shapes only, and match FreeBSD's `pthread_rwlock_*`. | POSIX `pthread_rwlock_*`, as FreeBSD libthr (`thr_rwlock.c`) and the kernel's umtx read-write lock (`kern_umtx.c`) implement it: a static initializer (`NULL`) set up on first use and a destroyed rwlock holding `1` (`thr_private.h`); `EINVAL` for a destroyed rwlock; `EBUSY` from the try forms when the lock is not available; destroying does not check whether the lock is held, and leaves an unused initializer in place; unlocking is `EPERM` when another thread holds the write lock or when nobody holds the lock, and readers are counted, not named; a thread that locks again while holding the write lock is not given an error, it waits forever (no owner check in `do_rw_wrlock` or `do_rw_rdlock`). The SCE name returns `0x80020000` plus the error number. | Imported by `eboot` and `libSceFios2.elf` (`Tryrdlock` by `libSceFios2.elf` only; `symbols/imports.csv`). Implemented on a host rwlock that also records the writer and the reader count, which the host does not expose, for the unlock answers (`runtime/syslib/kernel_rwlock.c`). A missing variable answers `EINVAL`, as the mutex calls do; unlocking a never-used initializer is `EPERM`, as for the mutex. Stops by name: `Rdlock` or `Wrlock` while the caller holds the write lock (the system would wait forever, the host answers `EDEADLK`; neither is useful to return), and an attribute object passed to `Init` (no imported call creates one, so its contents are not established; an empty attribute variable means the defaults, as in libthr). `scePthreadRwlockTimedrdlock` and `Timedwrlock` are under [Timed locks](#timed-locks-scepthreadmutextimedlock-scepthreadrwlocktimedrdlock-scepthreadrwlocktimedwrlock-issue-34). Not part of this entry: the `libScePosix` names. |
| `__stack_chk_fail(void)` (libkernel) | No arguments, no result: `void __stack_chk_fail(void)`, and it never returns (LSB; FreeBSD libc declares it `__dead2`). Compiler-generated code calls it, not application code. | LSB Core: the function that called it is aborted with a message that a stack overflow was detected, and the program then exits. FreeBSD libc logs "stack overflow detected; terminated" and ends the process with `SIGABRT`. | Imported by `eboot`, `libSceFios2.elf` and `libc.elf` from `libkernel` (`symbols/imports.csv`). Implemented as a stop by name (`rt_fatal`, which aborts): ending the program is the whole guest-visible contract, so the entry counts as implemented (`runtime/syslib/kernel.c`). Not covered: the PS4's own message or crash report, which the guest cannot observe after it ends; and `__stack_chk_guard`, a separate data import whose PS4 value is not established (an unbound data import gets a zeroed page, `runtime/boot.c`). |
| `_exit(status)` (libkernel) | One `int`, no result: `void _exit(int status)`, and it never returns (POSIX; FreeBSD's [`_exit(2)`](https://man.freebsd.org/cgi/man.cgi?query=_exit&sektion=2) has the same shape and declares it `__dead2`). No call site in the repository fixes the shape further. | POSIX: the process ends at once; functions registered with `atexit` are not called and open streams are not flushed; the low 8 bits of `status` (`status & 0377`) are available to the waiting parent, and the full value to `waitid` and `SIGCHLD` handlers. FreeBSD `_exit(2)`: the same, the C library's `exit(3)` runs the `atexit` handlers and flushes, then calls `_exit`. | Imported by `libc.elf` from `libkernel` (`symbols/imports.csv`). Implemented: the host's `_exit` with the guest's status, which has the same contract (`runtime/syslib/kernel.c`). Under the boot harness, `rt_exit_observer` records the run's outcome first, as `rt_fatal_observer` does for a stop: `pbboot` reports "exited with code" and the full status, from any guest thread, and still writes the boot record (`runtime/boot.c`). Not covered: what the PS4 shows the player when a game ends this way, which the guest cannot observe after it ends. |
| `scePthreadCondInit(cond, attr, name)`, `scePthreadCondDestroy(cond)`, `scePthreadCondSignal(cond)`, `scePthreadCondBroadcast(cond)`, `scePthreadCondWait(cond, mutex)`, `scePthreadCondTimedwait(cond, mutex, microseconds)` (libkernel) | Pointers to the guest's cond and mutex variables; each returns `int32_t`. The guest fixes the wait shapes: the verified `game/kernel/condition_wait.cpp` declares `Wait` as `int32_t(void *cond, void *mutex)` and `Timedwait` as `int32_t(void *cond, void *mutex, uint32_t timeout_us)`, a relative timeout in microseconds, not POSIX's absolute `timespec`. The public [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h) declares all six with these shapes (`Init` with an attribute pointer and a name, as `scePthreadMutexInit`); it is supporting evidence for the shapes only. | POSIX `pthread_cond_*`, as FreeBSD libthr implements it (`thr_cond.c`): a static initializer (`NULL`) set up on first use; a destroyed cond holds `1` (`thr_private.h`); `EINVAL` for a destroyed cond; `EBUSY` to destroy one with waiters; destroying an unused initializer succeeds and leaves it in place; `EPERM` when the waiter does not own the mutex, for every mutex type, `EINVAL` when the mutex is destroyed (`_mutex_owned`, `thr_mutex.c`); `ETIMEDOUT` when a timed wait expires; a recursive mutex is released completely for the wait and its count restored after (`_mutex_cv_unlock`, `_mutex_cv_lock`). The SCE name returns `0x80020000` plus the error number. | `scePthreadCondWait` and `Timedwait` are imported by `eboot`, `libSceFios2.elf` and `libc.elf`, the other four by `libSceFios2.elf` and `libc.elf` (`symbols/imports.csv`). The guest's call site checks exactly `0x80020001` (EPERM), `0x80020016` (EINVAL) and `0x8002003c` (ETIMEDOUT). Implemented on a host cond on the monotonic clock (`runtime/syslib/kernel_mutex.c`): the timed wait times out once the given microseconds have passed since the call (`0`: at once). A missing cond or mutex variable answers `EINVAL`, as the mutex calls do. Stops by name: an attribute object passed to `Init` (no imported call creates one, so its contents are not established; an empty attribute variable means the defaults, as in libthr). A wait with a recursive mutex releases every level the caller holds and takes them back after, as libthr does: the runtime tracks each recursive mutex's owner and depth, and releases the extra levels itself around the host wait. A wait with a mutex the caller does not own is `EPERM` for every type, as in libthr (see [Mutex ownership](#mutex-ownership-scepthreadmutexunlock-issue-132)). Open: a woken waiter counts until it has taken the mutex back, so a destroy right after a broadcast can answer `EBUSY` while libthr's succeeds; no known call site destroys a cond that way. The `libScePosix` names `pthread_cond_init`, `_destroy`, `_signal`, `_broadcast` and `_wait` (imported by `eboot`) are the same calls with errno values: the executable's call at `0x0247e6d7` passes `(&cond, NULL)` and its caller compares the result with bare error numbers (12, 16, 34, 35). `pthread_cond_timedwait(cond, mutex, abstime)` (imported by `eboot`; no call site in the repository) takes an absolute time in FreeBSD amd64's `struct timespec`, the two 64-bit fields of the `clock_gettime` entry, measured on the cond's clock: `CLOCK_REALTIME` unless an attribute set another (POSIX `pthread_cond_timedwait`; libthr's `cond_init` for no attribute). No imported call can set another: nothing imports `pthread_condattr_*`, and an attribute object stops `Init` by name. libthr's `pthread_cond_timedwait` answers `EINVAL` before touching the mutex for no time, negative seconds and nanoseconds outside `[0, 1,000,000,000)`; POSIX names only the nanoseconds case and leaves the other two undefined, and the runtime answers all three as libthr does. Implemented: the wait ends at the monotonic deadline where the wall clock reaches the time, the wall clock read first so it is never early; a time already passed is `ETIMEDOUT` with the mutex taken back. Open: a change of the wall clock during the wait is not followed, which POSIX's `CLOCK_REALTIME` wait would do. |
| `scePthreadKeyCreate(key, destructor)`, `scePthreadKeyDelete(key)`, `scePthreadGetspecific(key)`, `scePthreadSetspecific(key, value)` (libkernel) | A key is a `pthread_key_t`, an `int` on FreeBSD (`sys/_pthreadtypes.h`); `Create` takes a pointer to the guest's key variable and a destructor (`void (*)(void *)`, may be null), `Getspecific` returns `void *`, the others `int32_t`. These shapes come from the public [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h), supporting evidence for the shapes only, and match FreeBSD's `pthread_key_create`, `pthread_key_delete`, `pthread_getspecific` and `pthread_setspecific`. | POSIX thread-specific data, as FreeBSD libthr implements it ([`thr_spec.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_spec.c), the same in stable/9): a key is its slot's index plus 1, and `Create` takes the first free slot; `Delete` of a key that is out of range or free is `EINVAL` and calls no destructor; `Setspecific` of such a key is `EINVAL`, `Getspecific` answers `NULL`, as it does for a key the thread has not set since the key was created (each slot counts its creations). When a thread ends, each non-null value is cleared and, if its key is still the one it was set under, the destructor is called with it, in rounds while values remain (`_thread_cleanupspecific`). The SCE name returns `0x80020000` plus the error number. | Imported by `libc.elf` from `libkernel` (`symbols/imports.csv`). Implemented with the runtime's own key table and a host thread-local value array per guest thread (`runtime/syslib/kernel_key.c`); a thread created by `scePthreadCreate` runs its destructors on its guest stack when its entry returns (`runtime/syslib/kernel_thread.c`). A missing key variable for `Create` (unchecked in libthr) answers `EINVAL`, as the mutex calls do. Not established: the PS4's number of keys and of destructor rounds. POSIX guarantees at least 128 keys (`_POSIX_THREAD_KEYS_MAX`) and 4 rounds (`_POSIX_THREAD_DESTRUCTOR_ITERATIONS`), FreeBSD gives 256 and 4 (`<pthread.h>`); the runtime gives what POSIX guarantees and stops by name past it: a 129th live key, and a thread whose destructors still leave values after the 4th round. The main thread's values get no destructors, since it never ends through a thread exit here. |

| `sceSysmoduleLoadModule(id)`, `sceSysmoduleIsLoaded(id)`, `sceSysmoduleUnloadModule(id)` | One 32-bit system module ID; each returns `int32_t`. libSceSysmodule uses the complete 32-bit ID; the public OpenOrbis enum is narrower. | Load returns `0x805a1000` when `(id & 0x7fffffff) == 0`; query and unload return that status for ID 0, before locks or state changes. Evidence and library hash: [system-module evidence](#module-loading-at-run-time-libscesysmodule). Other native errors are not established. | Declared 16-bit stub IDs retain the existing load/query/unload policy; other full-width requests stop by name before lookup, without aliasing a low ID. See "Module loading at run time" below. |
| `time(tloc)` (libSceLibcInternal) | `time_t time(time_t *tloc)`, the ISO C declaration; `time_t` is a signed 64-bit integer on FreeBSD amd64. | ISO C 7.27.2.4: returns the calendar time, and also stores it in `*tloc` when `tloc` is not null; POSIX and FreeBSD [`time(3)`](https://man.freebsd.org/cgi/man.cgi?query=time&sektion=3) make it the seconds since the Epoch (UTC). FreeBSD's implementation ([`lib/libc/gen/time.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libc/gen/time.c)) returns the seconds of `clock_gettime(CLOCK_SECOND)` since FreeBSD 9, and `(time_t)-1` only when that call fails. | Imported by `libSceFios2.elf` from `libSceLibcInternal` (`symbols/imports.csv`); the executable's own `time` import is bound to the bundled `libc.elf`, whose `time()` also asks for clock 13 and returns its seconds (see "`clock_gettime` and `sceKernelClockGettime`" below). Implemented from the same clock as `clock_gettime(13)`: the host's wall clock in whole seconds, returned and stored through a non-null `tloc` (`runtime/syslib/kernel_time_of_day.c`). No `-1` path: the runtime's `CLOCK_SECOND` read cannot fail. Not established: no call site in this repository shows how `libSceFios2.elf` uses the result; an invalid non-null `tloc` is undefined behaviour and not modelled. |
| `gmtime(timer)`, `gmtime_s(timer, result)` (libSceLibcInternal) | `struct tm *gmtime(const time_t *)` and C11 Annex K's `struct tm *gmtime_s(const time_t *restrict, struct tm *restrict)`: `libSceFios2`'s calls (`+0x2cd73`, `+0x2cdf4`, `+0x2e494`) pass a `time_t` on the stack, and the `gmtime_s` callers test the answer for null, so it is the Annex K form, not Microsoft's error-number one. The PS4's `struct tm` is ISO C's nine `int`s, 36 bytes, without FreeBSD's `tm_gmtoff` and `tm_zone`: the same module's time-zone helper (`+0x2cd15`) copies exactly `0x24` bytes into a local `struct tm` that `mktime` normalizes in place, and its next local starts `0x28` bytes later, so a 56-byte FreeBSD `struct tm` would not fit. | ISO C 7.27.3.3 and [N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) K.3.8.2.3: the UTC broken-down time; `gmtime` answers a pointer to the library's own object, which a later call may overwrite, `gmtime_s` fills the caller's structure and answers its address. | Imported by `libSceFios2.elf` (`symbols/imports.csv`). Implemented with the host's `gmtime_r`, writing the nine `int`s and nothing more; `gmtime`'s object is one per thread (`runtime/syslib/libc_internal.c`). Stops by name: no time or no result (undefined in ISO C; Annex K's constraint handler is not modelled), and a time whose year does not fit an `int` (FreeBSD sets `errno`, which is not modelled for this library). `localtime` and `mktime` stay unimplemented: they need the console's time zone, which is not established. |
| `printf(format, ...)`, `vprintf(format, ap)`, `vsnprintf(buffer, size, format, ap)` (libSceLibcInternal) | The ISO C declarations. The module's 62 `printf` calls pass constant formats with `%s` conversions; its 33 `vsnprintf` calls pass formats through from their callers. | ISO C 7.21.6.3, 7.21.6.10 and 7.21.6.12. | Imported by `libSceFios2.elf` (`symbols/imports.csv`). The guest calls with the System V convention the host uses, so its `va_list` is the host's and the host's functions format; `printf` and `vprintf` write to the host's standard output (`runtime/syslib/libc_internal.c`). Each format is checked first, and what the runtime cannot hand to the host unchanged stops by name: `%lc` and `%ls` (the PS4's `wchar_t` is not established), `%n` (the host's fortified `printf` refuses it from writable memory, and no call is known to pass it), any conversion ISO C does not define, no format, and no buffer for a non-zero size. Not implemented: `fflush` (its stream is the `_Stdout` data import, which the runtime does not provide). |

## Module loading at run time (`libSceSysmodule`)

**Observed in our target**: the executable imports `sceSysmoduleLoadModule`, `sceSysmoduleIsLoaded`
and `sceSysmoduleUnloadModule` from `libSceSysmodule`; its shipped file I/O module imports the load
and unload calls too. Neither imports `sceKernelLoadStartModule` or another loader entry
(`symbols/imports.csv`), so a module is asked for by its system module id and nothing else. Five of
the seven modules the title ships are listed with action `stub` in `game/modules.csv`, which records
that the executable imports nothing from them; they are not mapped.

**Documented publicly**: the OpenOrbis toolchain's id list (`include/orbis/_types/sysmodule.h`,
after psdevwiki's "Libraries" page) names the ids of those five: `ORBIS_SYSMODULE_FACE` `0x0038`,
`ORBIS_SYSMODULE_SMART` `0x0039`, `ORBIS_SYSMODULE_S3DCONVERSION` `0x0086`, `ORBIS_SYSMODULE_HAND`
`0x0093` and `ORBIS_SYSMODULE_HEAD_TRACKER` `0x00cb`. They are recorded in the `sysmodule` column
of `game/modules.csv`; the runtime itself names no module (`tools/check_agnostic.py`):
`tools/boot.py` passes each stub to `pbboot` as `--stub-module ID:NAME`, and the run's status
(`stub_modules` in `symbols/boot.json`) says which of them the guest loaded.

**Inferred**: *if* nothing we map imports from a stub (as `game/modules.csv` records for the
executable), answering the load without mapping the module cannot leave a call unbound; an import
it would have satisfied still shows up unbound in `symbols/imports.csv` and stops the boot by name. Whether the executable's sites check the result
beyond success, and which ids they pass, is not yet observed: the boot stops before the first load
today.

**System modules the runtime provides**: a module the title does not ship but loads by id (it is part
of the system) can be listed with action `runtime` once the runtime implements the functions the
executable imports from it. `tools/boot.py` passes it like a stub, so a load is reported as done,
and its imports bind to the runtime's entries by name. The first is `libSceAppContent` (`0x00b4` in
the OpenOrbis id list), below.

**A second load** of a loaded module succeeds and is counted; each unload takes one load back, and
the module is loaded until the last one (maintainer's ruling of 2026-11-10; shadPS4's
`sysmodule_internal.cpp` counts loads the same way, facts only). The message and commerce dialogs
load their module on every open, so a refused second load would stop the second dialog.

**Unknown**: the error values (a module not loaded, an id the system does not know). Each is a
named fatal until a console measurement or a guest call site settles it.

The runtime validates the complete 32-bit module ID before consulting its configured
module list. A wider ID such as `0xabcd7f01` does not alias the 16-bit ID `0x7f01`.
Unsupported IDs stop by API name before looking up or changing a module record.

In `libSceSysmodule` (SHA-256
`43c456e00b59b2bd1303023cbcb463f50c0647854b4837468f8fc6c290b55c23`),
these argument checks precede the lock and the delegated module operation:

- `sceSysmoduleLoadModule` returns `0x805a1000` when `(id & 0x7fffffff) == 0`,
  covering IDs 0 and `0x80000000`.
- `sceSysmoduleIsLoaded` and `sceSysmoduleUnloadModule` return the same status for
  ID 0. Their handling of `0x80000000` is delegated and remains unknown.

These early errors leave every configured module record unchanged, including an
explicitly configured zero-ID record. The original load validator distinguishes
IDs `0x80`, `0xb0` and `0xb8` from other complete 32-bit IDs before its category
checks; narrowing the argument would select a different path.

The library functions are at module offsets `0x410` (load), `0x580` (query),
`0x520` (unload) and `0x480` (load validation). Their complete instruction spans
and unwind entries establish the boundaries. These observations describe this
library version; they do not establish behavior on every firmware. The wrappers
also propagate lock failures and delegated statuses. Load remaps `0x80020064` to
`0x80020016` after a successful lock. Inner loading, category predicates, reference
counts and release behavior remain unknown; the runtime stops on requests that
would need those answers.

The module-ID tests check the early numeric errors, complete-ID diagnostics,
unchanged module records on rejection, wider IDs against loaded and unloaded
records, and the configured 16-bit module lifecycle. Synthetic configured IDs
exercise the runtime's supported behavior; they do not prove acceptance by the
original library's validator.

## `scePthreadAttrGetaffinity`: the startup consumer and missing evidence

An affinity mask identifies the CPUs on which a thread may run. The current startup stop is a
query for the main thread's mask. Returning success without the correct output would change the
CPU count that the bundled C library stores; it would not implement the query.

**Observed in the hash-pinned bundled `libc.elf`:** the function at module offset `0x81a80`
initializes a thread attribute, obtains the current thread, and copies its attributes. If those
operations succeed, its call at `libc.elf+0x81ad0` passes the address of that attribute variable
in RDI and the address of a zero-initialized eight-byte stack slot in RSI. This is a two-pointer
interface, not FreeBSD's three-argument interface with an explicit buffer size. The return site
is `libc.elf+0x81ad5`, matching the genuine boot diagnostic. The caller does not check the affinity
query's return value: after destroying the attribute it reads the slot, counts its set bits
across 64 bits, and stores a 32-bit count. This establishes the consumer's memory requirements,
not the real query's write width, the actual mask or its complete error contract.

The original consumer also ran through the existing harness on nine private constructed
fixtures with scripted imports, using each compiler's loader. It produced the expected counts
for zero, low-bit, high-bit and all-bit masks, and exercised both earlier failure branches.
A scripted nonzero affinity return with a supplied mask still produced that mask's count,
corroborating that this caller ignores the return. These are original-consumer observations
under synthetic import behavior, **not** observations of PS4 affinity, recorded gameplay,
complete instruction-trace verification or an original-versus-replacement comparison. No
function status changes follow from them.

**Public supporting evidence:** the OpenOrbis homebrew header
[`libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/0a1aaf9dd4a92695538bdeb09fb056d06dd11725/include/orbis/libkernel.h#L463)
declares a signed 32-bit return and an attribute pointer plus an output pointer. Its comment
explicitly leaves the mask type unfinished; the declaration is corroboration, not a measured
PS4 contract. The [FreeBSD 9.3 affinity manual](https://man.freebsd.org/cgi/man.cgi?query=pthread_attr_affinity_np&sektion=3&manpath=FreeBSD+9.3-RELEASE)
describes retrieving a stored attribute mask, success/error returns and invalid attributes, but
its API includes a size argument absent at this target call. Its errors and CPU-set layout
cannot simply be assigned to the SCE interface.

**What the runtime answers (maintainer ruling, 2026-11-10).** The console's own mask is still not
measured. The maintainer ruled that a thread with no mask set by the guest answers **all ones**,
which is what `scePthreadGetaffinity` already answers. `scePthreadAttrGetaffinity(attr, mask)`
therefore writes eight bytes:
- the mask set on the attribute with `scePthreadAttrSetaffinity`, or copied from a thread by
  `scePthreadAttrGet`, which copies the thread's mask (from its creation attribute or
  `scePthreadSetaffinity`);
- otherwise `UINT64_MAX`.

A variable holding no attribute is `EINVAL`, as for the setter. No variable for the mask stops by
name. Host CPU availability is not consulted. evill33t's #239 did the groundwork on this call; it
took its value from the host, which the ruling replaced.

**How the boot gets past it without a value.** These are observations from static analysis of the
hash-pinned `eboot.elf`, `libc.elf` and `libSceFios2.elf` (Ghidra 12.0.3 with GhidraOrbis, issue
#34), not guarantees:
- The probe's count is stored at `libc.elf+0xd1c54`. That lies past the RW segment's file size, so
  it starts zero.
- The probe returns nothing. It is reached only by a tail jump from the initializer at `+0x5fad0`,
  which the module's `DT_INIT` runs.
- The count's only reader found is the exported `std::thread::hardware_concurrency()` at
  `+0x5fac0`.
- No reference to that function was found in those three objects, none of them imports it, and
  none imports `sceKernelDlsym`.

So `game/module_hooks.csv` replaces two functions before any guest code runs:
- the probe (`+0x81a80`, 128 bytes) with `return`, so the affinity query is never made and the
  count stays zero;
- `hardware_concurrency` (`+0x5fac0`, 7 bytes) with `stop`, so a caller ends the run by name
  instead of reading a count nobody established.

`pbboot --module-hook` first compares each function's first bytes, as the mapped and relocated
module holds them, with the ones the file gives (`first_bytes`), and refuses the run before any
guest code runs if they differ. It then writes each as a five-byte relative jump to a stub in the module's slot. A
`return` stub leaves every register as the caller had it. A `stop` is reported like an
unimplemented import: the status names the hook and its caller in `stopped_by_hook`.
The call itself is implemented (above), but the probe never reaches it while this hook is in
place, so the count stays zero. Whether to drop the hook and let the probe count the ruled mask is a
separate decision.

The hook guards calls only. A direct read of `+0xd1c54` that the analysis missed would see 0, not
a measured count. The offsets fit only the pinned `libc.elf` (`target.sha256`); another build fails
the byte check by name instead of being patched.

## Clock IDs and platform differences

The only clock ID confirmed by a decompiled game call in this repository is `4`, used with `sceKernelClockGettime` and identified there as `CLOCK_MONOTONIC`. This does not prove that the game passes `4` to the imported `clock_gettime` wrapper. POSIX defines clock identifiers symbolically; their numeric values are implementation-specific. Linux adds clocks such as `CLOCK_MONOTONIC_RAW` and `CLOCK_BOOTTIME`; FreeBSD has its own clock set, including `CLOCK_UPTIME` variants. Their names and numeric values are not interchangeable evidence for PS4 behavior.

`CLOCK_MONOTONIC` represents elapsed time from an unspecified starting point and is not the wall clock. The precise effects of suspend, rate correction, and resolution vary by implementation. `gettimeofday` and `CLOCK_REALTIME` represent wall time tied to the Epoch and can move when system time changes. No source examined here establishes that the game asks the imported `clock_gettime` wrapper for `CLOCK_REALTIME`, `CLOCK_MONOTONIC_RAW`, `CLOCK_BOOTTIME`, or another ID.

## Network byte-order helpers

The four `libSceNet` imports `sceNetHtonl`, `sceNetHtons`, `sceNetNtohl`, and `sceNetNtohs` take and return 32-bit or 16-bit unsigned integers, respectively. Public PSP2SDK API declarations provide those widths for the SCE names. The standard byte-order contract converts host order to network (big-endian) order for `hton*`, and reverses that conversion for `ntoh*`; this is documented by the [FreeBSD byte-order manual](https://man.freebsd.org/cgi/man.cgi?format=html&query=htons&sektion=3). Paleblood's guest ABI is AMD64, so these four conversions are byte reversals. Their implementations do not call host networking APIs or depend on host socket state.

This evidence supports only the four integer conversions. It says nothing about `libSceNet` socket, resolver, epoll, offline-state, or error behavior; those imports remain unresolved until their own contracts are established.

## IPv4 text conversion (`sceNetInetPton`, `sceNetInetNtop`, issue #41)

The executable imports `sceNetInetPton` from `libSceNet`. Target call-site inspection found four unconditional call instructions (at `0x0085785e`, `0x011c7a7b`, `0x02f984f6`, and `0x02df348d`); each passes address family `2` and supplies the result as an IPv4 address. The consumers use a positive return as success. This establishes Bloodborne's observed call shape, not every address family supported by the PS4 library. The import thunk and pointer-table references are not additional call sites.

For the observed AF_INET path, the runtime uses the documented BSD [`inet_pton(3)` contract](https://man.freebsd.org/cgi/man.cgi?query=inet_pton): parse dotted-decimal IPv4 text into four network-order bytes, return 1 for valid input and 0 for malformed input. This conversion does not open a socket or perform network I/O. Other target address families and null-pointer error behavior remain unresolved, so the runtime emits a named diagnostic for those inputs instead of forwarding host-specific errors.

`sceNetInetNtop(family, source, destination, size)` is the reverse, BSD [`inet_ntop(3)`](https://man.freebsd.org/cgi/man.cgi?query=inet_ntop) for AF_INET: the four network-order bytes as dotted-decimal text, answering the destination. The executable's one call (`0x02f982df`) passes family `2`, an address from `sceNetHtonl` and a 16-byte buffer (enough for `255.255.255.255` and its terminator), and tests the answer for null. A buffer too small for the text answers null with `ENOSPC` in `errno` under BSD; the library's `errno` (`sceNetErrnoLoc`) is not modelled, so that case stops by name, as do other families and missing pointers.

## `getpagesize`: observed, inferred, unknown

**Ruling (maintainer, 2026-11-10):** both entries answer **16384** (`runtime/syslib/kernel.c`).
Every source we can cite agrees on 16 KiB: shadPS4's kernel, the bloodborne-native port family and
the Astrr00 port (facts only), the `p_align` of every target segment, and the runtime's own 16 KiB
memory granule. The record below is why it is a ruling and not a measurement.

**Observed in our target** (read-only; the eight images verify against `target.sha256` and
`target.image.sha256`):

- `symbols/imports.csv` has exactly two `getpagesize` rows - `eboot` from `libScePosix` and
  `libc.elf` from `libkernel`. No `sysconf` and no quad-page call is imported, so this is the only
  page-size query the guest has.
- All sixteen `PT_LOAD` segments of those eight images have `p_align` `0x4000`, and in every one
  `p_offset - p_vaddr` is exactly `0x4000`.
- Three guest call sites share one shape: `0x00e04168`, `0x00e04939` and `0x00e05cf9`, each inside
  functions `0x00e04140`, `0x00e045c0` and `0x00e05a80`. Each is a lazy initialiser: a guard tests
  `0x05859ec0`, the call is made, the result is `cltq`-extended, `blsr %rax,%rcx` with `jne` rejects
  it unless it is a power of two, and the value is cached in `0x05859ec8` beside three fixed
  siblings (`0x10000`, `0x40000`, `0x200000`).
- `0x05859ec8` has exactly five cross-references in the executable: the three writes above and two
  reads, all inside those same three functions. None of them compares the cached value against a
  constant, and no other function reads it.

**Inferred, and the premise each inference needs**:

- The ELF ABI takes `p_align` of a loadable segment to be a multiple of the system page size, and
  requires `p_offset` and `p_vaddr` to be congruent modulo the page size. *If the PS4 loader follows
  both rules for these segments*, a page size larger than `0x4000` could not map them at their
  fixed addresses, while any power-of-two divisor of `0x4000` could.
- Restricting that to `0x1000`, `0x2000` and `0x4000` adds a second assumption - that the page size
  is at least 4096 - which is ours, not observed. The guest's own `blsr` check is consistent with
  every power of two and selects none of them.
- So `p_align` bounds the candidate set under our premises; it does not measure the value the kernel
  returns, and none of the three candidates is established.

**Unknown**: the number the PS4 reports. Stock FreeBSD on AMD64 uses 4096 (`PAGE_SHIFT 12`,
`sys/amd64/include/param.h`), and Sony's open-source page confirms a FreeBSD kernel but publishes
the upstream sources rather than the console's modifications, so that value describes the ancestor
and not the target. Our own loader does not depend on a page size either: it copies `PT_LOAD`
segments at byte-exact `p_vaddr` into one image buffer (`runtime/loader.c`, `rt_elf_map`) and uses
`p_align` only for TLS, and the `4096`s in `runtime/boot.c` are host allocation sizes. What would
confirm the ruling: one observed `getpagesize()` or `sysconf(_SC_PAGESIZE)` result on a console.

## Network connection state (`libSceNetCtl`, issue #41)

The executable imports exactly six functions from `libSceNetCtl` and neither `sceNetCtlInit` nor
`sceNetCtlTerm` (`symbols/imports.csv`), so the runtime answers the connection queries itself and the
game takes its offline paths. The public declarations of the same API family on PS Vita
([`psp2/net/netctl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/net/netctl.h)
in vitasdk/vita-headers, and the PSP2SDK header reference) give the shape of the calls: a
state query through an `int *`, an information query by an `int` code into a structure pointer,
one NAT query through a structure pointer, and a callback check that takes nothing and returns `int`.
Vita is a different operating system, so those declarations are evidence for the shape only and their
numbers are never proof of a PS4 value. Facts from other runtimes are used only where a row cites
them (the "not connected" value); elsewhere the guest's own call sites decide. They have settled the
state numbers (0 is not up, 3 is connected) and the callback keying (an id handed back, not a state),
and everything they do not reach - error codes, structure layouts, the offline answers - stays a
fatal that names the question and the evidence that would close it.

| Function | Arguments and representation | Publicly documented result and errors | Evidence and open questions for PS4 |
|---|---|---|---|
| `sceNetCtlGetState(int *state)` | One pointer to a 32-bit `int`, by the Vita-era declaration. Writes the state the system is in. | The Vita-era family names the disconnected state first in its state enum, so 0 there. Public sources give no error return for a query that succeeds. | Implemented: an offline runtime reports the disconnected state and succeeds. The guest fixes the numbers for us - one site tests the returned slot against 0 at `0x00dfd260` to decide whether to keep waiting, another bounds it with `cmp $0x3; ja` before a four-entry table at `0x020413f3` (so the states it can see are {0, 1, 2, 3}), and equality with 3 is the connected branch at `0x010b839f` and in its own callback at `0x010b45e5`. Still open: what 1 and 2 mean, the PS4 enumerator names, and the error value for a null state pointer (fatal). |
| `sceNetCtlCheckCallback(void)` | No arguments; returns `int`, by the Vita-era declaration. Runs the callbacks made due by a state change. | No public source describes the return value at all, only the declaration. | Implemented as 0, the one value both candidate contracts give while nothing is due: the runtime never leaves the disconnected state, so no callback runs. Which reading is the PS4's (a success code, or the number of callbacks it ran)? It only matters once a state change is possible. |
| `sceNetCtlRegisterCallback(void *callback, void *arg, int *cid)` | A callback pointer, an argument pointer and an out-parameter `int *cid`, returning `int`. The guest's own call at `0x010b8352` passes a function in the image, `this`, and `lea -0x78(%rbp)`, a slot it never writes, then reads that slot back at `0x010b8428` into its object at `+0x130`. | Vita-era registration is keyed by a callback id the caller receives back (`func, arg, int *cid`); the guest's sites show the same shape on PS4. The two public Vita-era sources disagree about the callback's return type, so the pointer stays opaque and no signature is modelled for it - the guest's callback at `0x010b45e0` does show it being dispatched as (state, arg). | Implemented: each registration gets a fresh id written through the caller's pointer, numbered from 1 so an id can never look like an unwritten slot, and nothing is ever dispatched while offline. Open: the error values for a null callback or a null id slot, the table's capacity, and whether re-registering the same callback replaces or adds (we add, being keyed by id). |
| `sceNetCtlUnregisterCallback(int cid)` | One `int`: the id a previous register call handed back. The guest passes the field it stored from that out-parameter (`0x010b8f8d`). | The Vita-era form takes the callback id `int cid`, which the sites match. | Implemented for an id we handed out: remove it and succeed. Fatal for an id we never handed out, or one already removed - no public source gives the PS4 answer and the guest's own paths never reach it. |
| `sceNetCtlGetInfo(int code, void *info)` | Claimed as an `int` code and a structure pointer, by the Vita-era `sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info)`; OpenOrbis `NetCtl.h` and shadPS4's `netctl.h` name code 14 `ORBIS_NET_CTL_INFO_IP_ADDRESS`. | shadPS4's `sceNetCtlGetInfo` answers `0x80412108` (not connected) for every code while the console is not connected, before writing anything. | Offline: code 14, the only code the executable asks for (`0x00dfd2b9`, `0x00dfd445`, each printing the buffer only on a non-negative answer), answers "not connected" and writes nothing. Other codes stop by name (a disconnected console may still describe its device or link; no source settles it), and so does a missing structure (PS4Recomp checks it first, shadPS4 the connection first). |
| `sceNetCtlGetNatInfo(void *nat_info)` | `{ size, STUN status, NAT type, mapped address }`, four 32-bit fields, 16 bytes; "not connected" is `0x80412108` (PS4Recomp's `libSceNetCtl`, used as facts). | The guest writes 16 at offset 0 (`0x01889417`), reads back only offset 8, the NAT type, and only on 0 (`0x01889436`); on any other answer it takes NAT type 1 itself (`0x0188942d`). | Offline: answers "not connected" and writes nothing, so the guest's own fallback applies. A size other than 16 and a missing structure stop by name. |

`0` is the success value, the convention public SCE-family documentation states for these calls (the
`@return 0 on success` lines in `psp2/error.h` of vita-headers). libSceNetCtl's own error codes have
no public source, so no path in `runtime/syslib/netctl.c` returns one: a case that would need an error
value stops the boot by name instead.

## Thread attribute detach state (`scePthreadAttrSetdetachstate`, issue #34)

`eboot` and `libSceFios2.elf` import `scePthreadAttrSetdetachstate` from `libkernel`, and `eboot`
imports `pthread_attr_setdetachstate` from `libScePosix` (`symbols/imports.csv`). Each source below
establishes a different part:

- **What the call does:** POSIX `pthread_attr_setdetachstate`. A thread created with the attribute
  starts detached for `PTHREAD_CREATE_DETACHED` and joinable for `PTHREAD_CREATE_JOINABLE`.
- **Invalid detach state:** POSIX and FreeBSD's `pthread_attr(3)` both make any other value
  `EINVAL`.
- **Invalid attribute variable:** POSIX leaves a null or uninitialised attribute undefined and only
  recommends `EINVAL`, and the FreeBSD manual lists no error for it. The behaviour comes from
  FreeBSD libthr's `_pthread_attr_setdetachstate` (`lib/libthr/thread/thr_attr.c`), which returns
  `EINVAL` when the variable is null or holds no attribute object.
- **The numbers:** FreeBSD's `<pthread.h>` defines the two states as `0` and `1`. The OpenOrbis
  homebrew SDK header (`include/orbis/_types/pthread.h`) defines `ORBIS_PTHREAD_CREATE_JOINABLE` and
  `ORBIS_PTHREAD_CREATE_DETACHED` with the same values. That header follows FreeBSD's text, so it
  corroborates the numbers rather than measuring them independently.

As with the mutex functions (`runtime/syslib/kernel_mutex.c`), the SCE name returns `0x80020000`
plus the error number, and the POSIX name returns the number itself.

Still open: the detach state a fresh attribute starts with, and the one `scePthreadAttrGet` copies
from a running thread. Both are values the system supplies. POSIX makes joinable the default, but
nothing we can cite confirms what the PS4 reports. In FreeBSD libthr, `_pthread_create`
(`thr_create.c`) uses that same default (`_pthread_attr_default`) when it is given no attribute
variable or an empty one. The runtime treats both cases as the unknown default, so the outcome, a
fatal, does not depend on the PS4 matching libthr there.

How the runtime keeps that open:

- The attribute records a state only when one is set (`has_detach_state`). `scePthreadAttrGet`
  replaces the whole attribute, as libthr's `pthread_attr_get_np` does, so a state set before the
  copy is dropped.
- By convention, thread creation reads the state through `rt_thread_attr_detach_state`
  (`runtime/include/runtime/process.h`) and never from the field. A fresh attribute's field holds
  zero, which is the joinable value, so reading the field directly would assume joinable silently.
- For no variable, an empty variable, or a state nobody set, the reader stops the process
  (`rt_fatal`), names the call that asked, and says which of those cases it hit.

What would settle it: an observed `scePthreadAttrGetdetachstate` result on a console, for a fresh
attribute and for one filled by `scePthreadAttrGet` from the main thread. Guest call sites would
also settle it: a thread created without a detach state being set, then joined or detached
(`scePthreadJoin`, `pthread_join` and `scePthreadDetach` are imported).

## POSIX names of the thread self and attribute calls (`libScePosix`, issue #34)

`eboot` imports `pthread_self`, `pthread_attr_init` and `pthread_attr_destroy` from `libScePosix`
(`symbols/imports.csv`), next to the `libkernel` SCE names the runtime already implements. They
are the same calls with the POSIX error convention, as for the mutex and detach-state calls:

- `pthread_self` has no error return (POSIX), so it answers what `scePthreadSelf` answers: the
  calling thread's own variable value. With no guest thread registered on the host thread it stops
  by the name the guest used.
- `pthread_attr_init` allocates the attribute object and stores it in the variable; `ENOMEM` when
  it cannot (POSIX; FreeBSD libthr's `_pthread_attr_init`). `pthread_attr_destroy` frees it and
  empties the variable; `EINVAL` for a missing or empty variable (libthr's
  `_pthread_attr_destroy`). The POSIX names return the error number itself, the SCE names
  `0x80020000` plus it, and an object made under one name is freed under the other.
- A missing variable passed to `pthread_attr_init` is undefined in POSIX and unchecked in libthr.
  The runtime answers `EINVAL` under both names, as the SCE name already did; no PS4 source or
  call site says otherwise.

## Mutex ownership (`scePthreadMutexUnlock`, issue #132)

A mutex belongs to the thread that locked it. FreeBSD libthr (`thr_mutex.c`, the same in stable/9
and main) checks that for every mutex type:

- Unlocking a mutex the caller does not own is `EPERM`, whether it is free or another thread holds
  it (`mutex_unlock_common`); so is a condition wait with it (`_mutex_owned`).
- The owner locking again gets `EDEADLK` for the error-checking (default) and adaptive types, and
  waits forever for the normal type, on purpose (`mutex_self_lock`). Its trylock is `EBUSY` for all
  three (`mutex_self_trylock`). A recursive mutex counts one more level.

The host (glibc) checks the owner only for its error-checking and recursive types, and its adaptive
type waits forever when the owner locks again. So the runtime makes every non-recursive guest
mutex an error-checking host mutex, and keeps the guest type for its own answers
(`runtime/syslib/kernel_mutex.c`):

- `scePthreadMutexUnlock`, `pthread_mutex_unlock` and the condition waits answer `EPERM` for a
  caller that does not own the mutex, for the normal and adaptive types too.
- The adaptive type locked again by its owner answers `EDEADLK`, as in libthr.
- The normal type locked again by its owner stops by the name the guest used
  (`scePthreadMutexLock` or `pthread_mutex_lock`): the system would wait forever, the host
  answers `EDEADLK`, and neither is useful to return.

The SCE names return `0x80020000` plus the error number, the `libScePosix` names the number
itself. One difference left: the adaptive type no longer spins before it sleeps, as libthr's and
glibc's do; that changes timing only, not an answer.

## Timed locks (`scePthreadMutexTimedlock`, `scePthreadRwlockTimedrdlock`, `scePthreadRwlockTimedwrlock`, issue #34)

Each takes the lock variable and the most it may wait, in microseconds from now (OpenOrbis
`libkernel.h` declares an `OrbisKernelUseconds`; shadPS4 binds the three names to libthr's
relative-time calls, `pthread_mutex_reltimedlock_np` and `pthread_rwlock_reltimed*lock_np`, facts
only). The executable's lock wrappers take them for any timeout but -1, which uses the untimed
call: `0x02484119` (mutex) tells `EDEADLK`, `EINVAL`, `EAGAIN` and `ETIMEDOUT` apart, and
`0x0247f681` and `0x0247f411` (rwlock) `EDEADLK`, `EINVAL` and `ETIMEDOUT`.

**What the runtime does:**
- The wait ends with the lock, or with `ETIMEDOUT` once the time has passed, on the monotonic clock
  (as for `scePthreadCondTimedwait`).
- **The owner asking again.** With a timeout, libthr's `mutex_self_lock` sleeps the whole time and
  answers `ETIMEDOUT` for the error-checking, adaptive and normal types; a recursive mutex takes
  one more level. The kernel's umtx rwlock has no owner check, so a thread holding the write lock
  that asks again waits out the time too (`do_rw_rdlock`, `do_rw_wrlock`). The host would answer
  `EDEADLK` at once, so the runtime sleeps the time itself.
- A destroyed lock or a missing variable is `EINVAL`, and the static initializers are set up on
  first use, as for the untimed calls.

## Direct memory (`libkernel`, issue #34)

`eboot` imports `sceKernelGetDirectMemorySize`, `sceKernelAllocateDirectMemory`,
`sceKernelMapDirectMemory` and `sceKernelReleaseDirectMemory` (`symbols/imports.csv`). Its first
use is the startup heap, at `0x02481a80`:

1. It asks for the size.
2. It allocates `0x7400000` bytes, aligned to `0x200000`, anywhere from 0 to that size, with memory
   type 0.
3. It maps the range read/write (protection 3) with flags 0, so the system picks the address, at
   the same alignment.
4. If the map fails, it releases the range.
5. On any failure it logs an error and builds its heap from a null base, so these calls have to
   succeed.

The signatures match the OpenOrbis homebrew SDK's `libkernel.h`, as supporting evidence only.

- **The pool.** Direct memory is a host memory file. An allocation is a range of it and a map is a
  host mapping of that range, so two maps of one range see the same bytes.
- **The size.** `sceKernelGetDirectMemorySize` answers the size of that pool (5 GiB), a choice of
  the runtime. The PS4's value is not established here. A title that sizes its allocations from the
  answer sizes them from the runtime's pool.
- **Allocation.** It takes the lowest start at or above the search start that fits before the
  search end.
- **GPU access.** The executable maps with protection 3 and `0x33`, CPU and GPU read/write
  (`0x10` and `0x20` in the public SDK list). The host mapping takes the CPU bits.
- **Where a map goes.** Without an address hint, the system chooses:
  - **A GPU-visible map** goes at a fixed base (`0x2000000000`) plus its offset in the pool. Every
    such address is then a fixed distance from the same byte of the pool, which is how the
    renderer's GPU will reach guest memory: it gets the whole pool as one buffer
    (`runtime/gpu_memory.h`, `docs/RENDERER.md`).
  - **Other maps** go anywhere free in the map window above that region.
  - **A second GPU-visible map of a range** would need the same place, so it stops by name. A
    boot doesn't make one: every range it maps, it maps once.
  - **With a hint,** a map goes where the hint says (below).
- **What stops by name.** Every case whose PS4 answer is not established stops with the function's
  name:
  - lengths, starts or alignments that are not multiples of 16 KiB (the granularity the
    executable's calls use);
  - memory types other than 0, 3 and 10;
  - map flags other than no-overwrite (`0x80`, shadPS4's kernel `memory.h`). No-overwrite keeps a
    map off memory already mapped, and the runtime never replaces a mapping, so it changes nothing.
    The movie code (`0x02d032b9`) passes it with no address, to map part of its own direct memory a
    second time;
  - protection other than CPU and GPU read and write;
  - a range that does not fit or was not allocated;
  - releasing part of a range or a mapped one;
  - the error values.

Measured with a local boot that let the libc affinity probe return (not committed): with these four
calls the executable's heap setup succeeds and the boot reaches `entry`. It stops next at
`scePthreadRwlockInit`.

### Range arithmetic

The existing `sceKernelAllocateDirectMemory` and `sceKernelMapDirectMemory`
implementations validate lengths by subtraction after checking the starting
address. This prevents a full-width length from wrapping an end address and
passing the allocation or containment check. An allocation must fit wholly
inside its end-exclusive search range; a map must fit inside one live allocation.
Misaligned address hints still stop by the API name before mapping.

`test_direct_memory_bounds` exercises the registered calls with exact boundaries,
overlong and wrapping ranges, a misaligned hint and a valid writable map. These
are host validation limits, without a claim about native error codes.

## What the title's C library imports (libc.elf and libSceFios2, issue #34)

`libc.elf` is Sony's FreeBSD-derived libc, with HP libunwind for C++ exceptions. These call sites
are in `libc.elf` unless marked; `libSceFios2` imports from `libSceLibcInternal`.

**Data**
- **`__progname`** points at `"eboot.bin"` (shadPS4, the deadinside28 port).
- **`_Stdout`** (`libSceLibcInternal`) is a 456-byte object, the size of `libc.elf`'s own
  `_Stdout`. `libSceFios2` only passes its address to `fflush`.
- **`Need_sceLibcInternal`** is an int holding 1. `libSceFios2` stores its address and never reads
  it.

**No-ops and fixed answers**
- **`__pthread_cxa_finalize`** does nothing: there is no fork, so there are no atfork handlers.
- **`_is_signal_return`** answers 0: no guest signals are delivered, so there is no signal
  trampoline (PS4Recomp agrees).
- **`signal`** keeps each signal's disposition and answers the previous one, delivering nothing.
  libc's one call sets SIGKILL to SIG_DFL. A signal outside 1 to 128, or a handler other than
  SIG_DFL for SIGKILL or SIGSTOP, answers SIG_ERR with EINVAL (FreeBSD's rules).
- **`__cxa_finalize`** (`libSceFios2`) does nothing: the module registers no handlers.

**Output and files**
- **`_write`** writes descriptors 1 and 2 (stdio, `+0x27362`) to the host's standard output and
  error. Other descriptors stop by name.
- **`fflush`** (`libSceFios2`) flushes the host's standard output for `&_Stdout`, and every host
  stream for NULL. Other streams stop by name.
- **`fstat`** is `sceKernelFstat` with -1 and errno (libunwind, `+0x530ae`).

**Clocks and time**
- **`getrusage(RUSAGE_SELF)`** gives the host's process times in FreeBSD's 144-byte layout. libc's
  `clock()` (`+0x2ef13`) reads them.
- **`sceKernelGettimezone`** writes the standard offset in minutes **east** of UTC and the daylight
  saving minutes, for the host's zone. libc's `localtime` and `mktime` add both to UTC, which fixes
  the sign; shadPS4 fills them the same way.
- **`localtime` and `mktime`** (`libSceLibcInternal`) use the same zone. `mktime` normalizes in
  place, or answers -1 and leaves the structure as it was.

**Memory**
- **`mmap`.** Anonymous private read-write maps (`+0x52fac` and others) are flexible memory,
  rounded up to 16 KiB (shadPS4's `posix_mmap`, and a native port family).
  - EINVAL for a zero length, a descriptor other than -1, or a non-zero offset.
  - File maps, hints, and other flags or protections stop by name.
- **`munmap`** is `sceKernelMunmap` with -1 and errno.
- **`msync`** is libunwind's mapped-memory probe (`+0x541a5`). The runtime asks the host and
  answers 0, or -1 with ENOMEM. A constant 0 would let libunwind read unmapped memory.
- **`sceKernelMapNamedFlexibleMemory`** maps like `sceKernelMapFlexibleMemory`. No name answers
  `0x8002000e`, and 32 or more characters `0x8002003f` (shadPS4).
- **`sceKernelMlock`** answers 0: host memory needs no locking. NULL, zero length or a wrapping
  range answer EINVAL.
- **`malloc` and `free`** (`libSceLibcInternal`) use the host allocator, reached through
  `libSceFios2`'s default allocator.

**Process and input (maintainer's rulings, 2026-11-10)**
- **`getpid`** answers 1000, the fixed id of the bloodborne-native port family (facts only): runs
  stay reproducible, and the host's pid is not the guest's. libunwind passes it to `sysctl`.
- **`_read`** on descriptor 0 (stdio, `+0x2729a`) answers end of file, 0 bytes: a console title has
  no standard input. Other descriptors stop by name.
- **`__stack_chk_guard`** (data) is the stack protector's canary, a fixed non-zero word,
  `0xdeadbeef54321abc` (shadPS4's; droogie's and Astrr00's hosts also use fixed constants). Unbound,
  the import read a zeroed page.

**Exceptions**
- **`sceKernelDebugRaiseException` and the `OnReleaseMode` variant.** A non-zero second argument
  answers EINVAL. Otherwise the program stops by name with the code: `abort()` raises
  `0xa002000b`. This is shadPS4's behaviour.

## C++ exceptions and module information (`sceKernelGetModuleInfoFromAddr`, `__Ux86_64_setcontext`, issue #34)

libc.elf's HP libunwind and FreeBSD's `__cxa_finalize` ask the system about modules and process
memory (`runtime/syslib/kernel_unwind.c`). The runtime records every module it maps
(`rt_process.modules`, filled by `runtime/boot.c` from the loader: segments, `PT_GNU_EH_FRAME`,
`DT_INIT`, `DT_FINI`).

- **`sceKernelGetModuleInfoFromAddr(address, flags, info)`** fills the 0x1a8-byte structure
  shadPS4's `module.h` calls `OrbisKernelModuleInfoEx`: size, name (256 bytes), id, TLS fields
  (left 0: no caller reads them), init and fini, the exception frame index and the frames it points
  to with their sizes (`0x148`-`0x15c`), and up to four segments `{address, u32 size, s32 prot}`
  with their count (`0x160`, `0x1a0`). The segments carry the program header's flags, as in
  shadPS4. libunwind asks with flags 1 (`+0x61d34`, then reads the index at `0x148` and checks its
  version byte); `__cxa_finalize` with flags 2. shadPS4's errors: flags 3 or more zero the
  structure and answer `EINVAL`, no structure `EFAULT` (`0x8002000e`), an address in no module
  `ESRCH` (`0x80020003`). The frames' start is decoded from the index's DWARF pointer encoding
  (absolute or pc-relative, 4 or 8 bytes); other encodings, another version and frames after their
  index stop by name.
- **`__elf_phdr_match_addr(info, address)`** is FreeBSD libc's helper (`lib/libc/gen/elf_utils.c`):
  1 when a segment with the execute flag holds the address, else 0 (`+0x2f39e`).
- **`__Ux86_64_setcontext(context)`** resumes at a landing pad. The layout is the one libc.elf's own
  getcontext writes (`+0x53cd0`, FreeBSD's `ucontext_t` with the machine context at `0x40`). Every
  general register, the flags and the x87/SSE state (`fxrstor` from `0x120`) are restored; the
  return address is pushed below the new stack pointer and returned to, as libunwind's own
  setcontext does (maintainer's ruling). A context of another length or format, or a misaligned
  FXSAVE area, stops by name.
- **`sigreturn`** stops by name: the runtime delivers no guest signals and answers
  `_is_signal_return` with 0, so libunwind never takes that path (maintainer's ruling).
- **`sysctl`** answers `{CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid}` (libunwind's process map query,
  `+0x52f50`) with -1 and `ENOENT` (maintainer's ruling); libunwind then probes with `msync`. Any
  other name stops by name.
- **`sceKernelPrintBacktraceWithModuleInfo`** writes a line on standard error and answers 0
  (PS4Recomp and the deadinside28 port do the same).

## Kernel semaphores (`libkernel`, issue #34)

`eboot` imports `sceKernelCreateSema`, `sceKernelDeleteSema`, `sceKernelWaitSema`,
`sceKernelSignalSema` and `sceKernelPollSema` (`symbols/imports.csv`). Its own calls are the
evidence here. They include the audio wrappers at `0x0111ad10`–`0x0111adc0` and the wrappers at
`0x0084ac20`–`0x0084ac83`:

- **Create:** `(&handle, name, attr, initial, max, NULL)`, with attr 1 or 2, initial 0 or passed
  through, and max `0xffff`, 1, or passed through.
- **Wait:** `(handle, 1, NULL)`, with no timeout.
- **Signal:** `(handle, 1)` or a count passed through.
- **Delete:** `(handle)`.
- **Results:** every result is compared with 0.

The signatures, and attr 1 = FIFO and 2 = priority wake order, match the OpenOrbis homebrew SDK
headers (supporting evidence only).

**What the runtime does.** A semaphore is a runtime object behind the handle. Wait blocks until the
units are there. Waiters are served in the order they arrived, or with attr 2 by thread priority:
- the best priority goes first, which is the smallest number (OpenOrbis `_types/user.h`: `0x100`
  highest, `0x2FF` lowest);
- equal priorities are served in arrival order;
- each waiter is placed by the priority its thread has when it starts to wait. The runtime records
  each guest thread's priority (`scePthreadSetprio`, inherited at creation).

shadPS4's semaphore (`threads/semaphore.cpp`) orders its wait list the same way. Signal adds units
and wakes the waiters.

**What stops by name.** Every case whose PS4 answer is not established stops with the function's
name:
- timeouts;
- a missing semaphore, variable or name;
- an option parameter, and attributes other than 1 and 2;
- counts outside `0 <= initial <= max`;
- signalling past the maximum;
- a poll that cannot be satisfied now;
- deleting a semaphore with waiters;
- a wait for more than one unit behind another waiter (whether it may be passed is not
  established);
- the error values.

A local boot that let the affinity probe and the rwlocks through (not committed) stopped next at
`sceKernelCreateSema`. With throwaway answers past it, the next stops were `gettimeofday`, then
`sceSysmoduleLoadModule` for system module `0x00b4`.

## Time of day (`gettimeofday`, `sceKernelGettimeofday`, issue #34)

`eboot` imports `gettimeofday` from `libScePosix`, `libc.elf` imports it from `libkernel`, and
`libSceFios2.elf` imports `sceKernelGettimeofday` (`symbols/imports.csv`). The executable's own
call, at `0x024e2703`, passes a timeval on its stack and no timezone, and reads both fields as
64-bit values. That matches FreeBSD's amd64 `struct timeval`: seconds, then microseconds.

The runtime answers the host's wall clock, under all three names, because the guest asks the
system it runs on. It returns 0. Given no timeval, the POSIX names fill nothing and succeed, as
FreeBSD's `sys_gettimeofday` does.

What stops by name:
- a timezone argument (obsolete in POSIX; what the PS4 fills in is not established);
- a missing timeval for `sceKernelGettimeofday` (its error value is not established).

## Application content (`libSceAppContent`, issue #34)

`eboot` loads system module `0x00b4` (`libSceAppContent` in the OpenOrbis id list). It imports
`sceAppContentInitialize`, `sceAppContentAppParamGetInt` and `sceAppContentGetAddcontInfoList`
from it. The public toolchain header names these functions but gives no signatures, so the shapes
come from the executable's own calls at `0x024261d8`–`0x0242622a`:

- **Initialize:** `sceAppContentInitialize(init, boot)`. Both structures are zeroed first and the
  result is not checked.
- **Parameters:** `sceAppContentAppParamGetInt(id, int32_t *value)` for ids 0 to 4, each into a
  global, results not checked.

Ids 1–4 are the title's `USER_DEFINED_PARAM_1`–`4` from its own `sce_sys/param.sfo`. Id 0 is the
SKU flag, which the system answers. Neither the boot parameter's contents, the SKU answer, the
answer for a key the file lacks, nor the error values are public.

**Which answers this executable observes.** A scan of every RIP-relative reference shows that the
boot parameter and the outputs for ids 0, 2, 3 and 4 are only passed to these calls. Nothing reads
them afterwards. Only id 1's output is read (at five sites).

**How the runtime answers.** It answers only what the game's own data declares, in
`game/app_params.csv`:
- an id with source `sfo` gets its value from the user's own parameter file (read by
  `tools/boot.py` and passed as `--app-param ID=VALUE`);
- an id with source `unread` succeeds without being written;
- `boot` set to `unread` lets the initialize call pass over the boot parameter.

Anything else stops by name: an undeclared id, a boot parameter not declared unread, and a missing
value variable.

A local boot with the affinity probe and the rwlocks let through (not committed) loads the module,
passes these calls, and stops next at `pthread_cond_init` (`libScePosix`).

**Add-ons (`sceAppContentGetAddcontInfoList`).** It is called as
`sceAppContentGetAddcontInfoList(service label, list, capacity, &hits)` and lists the title's
add-ons (DLC). Each entry is `{entitlement label[17], padding[3], status}`, and status 4 means
installed.
- **Answers:** without a list it gives only the count. A service label above 7, or neither a list
  nor a count, answers `0x80D90002`, and a call before initialize `0x80D90001`. The facts come
  from shadPS4's `libSceAppContent`.
- **The title's call:** it asks for the count first (`0x023b67cf`).
- **What the runtime lists:** an add-on is what the person running the runtime owns, the labels
  given to `tools/boot.py --addcont LABEL` (repeatable), each installed, under service label 0.
  None are listed by default.
- **Bloodborne's expansion:** The Old Hunters is `SPEXPANSIONDLC03`. Its data ships with patch 1.09
  (another Bloodborne port's notes), so the entitlement is all it needs.

## Ending a thread (`scePthreadExit`)

`scePthreadExit(void *value)` ends the calling thread with `value`, as a return from its entry
would: the value kept for a join, then the thread's key destructors, then the end (FreeBSD libthr's
`_pthread_exit`, and shadPS4's `posix_pthread_exit`).

The runtime does not unwind the guest's frames, which were not built for the host's unwinder.
Instead the thread's host side marks where it starts the guest's entry (`setjmp`), and an exit
returns there (`longjmp`). It then ends exactly as a return does.

Stops by name: an exit from inside a key destructor (libthr refuses it too), and an exit from the
main thread, which the runtime runs the boot on.

## Thread attributes: stack size and scheduling policy (issue #34)

The executable's thread wrapper (`0x02a3b324`–`0x02a3b46e`) sets up each thread like this:

1. It initializes an attribute.
2. It sets the stack size: the caller's size, or 64 KiB by default. If that is refused, it retries
   with 16 KiB and reports a fatal error if that fails too.
3. It sets detach state 0 and scheduling policy 1.
4. It creates the thread with `(&thread, &attr, entry, argument, name)`, comparing a failure with
   EPERM, EINVAL and EAGAIN as SCE codes.
5. It destroys the attribute and sets the new thread's affinity to a mask of the six bits `0x3f`.

What the runtime does:

- **`scePthreadAttrSetstacksize`** and **`pthread_attr_setstacksize`** record the size. The stack's
  address is left to the system, as libthr keeps one address and size per attribute.
- **Sizes below 16 KiB** are refused with EINVAL. libthr refuses sizes below its minimum that way,
  and the executable's wrapper names the PS4's minimum: its callers pass smaller sizes, and when
  one is refused it asks again for exactly 16 KiB (`0x02a3b383`).
- **`scePthreadAttrSetschedpolicy`** records one of FreeBSD's three policies (FIFO 1, OTHER 2,
  RR 3). libthr refuses any other with ENOTSUP, whose SCE value is not established, so another
  policy stops by name.
- **A missing attribute** answers EINVAL (as an SCE code under the SCE names).

## Thread creation (`scePthreadCreate`, `scePthreadSetaffinity`, issue #34)

**`scePthreadCreate(&thread, &attr, entry, argument, name)`** runs the guest on a new host thread:

1. The thread gets its own guest stack, of the attribute's size.
2. It gets its own TLS block, a copy of the executable's template as the main thread's is, and its
   thread pointer goes in GS.
3. It is registered, so `scePthreadSelf` answers the new thread's handle.
4. `entry(argument)` is called with the guest's convention. A return ends the thread (POSIX).
5. The attribute's detach state decides whether the host thread is joinable.

The scheduling policy is recorded, but the host schedules the thread: guest priorities are not
modelled.

What stops by name:
- no attribute, or no stack size or detach state on it (the PS4 defaults are not established);
- a stack address the guest supplies;
- the error values.

**`scePthreadSetaffinity`** records the CPU mask. The executable sets it once per thread, within
`0x3f`. Scheduling is left to the host, because the host's CPUs say nothing about the PS4's. An
empty mask stops by name.

**`scePthreadGetaffinity(thread, &mask)`** (imported by `libSceFios2`) answers the recorded mask, or
all ones for a thread nobody set one on (maintainer's ruling of 2026-11-10; shadPS4 answers the
host thread's set, PS4Recomp `0x3f`). No thread answers `ESRCH`, as for the set; no variable stops
by name.

**Stops on other threads.** A worker thread can fault or reach an unimplemented import. The boot
records the first such stop and wakes the main thread (`SIGUSR1`) to make the usual report. Workers
leave the timeout and that signal to the main thread.

A local boot with the affinity probe and the rwlocks let through (not committed) reaches the
`threads` milestone and stops next at `sceUserServiceInitialize`.

## Joining, detaching, explicit scheduling and once (issue #34)

**`scePthreadJoin(thread, void **value)`** and **`scePthreadDetach(thread)`** are FreeBSD libthr's
`pthread_join` and `pthread_detach` ([`thr_join.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_join.c?h=stable/9),
[`thr_detach.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_detach.c?h=stable/9);
OpenOrbis `libkernel.h` shapes; shadPS4's `JoinThread` and `posix_pthread_detach` agree, facts
only). The executable's join (`0x02a3b6c5`) tells `ESRCH`, `EDEADLK` and `EINVAL` apart. The other
callers compare the answer with 0: `libc.elf` `+0x6130` and `+0x6174`, `libSceFios2` `+0x2ebde`,
and the POSIX `pthread_join` (`libScePosix`) at `0x00838793` and `0x008387f3`.

In libthr's order:
- **join:** no thread is `EINVAL`; the calling thread itself `EDEADLK`; a thread the runtime does not
  know, or one already collected, `ESRCH`; a detached thread `EINVAL`; a thread another thread waits
  to join `ENOTSUP` (45). Otherwise it waits for the end, stores the exit value through a non-null
  `value`, and collects the thread.
- **detach:** no thread is `EINVAL`; an unknown or collected thread `ESRCH`; a detached thread, or
  one a thread waits to join, `EINVAL`. A detached thread is collected when it ends.
- **Stops by name:** a join or detach of the main thread, on which the runtime runs the boot.

**POSIX names (`libScePosix`).** They return the error number itself:
- `pthread_create(thread, attr, entry, argument)` is `scePthreadCreate` without a name. The
  executable's job threads (`0x00466674`, `0x004666df`) pass a detached attribute with a stack size
  and a priority.
- `pthread_exit(value)` is `scePthreadExit` (one call, `0x004667e3`).
- `pthread_attr_setschedparam` is `scePthreadAttrSetschedparam`. The job threads take their
  priorities from a table of 256, 650, 700, 750 and 767 (`0x02fc5480`), inside the range the SCE
  name accepts.

**`scePthreadAttrSetinheritsched(attr, inherit)`** chooses between `PTHREAD_INHERIT_SCHED` (4,
libthr's default: a new thread starts at its creator's priority) and `PTHREAD_EXPLICIT_SCHED` (0:
it starts at the attribute's priority), FreeBSD `<pthread.h>`'s numbers. `libSceFios2`'s thread
helper (`+0x2eaa2`) asks for explicit scheduling and then sets the priority. A variable without an
attribute is `EINVAL`. Stops by name: another value (libthr's `ENOTSUP`, whose SCE value is not
established, as for the policy), and explicit scheduling without a priority set (the attribute's
default priority is not established).

**`scePthreadOnce(once, init)`** runs `init` once per once variable (libthr
[`thr_once.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_once.c?h=stable/9)). The
variable's first `int` is its state: 0 never done (the static initializer), 1 done, 2 in progress,
3 in progress with waiters. A caller that finds it in progress waits for the end; any other state
is `EINVAL`. `libc.elf` calls it five times (`+0xad02`, `+0x1b292`, `+0x54ecb` and two tail calls)
with a static variable and a routine. Nothing cancels a guest thread, so a routine always runs to
its end.

**`scePthreadRename(thread, name)`** records a thread's name (OpenOrbis `libkernel.h`; libthr's
`pthread_set_name_np`, to which shadPS4 binds it, facts only). The executable's job threads rename
themselves with it (a tail call at `0x00466834`). The runtime keeps the first 31 bytes, as for the
name `scePthreadCreate` takes, names the host thread with the first 15 (the host's limit; debuggers
and the host's thread list show it), and answers 0. Stops by name: a thread the runtime does not know
or has collected, and a missing name.

## PlayGo (`libScePlayGo`)

PlayGo lets a title start before all of its package is installed: the title asks which chunks of
the package are present. On this runtime the whole title is always present, so every chunk is
reported installed (`runtime/syslib/playgo.c`).

| Call | Shape (from the executable's own calls) | Runtime |
|---|---|---|
| `scePlayGoInitialize(params)` | a 16-byte block: a 2 MiB work buffer the title allocated, its size, 0 (calls at `0x023dd67a`, `0x023de849`); neither reads the result | checks the buffer and answers 0 |
| `scePlayGoOpen(int32_t *handle, params)` | params is NULL (`0x023deae4`); 0 counts as opened | writes handle 1, the runtime's choice (the title only passes it back) |
| `scePlayGoGetChunkId(handle, uint16_t ids[], capacity, uint32_t *count)` | capacity 100 (`0x023deb0f`); the result is not read | the package's chunks, 0 to `chunk_count - 1`, from `chunk_count` in the copy's `sce_sys/playgo-manifest.xml` (3 for this title; `playgo-chunk.dat` agrees) |
| `scePlayGoGetLocus(handle, const uint16_t ids[], count, int8_t loci[])` | one byte per chunk (`0x023de8d6`, `0x023deb30`); non-zero counts as failure; the title treats 0 as not there, 2 as still arriving and polls again, any other value as ready | 3 for every chunk, a ready value (the title can't tell the ready values apart) |
| `scePlayGoSetInstallSpeed(handle, speed)` | called through a function pointer (`0x023dd891`) with 1 (trickle) or 2 (full); 0 is suspended | accepts it (everything is installed, so there is nothing to pace; nothing reads it back); an unknown handle `0x80B20009`, before initialize `0x80B20005`, another speed `0x80B20004` (shadPS4's `libScePlayGo`) |

No public source declares these calls: the OpenOrbis toolchain has no PlayGo header. Stops by
name: Open with parameters, an unknown handle, a chunk list longer than the space given, an unknown
chunk ID, a missing manifest, and every error value.

## The user service's start-up calls (`libSceUserService`, issue #36)

`eboot` imports `sceUserServiceInitialize`, `sceUserServiceTerminate`,
`sceUserServiceGetInitialUser` and `sceUserServiceGetLoginUserIdList`, among others. The shapes
match the OpenOrbis homebrew SDK's `UserService.h` and `_types/user.h` (supporting evidence only):
- the parameter block holds a `uint32_t` priority;
- the login list is four user ids, an unused slot holding -1;
- id `0xFF` is the system's.

The executable initializes the service twice:
- once at `0x024e1e69`, with priority `0x2bc`;
- again at `0x010ba1ac`, where it accepts success or exactly `0x80960003`. So a second initialize
  answers `0x80960003`, the "already initialized" code.

**What the runtime does.** It models one user, the person running it, logged in from the start:
- `GetInitialUser` answers that user's id;
- the login list holds it, then three -1 slots;
- the id is a handle the runtime hands out, never -1 or `0xFF`, because the PS4's ids are the
  system's and a title only passes them back.

**What stops by name:** a call before initialize, a missing argument, and the other error values.

**The user's name** (`sceUserServiceGetUserName(int32_t, char *, const size_t)`, in OpenOrbis
`UserService.h`; a name is at most 16 characters). The executable asks with 17 bytes
(`0x02561cd7`) or a size it computed (`0x0111bdfe`), and goes on only on 0. The name is the
runtime's choice, `Hunter`, written with its terminating zero. A user the runtime did not hand
out, or a buffer too small for the name, stops by name.

**Login and logout events** (`sceUserServiceGetEvent(OrbisUserServiceEvent *)`, the event being
a type, 0 login or 1 logout, and a user id; "no event" is `0x80960007`, OpenOrbis `errors.h`). No
user logs in or out during a run, so every call answers `0x80960007` and writes nothing. The
executable's one caller (`0x025628e4`) is a polling thread: after any non-zero answer it sleeps
33 ms (`sceKernelUsleep(0x80e8)`) and asks again, and it handles a login or logout only when one
comes. It never waits for the first user's login to be reported; whether the PS4 reports that
login is not public.

## The real-time clock (`libSceRtc`)

The runtime gives the host's local time, and keeps ticks as its own convention
(`runtime/syslib/rtc.c`).

**Sources.**
- **Signatures:** OpenOrbis `Rtc.h` gives `sceRtcGetCurrentClockLocalTime(date and time *)`,
  `sceRtcGetTick(const date and time *, uint64_t *)` and `sceRtcSetTick(date and time *, const
  uint64_t *)`. It names `sceRtcGetTickResolution` without a signature; the executable's one call
  (`0x010b70ea`) takes no argument and uses the `int32_t` answer as ticks per second, a negative
  one being a failure.
- **The date and time** is six 16-bit fields (year, month, day, hour, minute, second), then a
  32-bit microsecond at offset 12, 16 bytes in all. Rtc.h gives the microsecond 16 bits, which
  cannot hold 999999. The Vita homebrew SDK's `SceDateTime`, of the same library family, gives it
  32 bits in a 16-byte structure, and the executable copies the whole structure as 16 bytes.
- **Ticks:** the executable compares ticks, takes their differences and asks for the resolution.
  It holds no constant relating them to another epoch (neither the span from 0001 to 1970 in
  microseconds or seconds, nor its negation). About 60 sites ask for the local time; those reached
  pass it straight to `sceRtcGetTick`.

**What the runtime does.**
- **The local time** is the host's, in the time zone of the person running the runtime, as the
  console gives the one its owner set. A leap second shows as `:59`.
- **A tick** is the runtime's own convention, the same in every call: microseconds since
  0001-01-01 00:00:00 (proleptic Gregorian), 1000000 per second. `sceRtcSetTick` reverses
  `sceRtcGetTick` exactly.
- **Stops by name:** a date out of range (including February 30 and February 29 of a non-leap
  century), a tick past the year 9999, a missing argument, and every error value.

**Day of the week:** `sceRtcGetDayOfWeek(year, month, day)` (`0x024ee2a9`, its answer packed
into 3 bits) answers 0 for Sunday to 6 for Saturday. The PS4Recomp project's `libSceRtc` numbers
the days so, and notes that a real PS4's ticks count from year 1, as these do; both are used as
facts only. (The PSP SDK documents 0 as Monday, for an older library.) A date out of range stops
by name.

### Unix time, time zones, RFC 2822 and the network tick (issue #40)

These five calls relate ticks to Unix time and to UTC (`runtime/syslib/rtc.c`, tests in
`test/syslib/rtc_time_zones.c`).

- **Ticks and Unix time:** a tick counts microseconds from year 1, so 1970-01-01 00:00:00 UTC is
  tick 62135596800000000. PS4Recomp's `libSceRtc` and shadPS4's `rtc.cpp` both relate them so
  (facts only).
- **`sceRtcSetTime_t(date and time *, time_t)`** (`0x010b70a6`: seconds parsed from text, then
  `sceRtcGetTick` on the result, going on only on 0) gives the UTC date and time that many seconds
  after 1970. Both sources say the answer depends on the SDK the title was built with: before SDK
  3.00 only the low 32 bits count, from 3.00 a negative count is an error. Counts from 0 to
  `0xffffffff` mean the same either way; others stop by name.
- **`sceRtcConvertUtcToLocalTime(const tick *, tick *)`** and **`sceRtcConvertLocalTimeToUtc`**
  (11 and 2 call sites, such as `0x01969648` and `0x019696ae`; each passes a local copy of the
  tick and treats anything but 0 as no time) use the host's time zone, as the local time above
  does, with its daylight saving rule at that instant. A local time that the clock skips or shows
  twice at a daylight saving change stops by name, because the PS4's choice there is not
  established.
- **`sceRtcFormatRFC2822LocalTime(char *, const tick *utc)`** (`0x023e5ca9`, with no tick, which
  means now) writes RFC 2822 section 3.3's date and time in the host's local time, such as `Mon,
  02 Nov 2026 15:05:09 +0100`: 31 characters and a NUL, the day of the month in two digits as both
  sources write it.
- **`sceRtcGetCurrentNetworkTick(tick *)`** (`0x01086f50`, `0x010b70cf`; 0 is success, and the first
  treats a failure as no time) answers the current UTC tick from the host's clock, as both sources
  do. The host's wall clock stands for the network clock.
- **Stops by name:** a missing argument, and a time before year 1 or past the year 9999.

## The controller (`libScePad`)

The runtime models one standard controller, the one user's, always connected
(`runtime/syslib/pad.c`). Its state comes from the input backend (`runtime/input_host.h`,
`runtime/input/`): by default (`pbboot --input none`, `tools/boot.py --input none`) none is
installed and the pad is at rest, so CI and recorded boots read the same state as before. With
`--input host` the host's first gamepad and the window's keyboard drive it, and the title's
vibration rumbles the gamepad.

**Sources.**
- **Signatures and layouts:** the OpenOrbis homebrew SDK's `Pad.h`, `_types/pad.h` and
  `_types/errors.h`. They give the 120-byte `OrbisPadData` (touch data at `0x34`: a count, seven
  reserved bytes, then two 8-byte touches of x, y and an id), the 28-byte `OrbisPadInformation`,
  the two-byte vibration block (large motor, then small), port type 0 (standard), and
  `ALREADY_OPENED` `0x80920004`.
- **Button bits:** OpenOrbis' `OrbisPadButton`; shadPS4's `OrbisPadButtonDataOffset`
  (`src/core/libraries/pad/pad.h`) agrees: L3 `0x2`, R3 `0x4`, options `0x8`, up `0x10`, right
  `0x20`, down `0x40`, left `0x80`, L2 `0x100`, R2 `0x200`, L1 `0x400`, R1 `0x800`, triangle
  `0x1000`, circle `0x2000`, cross `0x4000`, square `0x8000`, touch pad `0x100000`.
- **The executable's own calls agree:**
  - before each `scePadReadState` (`0x02a90168`, `0x02a91948`) it clears exactly 120 bytes, and
    afterwards it tests `connected` at offset `0x4c`;
  - `scePadGetControllerInformation` gets a cleared 28-byte block. The title reads the touch
    resolution, dividing a non-zero touch point by it, and the stick deadzones: a stick within
    `0x80` ± the deadzone counts as centred;
  - `scePadInit` is called once (`0x02fa6182`), and the result is not read;
  - `scePadOpen(initial user, 0, 0, NULL)` keeps any handle ≥ 0, and the title closes before it
    opens again;
  - `scePadSetVibration` is called with the title's speeds (`0x02a9c7c1`) or with both 0 to stop
    (`0x02a9c822`), and 0 counts as success.
- **The DualShock 4:** the touch pad is 1920 × 942 (`DS4_TOUCHPAD_WIDTH` and `_HEIGHT` in the
  Linux kernel's `drivers/hid/hid-playstation.c`); each stick axis is a byte, 0 at the left and
  at the top; a touch carries a 7-bit id that changes for each new touch; the left motor is the
  strong one (a force-feedback effect's strong magnitude goes to `motor_left`).
- **SDL3:** stick axes run -32768 (left, up) to 32767 (right, down) and triggers 0 to 32767
  (`SDL_GetGamepadAxis`); touch fingers are 0..1 of the pad (`SDL_GetGamepadTouchpadFinger`);
  scancodes are USB HID keyboard usages (`SDL_scancode.h`); `SDL_RumbleGamepad`'s low-frequency
  motor is the left one.
- **The timestamp** is the process time in microseconds when the state is read, as shadPS4 stamps
  it (`src/input/controller.cpp`); the title does not read it.

| Call | Runtime |
|---|---|
| `scePadInit` | 0 |
| `scePadOpen` | handle 1, the runtime's choice; a second open while it is open answers `0x80920004` |
| `scePadClose` | 0; the pad can be opened again |
| `scePadReadState` | the backend's buttons, sticks, triggers and touches, the timestamp, `connected` 1; no motion, identity orientation. At rest without a backend: no button, sticks `0x80`, triggers 0, no touch, timestamp 0 |
| `scePadGetControllerInformation` | touch pad 1920 × 942; stick deadzones 13 (the runtime's choice); connected; a local pad |
| `scePadSetVibration` | 0; the speeds are kept (`rt_pad_vibration`) and passed to the backend: with `--input host`, the large motor to SDL's low-frequency rumble, the small one to the high-frequency rumble |

**Host input (`pbboot --input host`, `tools/boot.py --input host`).** It needs a build with SDL3
(the window's) and `--display window`, since the window's event loop reads the devices: SDL's
events are pumped on the display's thread (`display_vulkan.c`), never from a guest thread, and
after each pump `rt_input_host_pump` samples the gamepad and keyboard there
(`runtime/input/input_host_sdl.c`). The state therefore changes once per presented frame. For
example:

```
python3 tools/boot.py --display window --audio host --input host
```

The first connected SDL gamepad is used, and the next one when it is removed. Its buttons map by
position: south cross, east circle, west square, north triangle, start options, back and the
touch pad click the touch pad, the shoulders L1 and R1, the stick clicks L3 and R3, the D-pad the
D-pad. A DualShock 4 or DualSense touch pad reports up to two touches. Stick bytes are the axis'
top eight bits after the offset; the triggers are scaled to 0..255. The keyboard works with or
without a gamepad; a key's stick direction overrides the gamepad's stick, both keys of an axis
cancel out, and buttons combine. The default layout (`rt_default_keys`, `runtime/input/input.c`):

| Keys | Pad |
|---|---|
| W A S D | left stick up, left, down, right (full deflection) |
| arrow keys | right stick |
| I J K L | D-pad up, left, down, right |
| Return, Space | cross |
| Backspace | circle |
| R | square |
| F | triangle |
| Q / E | L1 / R1 |
| Z / C | L2 / R2, fully pressed |
| left Shift / Tab | L3 / R3 |
| Escape | options |
| T | touch pad click |

The runtime's choices: the L2 and R2 bits are set while their trigger is off zero; touch ids count
0..127 and wrap, down fingers listed first; a held vibration is sent to SDL again every 30
seconds, as SDL's rumble lasts at most about 65.

**Scripted input (`pbboot --input-script SPEC`, `tools/boot.py --no-record --input-script SPEC`).**
For unattended boots, a timed script plays the controller through the same backend interface
(`runtime/input/input_script.c`); it works with any `--display` mode and cannot be combined with
`--input host`. SPEC is the script, or `@FILE` to read it from a file. Events are separated by
commas, semicolons or new lines, and `#` starts a comment. Each event is `WHEN:CONTROLS[*HOLD]`:

- `WHEN` is seconds since the process started (`45`, `45.5s`) or frames presented by video out's
  first port (`700f`). Frames follow the game's own progress, so they suit a slow boot; seconds
  need no display.
- `CONTROLS` are names joined by `+`: `cross circle square triangle up down left right l1 r1 l2 r2
  l3 r3 options touch`, and the stick directions `lstick-up lstick-down lstick-left lstick-right`
  and `rstick-up` ... `rstick-right` at full deflection. A scripted trigger is fully pressed.
- The controls are held from `WHEN` for `HOLD`, on the same clock (default 0.25 s or 4 frames),
  then released; overlapping events combine.

pbboot writes `pbboot: input script: event N starts (S s, frame F)` when the title first reads
the pad during an event, so a run's log shows when each press landed. For example:

```
python3 tools/boot.py --no-record --display headless --input-script "45:down,47:cross"
```

A malformed event, an unknown control, a hold on the other clock, more than 256 events or a script
file over 64 KiB is refused before the boot, naming the event.

Fields the executable does not read are left zero. **Not modelled:** motion. The accelerometer
and gyroscope are not forwarded, because the units and axes the system reports them in are not
established here (shadPS4 scales SDL's m/s² by 0.098 without a source); acceleration and angular
velocity stay 0 and the orientation the identity. **Stops by name:** a second initialize, a call
before initialize, another user, port type, index or open parameters, an unknown handle, a
missing argument, and the other error values.

**The motion settings (issue #36).** `scePadResetOrientation(handle)` (OpenOrbis `Pad.h`) makes
the pad's present attitude the reference its orientation is reported from;
`scePadSetAngularVelocityDeadbandState(handle, bool)` and `scePadSetTiltCorrectionState(handle,
bool)` (named in `Pad.h` without a signature; shadPS4's `pad.cpp` declares `(int32_t handle,
bool enable)`, used as a fact) switch the motion sensors' deadband and tilt correction. The
executable calls each through a small wrapper (`0x02a9082e`, `0x02a90852`, `0x02a90882`) with its
open handle, and a bool for the two settings, and counts 0 as success. Since motion is not
modelled, a reset leaves the reported orientation the identity, neither setting changes a
reading with no motion, and each call answers 0 (`test/syslib/pad_motion_settings.c`). An
unknown handle and a setting other than 0 or 1 stop by name.

## The on-screen keyboard, canceled (`libSceImeDialog`, issue #36)

**Facts.** OpenOrbis `ImeDialog.h` and `_types/ime_dialog.h` give:
- the shapes: `sceImeDialogInit(setting, extended)`, `GetStatus()`, `GetResult(result)` and
  `Term()`;
- the statuses: NONE 0, RUNNING 1, STOPPED 2;
- the result: an end status (OK 0, CANCEL 1, ABORT 2) followed by 12 reserved bytes.

shadPS4's `ime_dialog.cpp` (facts only) gives the states: Init from NONE to RUNNING, Abort from
RUNNING to finished with ABORT, and Term from finished to NONE.

**The executable's keyboard** (`0x02d03f80`):
- **Opening.** It fills a setting with a text buffer that holds the initial text, calls Init with
  no extended setting, and takes 0 as open.
- **Polling.** Each frame, `0x02d03df0` asks GetStatus. On 2 it reads the result: end status 0
  takes the buffer's text; 1 and 2 (or a failed GetResult) take none.
- **Closing.** `0x02d03e70` then calls Term. `0x02d04300` calls Abort while the keyboard is open
  and takes 0 as closed.

**What the runtime does:**
- **Cancel.** Init opens the keyboard, and the first GetStatus finds it closed with CANCEL, as if
  the user backed out at once. The title takes no text, the buffer is left as given, and nothing is
  shown.
- **Abort and Term.** Abort of an open keyboard closes it with ABORT, and Term closes the library.

**What stops by name:**
- Init without a setting, or while a keyboard is open or not yet terminated (shadPS4 answers busy,
  `0x80bc0001`; this is not confirmed);
- GetResult before the keyboard closed, or with no structure;
- Abort when no keyboard is open;
- Term before the keyboard closed.

An extended setting is ignored.

## The mouse, not connected (`libSceMouse`, issue #36)

The runtime has no host mouse, so it answers as a console with no mouse plugged in
(`runtime/syslib/mouse.c`, tests in `test/syslib/mouse_not_connected.c`).

**Sources.** shadPS4's `libSceMouse` (`mouse.h`, `mouse.cpp`, `mouse_error.h`; facts only) gives
the shapes: `sceMouseInit(void)`, `sceMouseOpen(user, type, index, param)`, `sceMouseRead(handle,
data, count)` answering how many records it wrote (1 to 64 asked for), `sceMouseClose(handle)`,
the 40-byte record `{ uint64_t timestamp, bool connected, uint32_t buttons, int32_t x, y, wheel,
tilt, 8 reserved }` and `0x80DF0004` for a mouse already open. With no mouse it answers one
record, not connected, all zero. OpenOrbis `Mouse.h` only names the calls.

**The executable's calls agree.** It loads the library's system module (`0x02a8bce1`), then calls
Init (six sites, such as `0x02a8bcea`) and counts 0 as success. Two input objects each open the
mouse with (the initial user, 0, 0, NULL) (`0x02a8bf75`, `0x02a8c465`), keep the answer as their
handle only when it is above 0, and close a handle above 0 before opening again. Read
(`0x02a8c07f`, `0x02a8c56f`) asks for one record into the 40 bytes the object clears: below 0 is a
failure, and a record that is not connected ends the poll.

**What the runtime does.** Init answers 0 every time, as shadPS4 does. Open gives handle 1 (the
runtime's choice; the title keeps only handles above 0) and `0x80DF0004` while it is open. Read
writes one all-zero record (not connected) and answers 1. Close frees the handle. **Stops by
name:** a call before Init, another user, type, index or open parameters, an unknown handle, a
count outside 1 to 64, and a missing record.

## The common dialog's initialize (`libSceCommonDialog`, issue #40)

`sceCommonDialogInitialize` is the only call `eboot` imports from `libSceCommonDialog`. It takes
no arguments (OpenOrbis `CommonDialog.h`, as supporting evidence).

The executable calls it twice:
- at `0x024e1e8c`, logging a failure;
- at `0x02023fbb`, accepting success or exactly `0x80b80002`.

So the first call succeeds and a repeat answers `0x80b80002`, the "already initialized" code. No
dialog is shown: each dialog library's own calls do that (next section).

## The common dialogs, closed at once (`libSceMsgDialog`, `libSceSaveDataDialog`, `libSceNpProfileDialog`, `libSceNpCommerce`, issues #35 and #40)

The executable uses four of the console's common dialogs. It never reads a choice from three of
them: the message, save data and commerce dialogs do not import `GetResult`. It reads the profile
dialog's result but does not use it. So the runtime models each dialog as one the user closes at
once.

**Facts:**
- **Statuses and the base block.** NONE 0, INITIALIZED 1, RUNNING 2, FINISHED 3, and the
  parameter block's base: 0x30 bytes, with a magic number `0xC0D1A109` at 0x2c (OpenOrbis
  `_types/common_dialog.h`). Every call site stores the magic as `0xC0D1A109` plus the low 32 bits
  of the base's own address.
- **Errors and states.** `0x80b80003` (not initialized) and `0x80b80004` (already initialized), and
  the states: Initialize from NONE, Open from INITIALIZED or FINISHED, Terminate back to NONE
  (shadPS4's `commondialog.h`, `msgdialog.cpp`, `np_commerce.cpp` and `np_profile_dialog.cpp`,
  facts only).

**The executable's uses:**
- **Message dialog.** One system message, mode 3 with type 2 (the PSN user-generated content
  restriction notice, built at `0x020233b3`). Its start-up (`0x02023e95`) accepts 0 or exactly
  `0x80b80004`. It polls UpdateStatus while RUNNING (`0x02023d6b`) and terminates on FINISHED.
- **Save data dialog.** Shown when a save mount fails with `0x809f000a` (no space,
  `0x02c425e7`). It initializes, retrying while the answer is busy (`0x80b80008`), opens a
  system message (mode 3, size 0x98), polls while RUNNING and terminates.
- **Profile dialog.** `0x010b85e0` opens a profile (mode 1, size 0x80) for an online id and polls.
  On FINISHED it reads the result, which it does not use, and then terminates.
- **Commerce dialog.** `0x02040350` opens the PlayStation Plus offer (mode 5, size 0x80), ignoring
  Initialize's answer. `0x02040244` polls while RUNNING. Terminate's answer is ignored.

**What the runtime does:**
- **Opening.** Open checks the block (base size 0x30, the address-based magic, the library's own
  size field) and starts the dialog.
- **Closing.** The first UpdateStatus after Open finishes it. Nothing is shown.
- **Terminating.** A Terminate before any Initialize answers `0x80b80003`, which every caller
  ignores.
- **The profile result.** It is result 0, user action 1 (canceled) and the caller's user data
  (`OrbisNpProfileDialogResult`, 0x30 bytes, in shadPS4's `np_profile_dialog.h`).

**What stops by name:** an Open outside INITIALIZED or FINISHED; a NULL or malformed block; a
profile result before FINISHED or with no structure.

One dialog at a time (the console's busy answer, `0x80b80008`) is not enforced: each runtime
dialog finishes on its first status update.

## The splash screen and system status (`sceSystemServiceHideSplashScreen`, `sceSystemServiceGetStatus`)

- **`sceSystemServiceHideSplashScreen()`:** the title tells the system it may take down the boot
  splash, because its own screens follow. It answers 0, and the runtime keeps the fact
  (`rt_splash_hidden`), a sign of how far a boot got. The executable calls it through a function
  pointer (`0x01d5d995`) and reads no result.
- **`sceSystemServiceGetStatus(status)`:** polled for system events. The status is
  `{int32_t pending events, then one byte each: system UI overlaid, in background execution, CPU
  mode 7 normal, live streaming, out of the VR play area}`, and a missing pointer answers
  `0x80A10003`. The runtime has no system UI and no system events, so the answer is: no events,
  nothing overlaid, in the foreground, CPU mode normal, not streaming, not in VR. Only these 9
  bytes are written.

Facts from shadPS4's `libSceSystemService` (`systemservice.cpp`, `systemservice.h`,
`systemservice_error.h`).

## The web browser, not opened (`sceSystemServiceLaunchWebBrowser`, issue #40)

The executable calls it in three places and discards the answer each time. Each call passes a URL
string and NULL; OpenOrbis `SystemService.h` names the function without a shape. The three places:
- the official site link at `0x02437039`;
- a debug `BROWSER_TEST` command at `0x01d5eeeb`, with the same URL;
- a URL it builds at `0x02437112`.

The runtime opens no browser, as if the user closed it at once. It notes the URL on stderr and
answers 0 (shadPS4 also answers 0, facts only). No URL stops by name.

## System events (`sceSystemServiceReceiveEvent`, issue #40)

`sceSystemServiceReceiveEvent(event *)` takes the oldest pending system event. shadPS4's
`libSceSystemService` answers `0x80A10003` for a missing pointer and `0x80A10004` ("no event",
`systemservice_error.h`) for an empty queue (facts only). The executable calls it only inside a
loop bounded by the pending count `sceSystemServiceGetStatus` gave (`0x01b8a433`, `0x02276aa3`),
and acts on an event only when the call answers 0. The runtime raises no system events, so it
answers "no event" and writes nothing (`test/syslib/system_service_events.c`).

## System settings (`sceSystemServiceParamGetInt`, issue #40)

`sceSystemServiceParamGetInt(id, int32_t *value)` reads a console system setting. The setting
ids and the language codes come from the OpenOrbis homebrew SDK's `_types/sys_service.h` (id 1 is
the language, and 1 there is English (United States)).

All nine of the executable's call sites ask for id 1, into a variable preset to -1. They accept
codes 0–20 and fall back to a default otherwise.

**What the runtime does.** A setting is the console owner's choice, so the runtime answers what the
person running it chose:
- `tools/boot.py` passes `--system-param ID=VALUE`;
- it defaults the language to 1 (English, United States);
- `--system-param 1=18`, for example, chooses English (United Kingdom).

**What stops by name:** a setting nobody gave a value, and a missing variable.

## PSN services with no one signed in: matchmaking, web API, leaderboards, signaling, lookup (issue #41)

Every one of these services is gated in the executable on the signed-in user's NpId or online ID,
which `sceNpGetNpId` and `sceNpGetOnlineId` never give offline:
- matchmaking setup: `0x010bcf70`;
- web API client: `0x010b4d90`;
- signaling and lookup setup: `0x010c0800`.

So nothing beyond `sceNpMatching2Initialize` is reached, with one exception: the leaderboard
object reaches `sceNpScoreCreateRequest` with title context 0, from the comment checks
(`0x010d07a8`, `0x010d0a39`) and the jobs (`0x010cdf63`, `0x010cf339`, `0x010d04ed`), and treats an
answer below 1 as failure.

The codes come from shadPS4's `np_matching2.cpp`, `np_web_api.cpp`, `np_score.cpp`,
`np_signaling.cpp` and `np_utility.cpp`, with PS4Recomp where noted (facts only). The executable
itself tests several of them (`0x80550C06`, `0x80552701`/`04`/`05`, `0x8055080B`, `0x80550707`),
which supports the numbering. Ids start at 1 everywhere.

- **Matching2.**
  - Before initialize, every call answers `0x80550C03`.
  - NULL pointers, and a context block other than 0x28 bytes, answer `0x80550C0A`. An unknown
    context answers `0x80550C0B`.
  - `ContextStart` answers `0x804101E2`, the resolver timeout. shadPS4 notes this was confirmed on
    a disconnected console, and PS4Recomp agrees. So no context starts, and `ContextStop` answers
    `0x80550C08`.
  - Calls that need a started context stop by name on a known context: the server id, request
    options, the room, lobby and signaling callbacks, and every room and lobby request. A NULL
    context callback also stops by name.
- **Web API.**
  - Library contexts are ids below `0x8000`. User contexts carry their library in the upper 16 bits.
  - Error answers: `0x80552902` for a NULL or empty argument; `0x80552903` for a library id of
    `0x8000` or more; `0x80552904`, `0x80552905` and `0x80552906` for an unknown library, user
    context or request; `0x8055290B` and `0x8055290C` for an unknown filter or callback.
  - `SendRequest` answers `0x80552907`, not signed in (PS4Recomp agrees).
  - The response calls on a known request, which was never sent, stop by name. So does
    `UtilityParseNpId` on a real argument; NULL arguments answer `0x80550003`.
- **Leaderboards.**
  - `CreateNpTitleCtx` answers `0x80550006`, signed out. PS4Recomp and shadPS4's sign-in-checking
    variant agree; shadPS4's plain variant and one port hand out an id instead.
  - So no title context or request exists. Every id answers `0x8055070E`, and
    `SetPlayerCharacterId` first answers `0x80550704` for a negative character id.
- **Signaling.**
  - Answers: `0x80552702` for a second initialize; `0x80552701` before initialize; `0x80552715` for
    NULL pointers; `0x80552705` for an unknown context.
  - Connections: `ActivateConnection` answers `0x8055270B`, network information not available
    (PS4Recomp). `DeactivateConnection` answers `0x8055270E`. `GetConnectionStatus` writes inactive
    (0) and answers 0.
- **Lookup.**
  - Answers: `0x8055070C` for a NULL NpId; `0x8055070E` for an unknown title context or request;
    `0x80550704` for an option block.
  - `sceNpLookupNpId` starts the lookup. `sceNpLookupPollAsync` then answers 0 with result
    `0x80550705`, no login (PS4Recomp); before that it answers 1, still running (shadPS4).

## PSN offline: sign-in state, callbacks and matchmaking start-up

The runtime is offline: the one user is signed out of PSN for the whole run, and nothing about
that ever changes. The facts come from shadPS4's `libSceNpManager`, `libSceNpMatching2` and `libSceNpAuth`
(`np_manager.h`, `np_manager.cpp`, `np_types.h`, `np_types2.h`, `np_common.cpp`, `np_error.h`,
`np_auth/np_auth.cpp`, `np_auth/np_auth.h`), checked against two other Bloodborne ports and PS4Recomp's `libSceNp` (`ps4_np.c`), which answer the same for a signed-out user. The code is the
runtime's own.

| Call | Runtime |
|---|---|
| `sceNpRegisterStateCallback(callback, data)` | keeps it and never calls it: callbacks run only from `sceNpCheckCallback` when the sign-in state changes. A second registration answers `0x80550008`, no callback `0x80550003`. |
| `sceNpUnregisterStateCallback()` | 0, or `0x80550009` with none registered |
| `sceNpRegisterGamePresenceCallback(callback, data)` | keeps it (replacing any earlier one) and answers nothing; the caller (`0x010b8386`) reads no result |
| `sceNpGetState(user, uint32_t *state)` | 1, signed out (0 is not signed up, 2 signed in); a missing variable answers `0x80550003`, and a user the runtime did not hand out stops by name |
| `sceNpCheckCallback()` | 0, running nothing: offline nothing changes |
| `sceNpGetNpId(user, NP id *)`, `sceNpGetOnlineId(user, online id *)` | `0x80550006` (signed out), writing nothing: a signed-out user has no PSN identity. A missing variable answers `0x80550003`; the invalid user (-1, whose answer depends on the system software version) and a user the runtime did not hand out stop by name. The callers (`0x010bd072`, `0x010c0860`, `0x010b4666`, `0x010b4e5d`, `0x010b8931`) take their offline path. |
| `sceNpGetGamePresenceStatus(const online id *, int32_t *status)` | 0 with status 0 (offline; 1 is online) for every player; a missing argument answers `0x80550003` |
| `sceNpRegisterPlusEventCallback(callback, data)`, `sceNpUnregisterPlusEventCallback()` | the state callback's rules (`0x80550003`, `0x80550008`, `0x80550009`); kept and never called, since offline no PlayStation Plus event happens |
| `sceNpNotifyPlusFeature(const parameter *)` | 0: offline there is no one to tell. The callers (`0x01d38ac9`, `0x02275873`, `0x022769e6`) pass the 0x38-byte block of `sceNpCheckPlus` and read no result; a missing block stops by name. |
| `sceNpCreateAsyncRequest(const parameter *)` | a request id from `0x20000001`, at most 32 in use (`0x80550013`), a deleted one's slot reused. The block is 0x18 bytes `{size, CPU affinity mask, thread priority}`: no block `0x80550003`, another size `0x80550011`. The caller (`0x02274f54`) passes 0x18, `0x3f`, 700. |
| `sceNpCheckNpAvailability(id, const online id *, reserved)`, `sceNpCheckPlus(id, const parameter *, result *)`, `sceNpGetParentalControlInfo(id, const online id *, int8_t *age, info *)` | the check completes at once: the call answers 0 and the request's result is `0x80550006` (signed out); nothing is written. A request id of 0 or a missing argument `0x80550003`, an id that is no request `0x80550014`, an aborted request `0x80550012`, a second check on one request `0x80550003`. `sceNpCheckPlus`'s 0x38-byte block `{size, user, padding, features, 32 reserved}`: another size `0x80550011`; features outside 1–3, a non-zero reserved byte or the invalid user `0x80550003`; another user stops by name. A third argument to the availability check other than the observed 0 stops by name. |
| `sceNpPollAsync(id, int32_t *result)` | 0 with the result once the check ran (always at once offline); no variable `0x80550003`, an id that is no request `0x80550014`, a request that ran no check `0x80550015`. The caller (`0x022767b5`) reads 0 as done, 1 as still running. A request aborted before its check stops by name. |
| `sceNpAbortRequest(id)`, `sceNpDeleteRequest(id)` | 0, or `0x80550014` for an id that is no request. Abort marks an unchecked request aborted and leaves a checked one alone; delete frees the id. |
| `sceNpAuthCreateAsyncRequest(const parameter *)` (`libSceNpAuth`) | a request id from `0x10000001`, at most 16 in use (`0x80550305`), a deleted one's id reused. The block is 0x18 bytes `{size, CPU affinity mask, thread priority}`: no block `0x80550301`, another size `0x80550302`. The caller (`0x02275de5`) passes 0x18, `0x3f`, 700. |
| `sceNpAuthGetAuthorizationCode(id, const parameter *, code *, int32_t *issuer)` | `0x80550007` (user not found), writing nothing: the library looks up the user the online id belongs to, which cannot succeed signed out (shadPS4's note: a title cannot even obtain an online id then). The 0x20-byte block `{size, online id *, client id *, scope}` (caller `0x02276028`): no block or code variable, or a missing online id, client id or scope `0x80550301`; another size `0x80550302`. The request is not started. PS4Recomp answers `0x80550006` (signed out) here instead; the caller discards this answer either way and learns the outcome by polling. |
| `sceNpAuthPollAsync(id, int32_t *result)`, `sceNpAuthDeleteRequest(id)` | Poll: no variable or an id of 0 or below `0x80550301`, an id that is no request `0x80550306`, and otherwise `0x80550307` (invalid id): offline no request is ever started, so none has a result. Delete frees the id: 0, `0x80550301` or `0x80550306` as for poll. Callers `0x022752ca` (only an id above 0; a negative answer is its failure state, 1 still running, 0 done) and `0x02276f43`. |
| `sceNpCmpOnlineId(a, b)`, `sceNpCmpNpId(a, b)` (`libSceNpCommon`) | 0 for the same identity, `0x80550609` (no match) otherwise, `0x80550003` for a missing one. Online ids compare their 16 characters as `strncmp` does; NP ids then compare their 8 option and 8 reserved bytes. Callers `0x010a28d1` and `0x010b4676`. |
| `sceNpMatching2Initialize(const parameter *)` | the 0x30-byte block carries its own size at `0x20`, and 0x30 or the older 0x28 is accepted. Anything else, or no block, answers `0x80550C0A`; a second initialize answers `0x80550C02`. The caller (`0x010ba2d0`) treats a negative answer as failure. |
| `sceNpMatching2Terminate()` | 0, or `0x80550C03` before initialize |

## The PSN title id (`sceNpSetNpTitleId`)

`sceNpSetNpTitleId(const title id *, const title secret *)` registers the title's PSN title id
(12 characters and a terminator, in a 16-byte block) and its 128-byte title secret, which the
system would use to talk to PSN. A missing argument answers `0x80550003` (invalid argument). The
facts come from shadPS4's `libSceNpManager` (`np_types.h`, `np_error.h`); the other ports answer
0 too. The executable's call (`0x010ba192`) treats only a negative answer as failure.

Offline nothing talks to PSN, so the runtime keeps the title id (`rt_np_title_id`, for later
PSN-side calls such as trophies) and nothing of the secret.

## Trophies, offline (`libSceNpTrophy`)

The executable creates a trophy context for the initial user (service label 0, options 0) and a
handle at `0x02420524`, registers the context, and later unlocks trophies by id and reads the game
and trophy info. The facts (sizes, limits, error values) come from shadPS4's `libSceNpTrophy`
(`np_trophy.h`, `np_trophy.cpp`, `np_error.h`), checked against the other ports and PS4Recomp; the
code is the runtime's own (`runtime/syslib/np_trophy.c`).

| Call | Runtime answer |
|---|---|
| `sceNpTrophyCreateContext(int32_t *context, user, label, options)` | contexts from 1, one per (user, label), at most 8; a second for the same pair `0x80551613`, the ninth `0x80551623`; options other than 0 or no variable `0x80551604` |
| `sceNpTrophyCreateHandle(int32_t *handle)` | handles from 1, at most 4 (`0x80551624`) |
| `sceNpTrophyRegisterContext(context, handle, options)` | an unknown context `0x80551609`, an unknown handle `0x80551608`, a second registration `0x80551610` |
| `sceNpTrophyUnlockTrophy(context, handle, id, int32_t *platinum)` | ids 0 to 127 (others `0x8055160A`); records the unlock with its time; a repeat `0x8055160C`; `*platinum` is always -1; an unregistered context `0x8055160F` |
| `sceNpTrophyGetGameInfo`, `sceNpTrophyGetTrophyInfo` | the recorded unlocks: the unlocked count, and per trophy whether and when it was unlocked; names empty, grades 0; each structure must carry its own size, else `0x80551604` |

The trophy definitions (names, grades, which count toward the platinum) live in the title's
`sce_sys/trophy/trophy00.trp`, encrypted with a key the runtime does not hold, so it has none of
them and never awards a platinum. Unlocks are kept per user in
`<savedata>/<user>/sce_trophy/unlocked`, one `id tick` line each, and survive across runs when
`--savedata` names a folder.

## Content restriction (`sceNpSetContentRestriction`, issue #41)

The executable's only call, at `0x023b704f`, passes a parameter block it built on its stack (an
age from a table and a pointer to a restriction list), and it discards the result.

The call tells the system which online content the title restricts. The offline runtime offers no
online content, so there is nothing to record and it answers 0. A missing block stops by name.

## Thread variables fit in 40 bits (issue #34)

The executable keeps a thread variable's value in 40 bits. Its lock at `0x024368c0` records the
owner as `scePthreadSelf()` masked with `0xffffffffff`, then compares that with the whole answer.

A thread object above 2^40 would never own a lock it had just taken, and would wait on itself
(a local boot hung there). The host heap lies above 2^40, so the runtime allocates thread objects,
the main thread's included, from a range it reserves below it, at `0xf000000000`.

## Flexible memory (`sceKernelMapFlexibleMemory`, issue #34)

`sceKernelMapFlexibleMemory(void **address, size_t length, int32_t protection, int32_t flags)` maps
memory the system provides without a range of the direct memory pool. The signature is
corroborated by the OpenOrbis homebrew SDK's `libkernel.h`.

The executable's only call, at `0x02481dbe`:
- presets the address to 0;
- rounds the length up to 16 KiB;
- passes protection 3 or `0x33` and flags 0;
- on success, requires a non-null address.

**What the runtime does.** It maps fresh zeroed host memory at an address it picks, with the CPU
bits of the protection; GPU access is accepted, as for direct memory. The PS4 limits a title's
flexible memory to a budget. The budget is not established and the runtime does not enforce it.

**What stops by name:**
- an address hint;
- map flags;
- other protection bits;
- a length that is not whole 16 KiB pages;
- a missing variable.

## Sockets, the resolver and HTTP requests, offline (`libSceNet`, `libSceHttp`, issue #41)

**Sockets** are FreeBSD's socket calls under SCE names (shapes from OpenOrbis `net.h` and shadPS4's
`net.cpp`, facts only). A failure answers `0x80410100` plus the error number, with the number in
the network errno. The executable expects exactly that: it compares answers with `0x80410123`,
`0x80410139`, `0x80410109` and `0x80410104` at `0x02f9a8d0`, and errno with 35 at `0x011c7c60`.

A socket can be created, set up, bound and listened on, as FreeBSD 9 does with only its loopback
interface. shadPS4, the droogie bbhost runtime and PS4Recomp also create sockets. A family of
native ports refuses every socket call with ENETUNREACH instead, which breaks the executable's
Havok code: its bind and listen checks test for -1. Nothing ever connects or arrives:
- **Connect and send.** Connect, or a datagram to a non-loopback address, answers ENETUNREACH at
  once (FreeBSD 9's `in_pcbladdr`). An unconnected stream socket answers ENOTCONN to send and
  receive. A datagram socket with no destination answers EDESTADDRREQ to send.
- **Accept and datagram receive.** These answer EWOULDBLOCK on a nonblocking socket (`SO_NBIO`). A
  blocking socket waits until `sceNetSocketAbort` or `sceNetSocketClose` (EINTR), or until its
  accept or receive timeout option passes (EWOULDBLOCK, as shadPS4 does). An abort with no waiter
  answers ENOTBLK.
- **Bind and listen.** Bind answers EINVAL when the socket is already bound, EADDRNOTAVAIL for an
  address other than any or loopback, and EADDRINUSE for a port another socket holds, unless the new
  one set `SO_REUSEADDR` or `SO_REUSEPORT` (FreeBSD 9's `in_pcbbind`). Listen on a datagram socket
  answers EOPNOTSUPP.
- **Options.** The executable's options are stored and read back.
- **Epoll sets.** `sceNetEpollControl` adds a socket to a set (events 1 or 3) or removes it.
  Answers, from shadPS4: EINVAL for a bad operation or event; EBADF for an unknown set or socket, or
  for removing a socket the set does not hold; EEXIST for adding one twice. A closed socket leaves
  every set. Offline nothing ever becomes ready.

**The resolver.** With no DNS server, a name other than `localhost` answers RESOLVER_ENODNS,
`0x804101e1` (shadPS4's offline resolver). An IPv4 literal is converted unless flag `0x10000` is
set. A pool that is not live, including the 0 that the HavokScript debugger (`0x00dfd1b0`) passes,
answers EBADF.

**HTTP requests.** The executable's HTTP manager (`0x013d2740`) builds a connection and a request,
then sets the content length, timeout and waiting handle. Its state machine (`0x013d2970`) sends,
and treats any error other than busy, again or timeout as failure. The runtime builds both, with ids
from 1 that are never reused:
- `sceHttpSendRequest` answers `0x80431063` (network). PS4Recomp and the native port family answer
  the same; shadPS4 sends in the background and then answers `0x80431065`. Either way, the job ends
  failed.
- The response calls answer `0x80431065`.
- Ids that are not live answer `0x80431100`, a missing URL `0x80433060`, and NULL results
  `0x804311fe` (shadPS4's codes).

**What stops by name** (each needs a maintainer ruling or a real network to settle):
- type 6 peer-to-peer sockets and other families;
- binding port 0 (the PS4's ephemeral port is not known);
- connect or datagrams to loopback (no loopback stack);
- reading an option that was never set (the PS4's defaults are not known);
- asynchronous resolving, listening while unbound, and abort flags;
- non-HTTP URLs, methods other than GET, POST and PUT, and full tables.

## The network errno and epoll sets (`sceNetErrnoLoc`, `sceNetEpoll*`)

Facts from shadPS4's `libSceNet` (`net.cpp`, `guest_net.cpp`,
`net_error.h`, `net_types.h`) and from bbhost. The implementation is the runtime's own.

- **Errors:** a failing call answers `0x80410100` plus a FreeBSD error number, and sets the
  calling thread's network errno, whose address `sceNetErrnoLoc()` gives. That errno is separate
  from the C library's.
- **Offline:** an epoll set exists, but no socket ever becomes ready on it, so a wait ends only by
  its timeout, an abort or a destroy.

| Call | Runtime |
|---|---|
| `sceNetEpollCreate(name, flags)` | a new id from 1; flags other than 0 are `EINVAL` (`0x80410116`), a name over 31 characters `ENAMETOOLONG` (`0x8041013f`). The caller (`0x02b1575d`) keeps the id and reads the network errno on failure. |
| `sceNetEpollWait(id, events, max, timeout µs)` | 0 events after the timeout (negative waits for ever); `EINTR` (`0x80410104`) if an abort or destroy cuts it short; unknown id `EBADF`, no events or `max < 1` `EINVAL` |
| `sceNetEpollAbort(id, flags)` | interrupts the waiters; with none, `ENOTBLK` (`0x8041010f`), or with flag 1 the abort is kept and the next wait fails with `EINTR` at once; other flags `EINVAL` |
| `sceNetEpollDestroy(id)` | closes the set, interrupting its waiters; unknown id `EBADF` |

## Network library start-up (`sceNetInit`, `sceNetTerm`, `sceNetPoolCreate`, issue #41)

The names and argument shapes come from the OpenOrbis homebrew SDK's `Net.h`, which declares
`sceNetTerm` as returning nothing; the executable reads its result. The behaviour comes from the
executable's own calls:
- **`sceNetInit`.** Seven subsystems each call it once, behind a flag of their own. One
  (`0x011c7992`) accepts success or exactly `0x80410110`, and then does not terminate the library
  itself. So a repeat initialize answers `0x80410110`, "already initialized".
- **`sceNetTerm`.** Its callers (`0x011c79d9`) accept 0 or exactly `0x804101c8`. Which condition
  gives the second is not established, so terminating a library that is not initialized stops by
  name.
- **`sceNetPoolCreate`.** Every call passes a name and flags 0, treats a negative result as
  failure, and keeps the result as a pool id it passes on. The runtime hands out ids from 1.

The runtime is offline: initializing touches no network. A pool before initialize, without a name
or size, or with flags stops by name.

## SSL and HTTP library start-up (`sceSslInit`, `sceHttpInit`, issue #41)

`sceSslInit(size_t pool_size)` and `sceHttpInit(int32_t pool, int32_t ssl, size_t pool_size)`, as
the OpenOrbis homebrew SDK's `Ssl.h` and `Http.h` declare them.

The executable's HTTP manager (`0x013d1fc0` and `0x013d2050`):
- creates a network pool;
- then calls `sceSslInit` with a size;
- then calls `sceHttpInit` with the pool id, the SSL result and the same size.

It treats a negative result as failure, keeps each non-negative one as an id, and passes each id
back on teardown. So each call answers a context id; offline, a context is only an id.

**What stops by name:** an id the runtime did not hand out, and a size of 0.

## HTTP templates (`sceHttpCreateTemplate`, `sceHttpSetNonblock`, issue #41)

`sceHttpCreateTemplate(int32_t context, const char *user_agent, int32_t version, int32_t proxy)`,
as the OpenOrbis homebrew SDK's `Http.h` declares it. Its version list has 1 for HTTP/1.0 and 2
for HTTP/1.1.

The executable's only call (`0x013d1d32`) passes the HTTP context id, its user agent string,
version 2 and proxy 1. It keeps the result as a template id and passes it straight to the next
HTTP call. Offline, a template is only an id.

**What stops by name:**
- a context the runtime did not hand out;
- no user agent;
- a version other than HTTP/1.0 or 1.1;
- a proxy value other than the observed 1 (its meaning is not public).

`sceHttpSetNonblock(id, enable)` comes next. OpenOrbis names it without a signature, so its shape
comes from the executable's only call (`0x013d1d41`): the new template id and 1, with the result
discarded. The runtime records the choice and answers 0; offline, nothing ever blocks on a network.
An unknown id, and a value other than 0 or 1, stop by name.

## HTTP waiting handles (`sceHttpCreateEpoll`, issue #41)

OpenOrbis names `sceHttpCreateEpoll` without a signature, so its shape comes from the executable's
only use, a tail call at `0x013d1d82`. It passes the HTTP context id and the address of an
eight-byte slot preset to 0, and its caller (`0x023e4567`) discards the result.

A handle is an object the library allocates and the title only passes back, so the runtime
allocates one per call and answers 0. What the title later does with it (waiting on requests)
belongs to other calls, which stop by name until they are implemented.

**What stops by name:** an HTTP context the runtime did not hand out, and a missing slot.

## Network, SSL and HTTP teardown (`sceHttpTerm`, `sceSslTerm`, `sceNetPoolDestroy`, issue #41)

The executable's HTTP manager tears down what it set up, each call only for an id it got, and
none reads an answer:
- its connection object (`0x013d1d90`, `0x013d1e10`) calls `sceHttpAbortWaitRequest(handle)`,
  `sceHttpDestroyEpoll(HTTP context, handle)` and `sceHttpDeleteTemplate(template)`;
- the manager (`0x013d20d0`, `0x013d2120`, `0x013d2170`) calls `sceHttpTerm(HTTP context)`,
  `sceSslTerm(SSL context)` and `sceNetPoolDestroy(pool)`.

OpenOrbis' `Http.h` and `Net.h` give the `int32_t` id arguments of `sceHttpTerm`,
`sceHttpDeleteTemplate` and `sceNetPoolDestroy`; the others' come from these calls. The answers
are facts from shadPS4's `libSceHttp` (`http.cpp`, `http_error.h`):

| Call | Runtime |
|---|---|
| `sceHttpTerm(context)`, `sceHttpDeleteTemplate(template)` | 0, ending the id; `0x80431100` (invalid id) for one that is not live (never handed out, or already ended) |
| `sceHttpDestroyEpoll(context, handle)` | 0, freeing the handle; `0x80431100` for a context or handle that is not live, `0x804311fe` (invalid value) for no handle |
| `sceHttpAbortWaitRequest(handle)` | 0; `0x804311fe` for no handle, `0x80431100` for one that is not live. Every later `sceHttpWaitRequest` on the handle answers `0x80431080` (aborted). Offline no wait blocks, so there is no waiter to wake. |
| `sceSslTerm(context)`, `sceNetPoolDestroy(pool)` | 0, ending the id |

Ids are never reused. Using an ended pool or SSL context in `sceHttpInit`, an ended HTTP context
in `sceHttpCreateTemplate` or `sceHttpCreateEpoll`, and an ended template in the template calls,
stop by name as an unknown id does (`sceHttpsEnableOption` and `sceHttpsDisableOption` answer
`0x80431100`, as before).

**What stops by name:** ending an SSL context or pool that is not live (no source gives the
answer). shadPS4 answers `0x80431001` (before init) for the HTTP calls before any `sceHttpInit`;
the runtime does not model that state and answers `0x80431100` there, which the executable never
reaches.

## HTTPS options (`sceHttpsEnableOption`, `sceHttpsDisableOption`)

`sceHttpsEnableOption(id, flags)` and `sceHttpsDisableOption(id, flags)` set and clear HTTPS
flags on a template. The facts come from shadPS4's and PS4Recomp's `libSceHttp`, which agree:
- **The flags:** `0x01` verify the server, `0x02` verify the client, `0x04` check the common name,
  `0x08` and `0x10` check the not-after and not-before dates, `0x20` check for a known CA, `0x40`
  reuse sessions, `0x80` send SNI, and `0x2000`.
- **A new template's flags:** server verification, the common-name and known-CA checks, and SNI
  (`0xa5`).
- **Errors:** other bits answer `0x804311fe` (invalid value); an id that is no template answers
  `0x80431100` (invalid id).

The executable's one wrapper (`0x013d1e90`) enables or disables `0x01` on its template, and its
caller (`0x0227dade`) does not read the answer. Offline no request is sent, so each template
keeps its flags for when one is (`rt_http_template_ssl_flags`).

## HTTP request headers (`sceHttpAddRequestHeader`, issue #41)

`sceHttpAddRequestHeader(id, name, value, mode)`, as the OpenOrbis homebrew SDK's `Http.h`
declares it. The executable's only call (`0x013d1ed5`) adds a header to its template with mode 0,
and treats only 0 as success.

A header only matters to a request the library sends. Offline, no request is ever sent, because
every sending call stops by name. So the runtime checks the arguments and answers 0 without
keeping the header.

**What stops by name:**
- an id that is not one of the runtime's templates;
- a missing name or value;
- a mode other than the observed 0 (the modes are not public).

## Signal masks (`_sigprocmask`, issue #34)

`_sigprocmask(how, set, old)` is FreeBSD's `sigprocmask(2)`: the calling thread's blocked signals.
A FreeBSD `sigset_t` is four 32-bit words, with signal n at bit n - 1. `how` is `SIG_BLOCK` (1),
`SIG_UNBLOCK` (2) or `SIG_SETMASK` (3), and is ignored when there is no new set. SIGKILL and SIGSTOP
can never be blocked, and are dropped silently.

The bundled libc's `setjmp` (`libc.elf+0x2fcb0`) reads the mask with `SIG_BLOCK` and no new set, and
its `longjmp` (`libc.elf+0x2fd00`) puts it back with `SIG_SETMASK`.

**What the runtime does.** No signal is delivered to the guest yet. So the runtime keeps each guest
thread's mask, a new thread starting with its creator's, and does not apply it to the host thread.

**What stops by name:** an unknown `how`. Its error is reported through the guest's `errno`, which
the runtime does not model.

**`sigprocmask` and `sigfillset`** (`libc.elf` imports both from libkernel). `sigprocmask` is the
same call under its POSIX name (FreeBSD's libc `sigprocmask` is the system call itself); its
thirteen calls include `+0x53f84`, which blocks everything with `SIG_SETMASK` and a set from
`sigfillset` and keeps the old mask. `sigfillset(set)` puts every signal in the set, each of the four
words all ones (FreeBSD libc's `sigsetops.c`, POSIX), and answers 0. A missing set stops by name:
POSIX defines no error for it.

## Thread priorities (`scePthreadGetprio`, `scePthreadSetprio`, issue #34)

`scePthreadGetprio(thread, int32_t *priority)` and `scePthreadSetprio(thread, priority)`, as the
OpenOrbis homebrew SDK's `libkernel.h` declares them.

**What the executable does.** Its only reader (`0x02a3b59d`) reads the current priority before
choosing a new one from a class. It sets priorities from 256 (`0x100`) to 767 (`0x2ff`), at
`0x02a3b63d` and `0x0111ab46`.

**Where a thread's priority comes from:**
- **The main thread.** An executable may declare its main thread's settings in its process
  parameters (`sceUserMainThreadPriority` and the like, which homebrew titles define). Without
  them the main thread runs at `SCE_KERNEL_PRIO_FIFO_DEFAULT`, 700 (libretro's public
  `ps4_defines.h`). The executable passes the same 700 (`0x2bc`) as a priority to
  `sceUserServiceInitialize`.
- **This executable's block** holds no main thread settings: after relocation only the C library's
  parameter pointer (`+0x38`) is set, and `+0x18` to `+0x37` are empty.
- **Created threads.** A thread created without explicit scheduling inherits its creator's
  priority (FreeBSD libthr's default attribute, `PTHREAD_INHERIT_SCHED`). The executable sets none.

**What the runtime does.** It records priorities. The host schedules the threads, so priorities
change nothing else.

**What stops by name:**
- main thread settings in the process parameters (which slot is the priority is not established);
- a priority outside 256–767;
- a missing thread or variable.

## `clock_gettime` and `sceKernelClockGettime` (libkernel, issue #34)

`clock_gettime(clockid_t id, struct timespec *tp)` is POSIX, with FreeBSD's clock numbers and
amd64 `struct timespec` (two 64-bit fields, seconds and nanoseconds).

The bundled libc's `time()` (`libc.elf+0x2f030`) asks for clock 13 and returns its seconds. Clock
13 is FreeBSD's `CLOCK_SECOND`, which FreeBSD answers with the wall clock's whole seconds and 0
nanoseconds (`kern_clock_gettime`).

**What the runtime does.** It answers the clocks whose meaning FreeBSD fixes and the host has:
- `CLOCK_REALTIME` (0) and `CLOCK_SECOND` (13), from the host's wall clock;
- `CLOCK_MONOTONIC` (4), from the host's monotonic clock.

**What stops by name:** other clocks and a missing timespec. Their errors go through `errno`,
which the runtime does not model.

`sceKernelClockGettime(id, tp)` is the same call under its SCE name. It returns `0x80020000` plus
the error number: `EINVAL` for another clock and `EFAULT` for a missing timespec, as FreeBSD
answers them. The executable's call (`0x0111a815`) asks for clock 4, and treats anything but 0 as
failure.

## Process time (`sceKernelGetProcessTime`, issue #34)

`uint64_t sceKernelGetProcessTime(void)`, as the OpenOrbis homebrew SDK's `libkernel.h` declares
it, is the time since the process started.

Its unit is microseconds. The executable turns it into seconds by dividing by 1,000,000:
- at `0x00e4fb6e`, through the compiler's multiply by `0x431bde83` and shift by 50;
- at `0x0252607e`, through a division by the double 1000000.0.

It also seeds a random generator with it (`0x00835dba`).

**What the runtime does.** It measures from its own start, on the host's monotonic clock.

## Audio job manager (`libSceAjm`, issue #37)

AJM decodes compressed audio in batches of jobs that the title builds in a buffer and hands over.
The facts come from shadPS4's `libSceAjm`, checked against bbhost (a Bloodborne host, whose
observations of the title's FMOD are noted) and PS4Recomp. The ATRAC9 configuration word's layout
and tables are LibAtrac9's (MIT). The code is the runtime's own.

**Contexts, codecs and instances.**
- **Contexts:** `sceAjmInitialize(0, &context)` and `sceAjmFinalize(context)`.
- **Codecs:** `sceAjmModuleRegister(context, codec, 0)` and `sceAjmModuleUnregister`, with 0 MP3,
  1 ATRAC9 and 2 AAC.
- **Instances:** `sceAjmInstanceCreate(context, codec, flags, &instance)` gives an id that is the
  slot index with the codec at bit 14. Its flags hold a revision (bits 0–2, not 0), the channels
  (3–6), the sample format (7–9, 0 for 16-bit) and gapless looping (bit 10).
- **The title's instances:** it creates ATRAC9 instances with flags `0x409` to `0x441`: revision
  1, 1 to 8 channels, 16-bit, gapless looping (`0x01118d9a`).

**Jobs and batches.**
- **Building jobs:** `sceAjmBatchJobControlBufferRa` and `sceAjmBatchJobRunBufferRa` write a job
  into the batch and return the next free position.
- **A job** is a sequence of chunks, each with a 32-bit header: an identifier in bits 0–6 and a
  20-bit payload. The chunks are the job itself (payload the instance), an optional return
  address, input and output buffers (`{header, size, pointer}`) and the flags.
- **Fixed sizes:** a control job is 0x30 bytes (0x40 with a return address) and a run job 0x40
  (0x50). The title's FMOD lays jobs out at those strides.
- **Starting and waiting:** `sceAjmBatchStartBuffer` runs the batch (a size that is not a multiple
  of 8 is malformed, `0x80930011`). `sceAjmBatchWait` waits with a timeout in ms;
  `sceAjmBatchCancel` and `sceAjmBatchErrorDump` round it out.

**Job flags:**
- run: bit 11 get codec info, bit 12 multiple frames;
- control: bit 13 reset, bit 14 initialize, bit 15 resample;
- sideband: bit 45 gapless, bit 46 format, bit 47 stream.

A job's output control buffer starts with its result, then the stream result, format, gapless
state, frame count and codec info, as its flags ask. The ATRAC9 codec info is `{superframe bytes,
frames per superframe, bytes left in the superframe, samples per frame}`.

**What the runtime does.**
- **When jobs run:** when the batch is started, on the calling thread (bbhost does the same for
  this title). A wait finds the batch finished and frees its id; a cancel of a batch that has
  run does nothing.
- **Control jobs** are modelled: initialize with the 4-byte ATRAC9 configuration word, reset, and
  gapless. So is the codec info, derived from the configuration word: `0xFE`, a sample rate index,
  a channel configuration, a 0 bit, the frame bytes minus one, and a superframe index, read
  most-significant bit first.
- **Errors:** context `0x80930002`, instance `0x80930003`, batch `0x80930004`, parameter
  `0x80930005`, no batch id left `0x80930006`, no instance left `0x80930007`, already registered
  `0x80930009`, not registered `0x8093000A`, revision 0 `0x8093000B`.

**Decoding.** ATRAC9 is decoded with LibAtrac9 (MIT, vendored unmodified in
`third_party/libatrac9`), behind a small decoder interface (`runtime/ajm.h`). The decode loop
answers as shadPS4's does:
- **Before each frame:** too little output room answers "not enough room" (`0x10`), and less input
  than the rest of the superframe "partial input" (`0x8`).
- **Each frame:** it drops the gapless skip and stops at the gapless total. A superframe's padding
  is skipped, and when the gapless window ends mid-superframe the rest of it is decoded and dropped.
- **Looping and repeats:** an instance with gapless looping starts the window again. A job repeats
  frames only with "multiple frames".
- **Errors:** a decoder error is fatal, with its code as the internal result.

The tests use a stand-in decoder, because no ATRAC9 data may be in the repository, plus the real
one on junk input.

**Stops by name:** codecs other than ATRAC9, sample formats other than 16-bit, resampling, format
inputs and outputs, and the statistics instance.

## No microphone and no voice chat (`libSceAudioIn`, `libSceVoice`, issue #37)

**Microphone input.** The executable's only user is FMOD's PS4 record driver:
- **Opening.** Its record start (`0x0111c050`) opens a port with
  `sceAudioInOpen(user, type 0/1/5, index 0, 256 samples, 16000 Hz, param 0 = 16-bit mono)` at
  `0x0111c11d`. A negative answer is an FMOD error.
- **Reading.** Its update thread calls `sceAudioInInput` at `0x0111beee` and uses the answer as a
  sample count to advance a ring buffer, so an open port must never answer an error there.
- **Closing.** The port is closed at `0x0111c25b`, and the answer is ignored.

The facts come from shadPS4's `audioin.cpp` and `audioin_error.h` (facts only), whose codes match
the PS Vita SDK's `audioin.h`:
- A port opens without a device and reads silence. PS4Recomp and the droogie bbhost runtime agree;
  the bloodborne-native port family answers `0x80260109` (not opened) instead, so the majority answer
  is used.
- The length must be 1 to 2048 samples (`0x80260102`), the frequency 16000 or 48000 (`0x80260103`),
  and the param 0 or 1, 16-bit mono or stereo (`0x80260106`).
- An unknown handle is `0x80260101`, a NULL buffer `0x80260105`.

**What the runtime does with the microphone:**
- Open hands out handles from 1.
- Input writes the port's samples of silence, waits for the time they last (length / frequency),
  and answers the sample count.
- Close frees the handle.
- Stops by name: a type other than 0, 1 and 5; an index other than 0; a full table.

**Voice chat.** At `0x010c8000` the executable initializes the library (`sceVoiceInit(param,
100)`). It then creates a microphone port (type 0) and a speaker port (type 5) for the initial
user. Its microphone loop (`0x010c8a50`) reads only while `sceVoiceGetPortInfo` reports bytes.
Peer ports, connections, Start and the incoming packet path need an online peer. Teardown ignores
every answer.

shadPS4 (`voice.cpp`, `voice.h`), PS4Recomp (`ps4_voice.c`), the droogie bbhost runtime and the
bloodborne-native port family all answer success with no audio behind it (facts only). The port
info is 0x20 bytes: the byte count is at 0x10, and the frame size at 0x14 is 1 (shadPS4), which the
incoming packet path divides by. Their error codes disagree (shadPS4 `0x804e08xx`, the PS Vita's;
PS4Recomp `0x80233xxx`), so misuse stops by name.

**What the runtime does with voice chat:**
- **Answers 0.** Init; CreatePort, which hands out the lowest free id from 0; DeletePort, for any
  id, since the executable also deletes `0xff`; Connect, Disconnect, Start and Stop; End, which
  forgets every port.
- **GetPortInfo** writes the type, frame size 1 and byte count 0.
- **ReadFromOPort** reads 0 bytes, and **WriteToIPort** discards the data.

## Audio output (`sceAudioOutInit`, `sceAudioOutOpen`, `sceAudioOutSetVolume`, `sceAudioOutOutput`, issue #37)

The OpenOrbis homebrew SDK's `AudioOut.h` declares `int32_t sceAudioOutInit(void)` and
`int32_t sceAudioOutOpen(user, port type, index, length, sample rate, param)`. Its `_types`
headers give:
- port type 0 (main);
- format 4 (float stereo);
- the error codes `0x8026000E` (already initialized) and `0x8026000F` (not initialized).

**What the executable does:**
- Its only init call (`0x0111b665`) accepts success or exactly `0x8026000E`.
- Its only open (`0x0111b69d`) passes user `0xff` (the system user in the SDK's user types), the
  main port, index 0, 256 samples, 48000 Hz and a format. It keeps a non-negative result as the
  port handle.
- The format is the executable's own evidence. Its output callback (`0x0111b600`) accepts 2 or 8
  channels and passes `4 | (channels == 8)`. So 4 is float stereo (as in the SDK) and 5 is float
  with eight channels. The boot opens eight channels.
- A second open (`0x0111b6fc`) passes port type 1 with the same settings, and does not check the
  result. The PS4 name of type 1 is not public; the sibling Vita SDK calls it BGM.
- The movie player's sound gets a port of its own (`0x02c225ae`, `0x02c22842`): 1024 samples at
  48000 Hz. Its format comes from the movie's channel count: 0, 1 or 2 (16-bit mono, stereo, eight
  channels) for 48 kHz sound, and 3, 4 or 5 (float) otherwise. The SDK's `_types/audio_out.h`
  names 0, 1, 3 and 4; shadPS4's `audioout.h` adds 2 and 5 as the eight-channel forms. The
  6-channel movies open format 2. With `--audio host`, 16-bit blocks are scaled to -1..1 for the
  host stream.

**What the runtime does.** It records each open port and hands out handles from 1, the lowest one
free, so a closed port's handle is handed out again (shadPS4 also reuses a closed port's slot). Sound output
belongs to the output calls, which stop by name until they are implemented.

**What stops by name:** any open other than the observed ones (256 float samples in formats 4
and 5, or 1024 samples in formats 0 to 5, at 48000 Hz), and running out of handles.

`int32_t sceAudioOutSetVolume(handle, flag, int32_t *volume)`, as the SDK declares it, comes next.
The executable's only call (`0x0111b6d6`) passes its new port, flag `0xff` and eight volumes of
32768, and treats nonzero as failure. Flag bit n selects channel n, whose volume is `volume[n]`.

**What the runtime does.** It records the volumes.

**What stops by name:**
- a handle it did not hand out;
- a flag naming a channel the port does not have;
- a missing array;
- a volume outside 0–32768.

`int32_t sceAudioOutOutput(handle, const void *samples)`, as the SDK declares it, hands a port
sound. The executable's audio thread (`0x0111b93c`) hands its port one block at a time (`length`
samples for every channel). It advances its ring buffer when the result is not negative, and loops
with no wait of its own. So the call itself paces the thread: it returns once the port has room for
the next block.

**What the runtime does.** It plays nothing yet.
- Each port takes one block per `length / rate` seconds (256 samples at 48000 Hz: 5.33 ms).
- A call waits until the block before it would have finished playing, then answers 0 (the caller
  only checks the sign).
- A port that falls behind starts again from now.
- No block (`samples` NULL) hands nothing over: the call waits until the port has room for the next
  block and answers 0, without moving the port's timeline on. shadPS4 (`audioout.cpp`) waits for the
  port's buffer to drain and copies nothing for a null pointer, returning 0 samples; PS4Recomp
  (`ps4_audioout.c`) waits for room and pushes nothing, returning 0. The movie player's sound thread
  calls it so when it has no sound to give (found during the opening movie).

**What stops by name:** a handle the runtime did not hand out.

`int32_t sceAudioOutClose(handle)`, as the SDK declares it, closes a port. The movie player's sound
ends with it (`0x02c2276f`): first no block (`sceAudioOutOutput` with NULL, which waits for room), then
the close, whose negative result it takes as failure. The runtime takes the port back once any output
call on it has returned, closes its host stream and answers 0, as shadPS4's `audioout.cpp` does; the
handle is free for the next open. A handle that names no open port stops by name (the error value is
not established).

`int32_t sceAudioOutGetPortState(handle, void *state)`, as the SDK declares it, reports a port's
state. Its `_types/audio_out.h` gives the layout (32 bytes): a 16-bit output, an 8-bit channel
count, a volume and a 64-bit flag. The executable's only call (`0x0111bb41`) asks about its second
port, and reads only output, testing it for 0.

**What the runtime does.** It plays nothing on the host, so no port is output anywhere. It answers
output 0, and 0 in the fields whose encoding is not established. This must change when the runtime
plays sound.

**What stops by name:** an unknown handle, and a missing state.

**Sound on the host (`pbboot --audio host`, `tools/boot.py --audio host`).** With this option,
each port plays through SDL3 on the default playback device (`runtime/audio_host.h`,
`runtime/audio/audio_host_sdl.c`). Without it, or in a build without SDL3, nothing is played and
ports keep time on their own as before. Boots are silent by default.

- **Each block:** each channel is scaled by its volume (32768 is 0 dB, the level the title sets;
  until it sets one the port plays at 0 dB, the runtime's choice) and queued on the port's stream.
- **Pacing:** the call returns once no more than four blocks wait to be played, so the title's
  audio thread runs at the device's pace.
- **Channel order:** the title's 8-channel float format (format 5) is passed in SDL's 7.1 order,
  as shadPS4's SDL backend does; the STD layouts, which are remapped, are not used by the title.
- **Port state:** a port playing on the host reports output 1 (`CONNECTED_PRIMARY`, shadPS4's
  `audioout.h`).
- **Observed:** with ATRAC9 decoding, a boot plays the game's own sound after about 17 seconds.

## The movie player (`libSceAvPlayer`)

The title plays its movies (`/app0/.../movie/*.mp4`: H.264 video, AAC sound in several languages)
through the console's movie player. One wrapper in the executable makes every call
(`0x02cfdf33` to `0x02d004a9`):

1. `sceAvPlayerInit` gets a 0x78-byte block with the title's memory callbacks and an event callback.
   The block has no file callbacks, auto start is 0 and the default language is none.
2. `sceAvPlayerPostInit` gets decoder settings (video decoder type 3).
3. `sceAvPlayerAddSource` gets the movie's path and `sceAvPlayerSetLooping` a flag.
4. After the ready event the title counts the streams and reads each one's info. It enables one
   video stream and the audio stream its language setting picks, then calls `sceAvPlayerStart`.
5. Each frame, a picture comes from `sceAvPlayerGetVideoDataEx` and sound from
   `sceAvPlayerGetAudioData` on the title's audio thread.

The layouts, event numbers and error values are from shadPS4's `libSceAvPlayer` (`avplayer.h`,
`avplayer_error.h`, `avplayer_state.cpp`, `avplayer_source.cpp`), checked against the OpenOrbis
`AvPlayer.h`. The code is the runtime's own (`runtime/syslib/avplayer.c`). Decoding uses FFmpeg on
the host (`runtime/media/`, `runtime/media_host.h`), when the runtime is built with it.

| Call | What the title passes and reads | Runtime answer |
|---|---|---|
| `sceAvPlayerInit(const InitData *)` | memory callbacks {allocate, deallocate, allocate texture, deallocate texture}, an event callback, no file callbacks | a handle; file callbacks, auto start, a missing allocator or event callback stop by name |
| `sceAvPlayerPostInit(handle, data)` | decoder types and parameters | 0 (they change how the console decodes, not the result) |
| `sceAvPlayerAddSource(handle, path)` | the movie under `/app0` | opens and probes it, sends event 2 (ready), 0; a movie the runtime cannot open stops by name |
| `sceAvPlayerStreamCount`, `sceAvPlayerGetStreamInfo` | the type (0 video, 1 audio), an audio stream's channels and rate | the movie's video and audio streams, durations in ms |
| `sceAvPlayerEnableStream(handle, index)` | one video stream and one audio stream | 0 |
| `sceAvPlayerStart` | | event 3 (playing); the movie's clock starts |
| `sceAvPlayerGetVideoDataEx(handle, FrameInfoEx *)` | the picture's address, width, height and four crops; it lays the planes out from the width | true once the clock reaches the next picture. The picture is NV12 in a buffer from the title's texture allocator (alignment 0x100), with pitch = width rounded up to 64 and height rounded up to 16, cropped back to the picture |
| `sceAvPlayerGetAudioData(handle, FrameInfo *)` | address, size in bytes, channels, rate | true with up to 1024 interleaved 16-bit sample frames, handed out up to 0.1 s ahead of the clock |
| `sceAvPlayerIsActive`, `sceAvPlayerCurrentTime` | | true until a movie that does not loop has handed out its last frame, then event 1 (stopped) once; the clock in ms, standing still while paused |
| `sceAvPlayerPause`, `sceAvPlayerResume`, `sceAvPlayerStop`, `sceAvPlayerClose` | | events 4, 3 and 1; Close frees the buffers through the title's callbacks |

The runtime decodes a few frames ahead on a host thread of its own, so FFmpeg never runs on a guest
thread's small stack. The guest calls only copy finished frames into the title's buffers. A looping
movie starts again with its clock carried on. Without FFmpeg, opening a movie stops by name.

## Attribute priority and CPU mask (`scePthreadAttrSetschedparam`, `scePthreadAttrSetaffinity`, issue #34)

`scePthreadAttrSetschedparam(attr, const struct sched_param *param)` sets the priority
(`sched_param`'s one int) of a thread created with the attribute. OpenOrbis' `libkernel.h` gives
the shape; libthr's `_pthread_attr_setschedparam` gives the semantics.

**What the executable does.** Its thread wrapper (`0x0111aafd`) sets it without explicit-scheduling
inheritance. So under libthr's default (`PTHREAD_INHERIT_SCHED`), the new thread still starts at
its creator's priority. The wrapper then sets the priority itself with `scePthreadSetprio`
(`0x0111ab46`).

**What the runtime does.** It records the priority on the attribute. A variable holding no
attribute is `EINVAL` (libthr).

**What stops by name:** a missing param, and a priority outside 256–767.

`scePthreadAttrSetaffinity(attr, uint64_t mask)` is the wrapper's next call (`0x0111ab0e`), with
the CPU mask its caller chose. As for `scePthreadSetaffinity`, the runtime records the mask (the
created thread takes it) and the host schedules. An empty mask stops by name, and a variable
holding no attribute is `EINVAL`.

## Save data (`libSceSaveData`)

The title's saves live in the host folder given to `pbboot --savedata`. `tools/boot.py` gives
each run a new empty one, so boots start alike, unless `--savedata DIR` names a folder to keep
them in. A mounted save appears to the title as `/savedataN` (N the lowest free of 16), through
the same file calls as `/app0` and `/data`, read-only or writable as mounted.

**Sources.** shadPS4's `libSceSaveData` (`savedata.cpp`, `savedata.h`, `savedata_error.h`,
`save_instance.cpp`, `save_memory.cpp`), checked against two other Bloodborne ports and the OpenOrbis
headers. Where they disagree, shadPS4's answer is taken. The code is the runtime's own.

**The title's SDK version decides some answers.** The executable says SDK `0x02000071`
(firmware 2.00), which the runtime reads through `rt_compiled_sdk_version`:
- "not initialized" is `0x80960002` (it is `0x809F000B` before 2.00 and `0x809F0001` from 2.50);
- a directory search reports the total hits, the number set and the params (from 1.70);
- a mount writes back only the mount point (before 4.50), and mode `0x20` (create or open)
  does not create;
- terminating with saves mounted unmounts them (before 4.00).

**On the host.** `<savedata>/<user id>/<directory name>/` holds the title's files, with
`sce_sys/param.bin` (the 1328-byte param) and `sce_sys/icon0.png`; a save exists when its
`sce_sys` does. The memory-backed save is `<savedata>/<user id>/sce_sdmemory/memory.dat`. These
are the runtime's own files, not a console's `param.sfo`.

| Call | Runtime |
|---|---|
| `sceSaveDataInitialize(params)` | 0; the parameter block (a priority, `0x2bc` from the caller at `0x02c3c50e`) is ignored, as the library does |
| `sceSaveDataTerminate()` | 0, unmounting any save still mounted; "not initialized" before initialize |
| `sceSaveDataMount(request, result)` | the 80-byte original request. Modes: 1 read-only, 2 read-write, 4 create, 8 destruct-off, `0x10` copy the package's icon. Answers: not there and no create `0x809F0008`; create when it exists `0x809F0007`; already mounted `0x809F0003`; 16 mounted `0x809F000C`; user below 0 `0x809F0011`; a bad name (empty, unterminated, `/`, `\`, leading `.`) `0x809F0000` |
| `sceSaveDataUmount(mount point)` | 0, or `0x809F0008` for a mount point not mounted |
| `sceSaveDataSetParam(mount point, type, data, size)` | types 0 (whole param), 1 title, 2 subtitle, 3 detail, 4 user param, written to `param.bin` with the time; type 5 or more `0x809F0000`; not mounted `0x809F0004` |
| `sceSaveDataSaveIcon(mount point, icon)` | writes the icon (the smaller of buffer and data size); read-only mount `0x809F000D` |
| `sceSaveDataDirNameSearch(condition, result)` | the user's saves (folders with `sce_sys/param.bin`, not `sce_*`), matched by an SQL-LIKE pattern without regard to case (`%`, `_`), sorted by name, user param or time (blocks are alike here), ascending or descending; a key above 5 or an order above 1 `0x809F0000` |
| `sceSaveDataSetupSaveDataMemory(user, size, param)` | creates or grows `memory.dat` to the size (never shrinks it); size 0 `0x809F0000` |
| `sceSaveDataGet/SetSaveDataMemory(user, buffer, size, offset)` | reads or writes at the offset; before setup `0x809F0012`; no buffer or a negative offset `0x809F0000` |
| `sceSaveDataDelete(request)` | the 64-byte request { user, padding, title id, directory name, unused, reserved }; removes the save's folder and everything in it, 0. Mounted `0x809F0003`; no request, no name or a bad name `0x809F0000`; user below 0 `0x809F0011`. The executable's one caller (`0x02c3e765`) passes no title id and asks again while the answer is `0x809F0014` |

**Stops by name** (not modelled): another title's saves (a title id), the free-space check and its
required-blocks answer, the backup copy and its "corrupted" marker, sort key 4, and a memory read
or write past the size set up, a delete of a save that is not there (shadPS4 answers 0 then, which
is not established for the console), and a host failure to remove a save. The save data dialog
stays unbound until the boot reaches it.

## The title's files: `/app0`, `sceKernelStat`, opening and reading (issue #35)

The title reads its own data under `/app0`, the console's read-only mount of its package. The
runtime serves `/app0` from your own copy and never writes to it:
- `tools/boot.py --app0 DIR` chooses the copy, defaulting to `$BB_APP0`, then
  `$BB_DATA_ROOT/merged`.
- `pbboot` takes it as `--app0`.
- A path under any other mount, and one with a `..` component (which could leave the copy), stops by
  name.

`sceKernelStat(path, struct stat *)` is FreeBSD's `stat(2)` under its SCE name, which returns
`0x80020000` plus the error number. The PS4's `struct stat` is FreeBSD 9's amd64 layout, 120 bytes
(OpenOrbis' `_types/kernel.h`, `OrbisKernelStat`).

**What the executable does.** Its call at `0x02474519` asks whether
`/app0/dvdroot_ps4/sound/sprj_mixer.fev` exists:
- 0 means yes;
- exactly `0x80020002` (`ENOENT`) means no;
- anything else goes to its error path.

**What the runtime does.** It fills the structure from the host file:
- type, size, block count and size, link count and times;
- the permission bits without write (the mount is read-only);
- device and inode numbers as the host's (they only identify the file);
- the birth time as the modification time (the host's stat has none);
- owner, group, flags and generation 0.

**Errors.** A missing file answers `ENOENT`, and a missing directory component `ENOTDIR` (the same
numbers on FreeBSD and Linux). Any other host error stops by name.

**POSIX `stat` and `errno`.** `eboot` also imports POSIX `stat` from `libScePosix`. The runtime
gives it the same lookup and answer, the POSIX way: 0, or -1 with the calling thread's `errno`
set to `ENOENT` (2) or `ENOTDIR` (20). The executable's one call (`0x00465431`) reads the access
and modification times and the size on 0, and treats any other answer as no file.

`errno` is libkernel's `__error()`, FreeBSD's `int *__error(void)` behind the `errno` macro. It
is imported by `eboot`, `libc` and `libSceFios2`. Each guest thread runs on its own host thread,
so a thread-local `int` is that thread's `errno`; it starts at 0, and success leaves it alone.

### Truncating by name, and the POSIX mkdir, rmdir and rename

`sceKernelTruncate(path, length)` (imported by `eboot` and `libSceFios2`) is FreeBSD's
[`truncate(2)`](https://man.freebsd.org/cgi/man.cgi?query=truncate&sektion=2&manpath=FreeBSD+9.0-RELEASE)
under its SCE name: 0, or `0x80020000` plus the error number. The shape is OpenOrbis
`libkernel.h`'s; the executable's wrapper (`0x024733a4`) passes a 64-bit length and turns each
`0x8002xxxx` answer into an engine error through a table (`0x02473530`). `eboot` also imports
[`mkdir`](https://man.freebsd.org/cgi/man.cgi?query=mkdir&sektion=2&manpath=FreeBSD+9.0-RELEASE),
[`rmdir`](https://man.freebsd.org/cgi/man.cgi?query=rmdir&sektion=2&manpath=FreeBSD+9.0-RELEASE) and
[`rename`](https://man.freebsd.org/cgi/man.cgi?query=rename&sektion=2&manpath=FreeBSD+9.0-RELEASE)
from `libScePosix`: 0, or -1 with `errno` set. The executable's calls (`0x00df0bf4`, `0x00e03e68`,
`0x00e03ddb` and others) ignore the answer or only compare it with 0. (`sceKernelRename` and
`sceKernelFtruncate` are under "Opening and reading".)

**What the runtime does:**
- **Paths** resolve as for opening: `/data`, the mounted save data, and `/app0`.
- **Errors** are FreeBSD's numbers, never the host's: `ENOENT`, `ENOTDIR`, `EEXIST`, `EISDIR`,
  `EINVAL` and `ENOTEMPTY` (66 on FreeBSD, 39 on Linux).
- **`rmdir`** of a mount's own root is `EBUSY` (the man page's "mount point for a mounted file
  system").
- **`rename`** across mounts is `EXDEV`, after the source and the target's folder are looked up,
  in FreeBSD's order (stable/9
  [`vfs_syscalls.c`](https://cgit.freebsd.org/src/tree/sys/kern/vfs_syscalls.c?h=stable/9),
  `kern_renameat`). A directory over a non-empty one is `ENOTEMPTY`.
- **`truncate`** with a negative length is `EINVAL` before the lookup (`kern_truncate`; Linux checks
  it first too); a directory is `EISDIR`. Growing a file fills with zero bytes.
- **A new directory's mode** is `mode & 0777` less the host's umask. The PS4 process's umask is
  not established.

**What stops by name:**
- no path;
- any change under a read-only mount (`/app0`, save data mounted to read). FreeBSD answers
  `EROFS`, but only after lookups whose order the runtime does not model for each call, and the
  title's callers change only `/data` and its saves;
- renaming a mount's root;
- any other host error (`EACCES`, `ENOSPC`, `EIO` and the rest).

### Opening and reading

`sceKernelOpen`, `sceKernelRead`, `sceKernelPread`, `sceKernelLseek`, `sceKernelFstat` and
`sceKernelClose` are FreeBSD's `open(2)`, `read(2)`, `pread(2)`, `lseek(2)`, `fstat(2)` and
`close(2)` under their SCE names. The OpenOrbis homebrew SDK's `libkernel.h` declares the shapes.
Each returns a result, or `0x80020000` plus the error number. The executable opens its data with
flags 0 (`O_RDONLY`) and mode 0555.

**What the runtime does:**
- **Opening.** It opens the host file read-only. A request to write, create, truncate or append
  answers `EROFS`, as FreeBSD does on a read-only mount. The lookup comes first: a missing file
  without `O_CREAT` is `ENOENT`. Other flag bits stop by name.
- **Descriptors.** A descriptor is the runtime's own number, from 3 up, standing for a host
  descriptor (the title only passes it back). One the runtime did not hand out is `EBADF`.
- **Seeking.** A whence other than 0, 1 or 2 (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`, the same on
  FreeBSD and Linux), and a seek before the start, are `EINVAL`.

### Writing

`sceKernelWrite(fd, buf, n)` (imported by `eboot` and `libSceFios2`) and
`sceKernelPwrite(fd, buf, n, offset)` (imported by `libSceFios2`) are FreeBSD's `write(2)` and
`pwrite(2)` under their SCE names, with the shapes from the same `libkernel.h`. Each returns the
number of bytes written, or `0x80020000` plus the error number. Writes reach files under the
writable `/data` mount, which `sceKernelOpen` opens with the requested access mode.

**What the runtime does:**
- **Order of checks.** FreeBSD's order (stable/9 `sys_generic.c`, `sys_write`, `sys_pwrite` and
  `kern_pwritev`): the size, then the descriptor, then the offset.
- **`EBADF`.** `write(2)` answers it for a descriptor that is not open for writing: one the runtime
  did not hand out (0 to 2 included, as for reading), and one opened read-only, which is every
  `/app0` file and a `/data` file opened with `O_RDONLY`.
- **`EINVAL`.** A negative `pwrite` offset, as for `pread` (`pwrite(2)`: "The offset value was
  negative").
- **The position.** A `pwrite` leaves the file position where it was. A write past the end leaves a
  hole that reads as zero bytes, as the host file system does. A zero count writes nothing and
  answers 0.
- **The count.** The answer is the host's count, which can be short. Linux writes at most
  `0x7ffff000` bytes per call, so a request between that and `INT_MAX` comes back short where
  FreeBSD would write it all.

**What stops by name:**
- **More than `INT_MAX` bytes.** FreeBSD answers `EINVAL` above `IOSIZE_MAX`. That is `INT_MAX`
  while the `debug.iosize_max_clamp` sysctl is set (its default in FreeBSD 9; current FreeBSD
  defaults to 0), and `SSIZE_MAX` otherwise. The FreeBSD 9.0 `write(2)` page says `INT_MAX`. The
  PS4's setting is not known.
- **No buffer for a non-zero count,** as for reading.
- **A `pwrite` on a descriptor opened with `O_APPEND`.** POSIX writes at the offset. FreeBSD's
  `write(2)` BUGS section says its `pwrite` appends instead, and Linux does the same. The PS4's
  answer is not established.
- **Any host write error** (`ENOSPC`, `EIO`, `EFBIG` and the rest).

What would settle the stops: guest call sites that pass these cases, or results observed on a
console.

### Truncating and renaming

`sceKernelFtruncate(fd, length)` is FreeBSD's `ftruncate(2)` under its SCE name (shape from the
OpenOrbis homebrew SDK's `libkernel.h`). The title reaches it after Play Offline, while it creates
its save data: its file object cuts its file at its current position (`0x02473290`). The runtime
passes the length to the host. A negative length, and a descriptor opened without write
permission, are `EINVAL` (FreeBSD's `truncate(2)`; Linux answers the same); one the runtime did not
hand out is `EBADF`. Any other host error stops by name.

`sceKernelRename(from, to)` is FreeBSD's `rename(2)` (shape from `libkernel.h`); the title calls it
(`0x02474a85`) while it creates its save data. The runtime renames the host file, replacing an
existing `to` as `rename(2)` does. Paths on two different mounts are `EXDEV` (`rename(2)`: "on
different logical devices"); on the read-only `/app0` `EROFS`, after the lookup; a missing file or
directory component `ENOENT` or `ENOTDIR`. Any other host error stops by name.

### Directories and flushing

`sceKernelGetdirentries(fd, buf, nbytes, basep)` is FreeBSD's `getdirentries(2)` and
`sceKernelFsync(fd)` FreeBSD's `fsync(2)`, under their SCE names (shapes from the OpenOrbis
homebrew SDK's `libkernel.h`). `eboot` and `libSceFios2` import the first, `libSceFios2` the
second.

**How the executable lists a directory.** Its directory iterator opens the directory with
`sceKernelOpen(path, 0x20000, 0555)`, that is `O_DIRECTORY` (`0x024722d0`), and treats a negative
answer as no listing. Each step (`0x02472480`) takes `st_blksize` from the directory's
`sceKernelFstat` as `nbytes`, then walks the FreeBSD 9 records: a 32-bit file number, the 16-bit
record length, the type, the name length and the NUL-terminated name. It skips `.` and `..`, adds
`/` to a `DT_DIR` (4) name, and, when it stops partway through a buffer, seeks the descriptor back
to `*basep` so that its next call reads the same records again.

**What the runtime does:**
- **Opening.** `O_DIRECTORY` alone opens a directory on either mount. A path that is not a
  directory is `ENOTDIR` (`open(2)`).
- **Listing.** It lists the host directory in the host's order, as many whole records as fit, each
  rounded up to 4 bytes (FreeBSD 9's `GENERIC_DIRSIZ`), and answers the byte count, 0 at the end.
  File numbers are the host's inode numbers (they only identify a file). The type is the host's:
  FreeBSD and Linux share the `DT_` numbers. Where the host file system leaves it unknown, the
  runtime asks `fstatat`.
- **Positions.** `*basep` (when not NULL) is the position before the call, and the position
  afterwards follows the last record listed. So a seek to `*basep` lists the same records again. A
  position is a count of entries, the runtime's own number; FreeBSD's is a byte offset. The title
  only hands it back to `sceKernelLseek`, which on a directory accepts `SEEK_SET` and `SEEK_CUR`;
  a position before the start is `EINVAL`.
- **Flushing.** `sceKernelFsync` flushes the host file and answers 0.
- **Errors.** A descriptor the runtime did not hand out is `EBADF` for both.

**What stops by name:** `O_DIRECTORY` with any other flag; listing a descriptor that is not a
directory, with no buffer, or into fewer than 512 bytes (a directory block: shadPS4 answers
`EINVAL`, the PS4's answer is not established); `SEEK_END` on a directory; a host `fsync` error.

An entry added or removed between two calls can move the listing by one entry, as on any file
system that lists by position.

### Directories and removing files

`sceKernelMkdir(path, mode)`, `sceKernelRmdir(path)` and `sceKernelUnlink(path)` are FreeBSD's
`mkdir(2)`, `rmdir(2)` and `unlink(2)` under their SCE names, 0 or `0x80020000` plus the error
number. `eboot` and `libSceFios2.elf` import all three from `libkernel` (`symbols/imports.csv`).
The shapes come from the OpenOrbis homebrew SDK's `libkernel.h` (`int32_t sceKernelMkdir(const
char *, OrbisKernelMode)`, `OrbisKernelMode` a `uint16_t` like FreeBSD's `mode_t`;
`int32_t sceKernelRmdir(const char *)`, `int32_t sceKernelUnlink(const char *)`), supporting
evidence for the shapes only. No call site in the repository shows them yet.

**Under `/data` and a writable mount** the host does the work, and its error is translated:
- a missing file or directory component is `ENOENT` or `ENOTDIR`, and `mkdir` of an existing
  name `EEXIST` (the same numbers on FreeBSD and Linux);
- `rmdir` of a directory with entries is `ENOTEMPTY`, 66 on FreeBSD (Linux's is 39), and of a
  path whose last component is `.` `EINVAL` (`rmdir(2)`; Linux answers the same);
- `unlink` of a directory is `EPERM`. FreeBSD's `unlink(2)` page lists both `EISDIR` and `EPERM`
  for "the named file is a directory"; the kernel settles it: `kern_unlinkat` (stable/9) and
  `kern_funlinkat` (current) answer `EPERM` ("POSIX"), and turn the lookup's `EINVAL` for a last
  component `.` into `EPERM` too. Linux answers `EISDIR` for both, which the runtime translates.
- Any other host error stops by name.

**The mode of `mkdir`.** FreeBSD gives the new directory `(mode & ACCESSPERMS) & ~umask`
(`kern_mkdirat`, stable/9 and current), so bits above 0777 are dropped. The runtime passes
`mode & 0777` to the host, as `sceKernelOpen` does for a new file, and the host process's umask
then applies. Not established: the PS4 process's umask, so the bits a later `sceKernelStat` shows
are the host's answer, not a console's; and whether the console enforces those bits for the title
(a folder made without the owner's write and search bits refuses files on the host, which then
stop by name).

**On a read-only mount** (`/app0`, a save mounted read-only) nothing changes, and the answers follow
FreeBSD's lookup order (`vfs_lookup.c`, stable/9 and current):
- a missing component of the path prefix is `ENOENT` or `ENOTDIR` first;
- `mkdir` of an existing name is `EEXIST` (the read-only check is not made for a name that
  exists), of a new name `EROFS`;
- `rmdir` and `unlink` of an existing name, file or directory, are `EROFS`, before `rmdir`'s
  `ENOTDIR` and `ENOTEMPTY` and `unlink`'s `EPERM` for a directory;
- a last component `.` is refused by the lookup before that: `rmdir` `EINVAL`, `unlink` `EPERM`.

**Stops by name**, besides the path checks every file call shares (no path, a path outside the
mounts, a `..` component, a path longer than the runtime's buffer): `rmdir` of a mount point itself
(`/data`, `/app0`, `/savedataN`). FreeBSD's `rmdir(2)` answers `EBUSY` for the root of a mounted
file system, but whether `/data` is one on the console is not established, and the host would
remove the runtime's own folder.

## The disc map (`sceDiscMapIsRequestOnHDD`, issue #35)

The disc map tells the file library whether part of a file is already on the hard disk or still
only on the disc (`runtime/syslib/disc_map.c`).

- **Shape:** `sceDiscMapIsRequestOnHDD(const char *path, int64_t offset, int64_t bytes, int32_t
  *on_hdd)`, as shadPS4's `disc_map.cpp` declares it (a fact only).
- **The caller:** the bundled file library's one call (`libSceFios2.elf+0x2d71c`) passes a
  variable preset to 0. On 0 it takes a nonzero variable as on the hard disk; on any other answer
  it treats the request as not mapped and remembers that the map is unavailable.
- **The answer:** the runtime serves the title from host files with no disc, so there is no disc
  bitmap. It answers `0x81100004` ("no bitmap info", numbered in shadPS4's `disc_map_codes.h`),
  as shadPS4 does, and writes nothing. A missing path or variable stops by name.
- **The unnamed import `ioKMruft1ek`.** Its name is not public, so it is registered by its NID
  with `RT_SYSLIB_NID` (`runtime/include/runtime/syslib.h`): the loader matches the import's hash
  directly, and `tools/runtime_issues.py` checks the row `ioKMruft1ek#A#B`. The file library calls
  it as `(path, offset, bytes, int *, int *, int *)` (`libSceFios2.elf+0x29c6f`, `+0x2a381`) and
  takes any non-zero answer as "no map". It answers `0x81100004` and writes nothing, as shadPS4
  and the bloodborne-native ports do (PS4Recomp answers `0x80820001`); the maintainer's ruling of
  2026-11-10 picked `0x81100004`. A missing path or variable stops by name.

## Memory protection queries (`sceKernelQueryMemoryProtection`, issue #34)

`sceKernelQueryMemoryProtection(void *address, void **start, void **end, int32_t *protection)`, as
the OpenOrbis homebrew SDK's `libkernel.h` declares it, gives the protection of the mapping that
holds an address.

**What the executable does.** Its helpers (`0x02abad98`, `0x02abadf8`) pass no start or end. They
test the protection's CPU read bit (1) and GPU read bit (`0x10`), on memory the game mapped itself.
Another (`0x02d031df`, reached once the movie player runs) passes a start and an end and reads only
the protection.

**What the runtime does.** It records every direct and flexible mapping with the protection the
guest asked for, GPU bits included, and answers that. A start gets the mapping's start, and an end
the address one past its last byte (exclusive), as shadPS4's `MemoryManager::QueryProtection` and
PS4Recomp's `ps4_direct_mem.c` both answer.

**What stops by name:**
- an address outside those mappings (other memory is not tracked yet);
- a missing variable.

## GNM: the tessellation factor ring (`sceGnmGetTheTessellationFactorRingBufferBaseAddress`, issue #39)

Graphics are rebuilt at the engine level (issue #39), so the GNM calls answer what the engine
expects back; the GPU is not emulated.

The OpenOrbis homebrew SDK's `GnmDriver.h` only names this call. The executable's only call
(`0x02ad3fb4`) takes a 64-bit address and maps 128 KiB of direct memory there, for the GPU's
tessellation factors: `sceKernelMapDirectMemory` with that address as a hint, protection `0x33`,
alignment `0x10000`.

**What the runtime does.** The PS4 answers a fixed address of its own. The runtime answers one it
reserves for this, `0xfe00000000`: below 2^40, and clear of the executable, modules and thread
objects. It is the runtime's choice, not the PS4's value: the engine only maps it and hands it back
to the GPU calls.

For this, `sceKernelMapDirectMemory` now takes an address hint as FreeBSD's `mmap` does without
`MAP_FIXED`: the map goes there when the range is free. A hint in use (where the system then maps
is not established) and one off the alignment stop by name.

## Virtual memory queries (`sceKernelVirtualQuery`, issue #34)

`sceKernelVirtualQuery(address, flags, info *, size)`, as the OpenOrbis homebrew SDK's `libkernel.h`
and `_types/kernel.h` (`OrbisKernelVirtualQueryInfo`) declare it, describes the mapping that holds
an address. The answer is a 0x48-byte structure:

| Offset | Field |
|---|---|
| `0x00`, `0x08` | start and end |
| `0x10` | the direct memory offset |
| `0x18` | protection |
| `0x1c` | memory type |
| `0x20` | flag bits: flexible 1, direct 2, stack 4, pooled 8, committed `0x10` |
| `0x24` | a 32-byte name |

**What the executable does.** Every call passes flags 0 and size 0x48, and reads the start, the
offset or the direct bit:
- `0x02d0328c` works out an address's place in direct memory as `offset + (address - start)`;
- `0x02ad3f5e`, `0x02ad4066` and `0x02ad423e` release the direct memory at the offset.

**What the runtime does.** It answers from its record of direct and flexible mappings: start,
offset, protection as asked for, memory type, flexible or direct, committed, and no name. No call
reads the end, and whether it is inclusive is not established; the runtime writes the address just
past the mapping.

**What stops by name:**
- flags other than 0;
- a size other than 0x48;
- an address outside those mappings (other memory is not tracked yet);
- a missing structure.

## Unmapping, protecting and typing memory (`sceKernelMunmap`, `sceKernelMprotect`, `sceKernelGetDirectMemoryType`, issue #34)

The shapes are OpenOrbis `libkernel.h`'s: `sceKernelMunmap(address, length)`,
`sceKernelMprotect(address, length, protection)` and `sceKernelGetDirectMemoryType(start, &type,
&range_start, &range_end)`.

**What the executable does:**
- It frees a direct map with `sceKernelMunmap` and then `sceKernelReleaseDirectMemory`
  (`0x024826fd`, `0x0248270c`), the order the release needs.
- `0x02d03214` wraps `sceKernelMprotect` and answers whether the result was 0.
- `0x02abae74` passes the pool offset `sceKernelVirtualQuery` gave for an address to
  `sceKernelGetDirectMemoryType` and only tests whether the type is 3 (write-combined garlic).

**What the runtime does:**
- **Munmap** is FreeBSD's [`munmap(2)`](https://man.freebsd.org/cgi/man.cgi?query=munmap&sektion=2&manpath=FreeBSD+9.0-RELEASE).
  A zero length is `EINVAL` (FreeBSD's `sys_munmap`; shadPS4's `sceKernelMunmap` answers the
  same, facts only). As in FreeBSD the range may span several mappings, reserved space and holes:
  every mapping the runtime made inside it goes (a direct map no longer holds its range, which can
  then be released), reserved space inside it is given back, and holes stay as they are. A
  GPU-visible range can be mapped again at its GPU address. The address is not handed out again
  for other maps.
- **Mprotect** is FreeBSD's [`mprotect(2)`](https://man.freebsd.org/cgi/man.cgi?query=mprotect&sektion=2&manpath=FreeBSD+9.0-RELEASE):
  the mapping takes the new protection, the CPU bits on the host and the GPU bits recorded, as for a
  map. The queries answer the new protection.
- **GetDirectMemoryType** answers the memory type of the allocated range that holds the pool
  offset, and that range's start and end (just past it, as for `sceKernelVirtualQuery`).

**What stops by name:**
- part of a mapping; for munmap, a start off the 16 KiB page, or a range outside the runtime's map
  window that touches nothing it mapped or reserved; for mprotect, more than one mapping or memory
  the runtime did not map (other memory is not tracked yet);
- an offset outside every allocated range (shadPS4 answers `ENOENT`; no public source settles it);
- protection beyond CPU and GPU read and write;
- a missing variable.

## Reserving address space and mapping into it (`sceKernelReserveVirtualRange`, `sceKernelBatchMap`, `sceKernelReleaseFlexibleMemory`, issue #34)

The executable's block allocator (`0x02d01eb0`) builds one contiguous view of blocks it got
separately. It reserves room for all of them (`sceKernelReserveVirtualRange`, flags `0x80`, no
overwrite, alignment 0); for each block it asks `sceKernelVirtualQuery` where the block lies in
direct memory, then maps an alias of every block at its place in the reserved range in one
`sceKernelBatchMap`. To free the view (or after a failed batch) it turns every entry into an unmap,
runs the batch again, and unmaps the whole reserved range, which by then is holes.

- **`sceKernelReserveVirtualRange(&address, length, flags, alignment)`** reserves inaccessible space
  in the runtime's map window (shadPS4 maps it with no access). `EINVAL` (`0x80020016`) for no
  variable, a length that is 0 or not whole 16 KiB pages, and an alignment that is not 0 or a
  power-of-two multiple of 16 KiB up to 2 GiB (shadPS4's checks, shared by the bloodborne-native
  ports). An address hint and flags other than no overwrite stop by name.
- **`sceKernelBatchMap(entries, count, &processed)`** runs 32-byte operations (start `0x00`, offset
  `0x08`, length `0x10`, protection `0x18`, type `0x19`, operation `0x1c`; shadPS4's `memory.h`) in
  order until one fails, and writes how many succeeded when `processed` is given. Each map is
  placed at its start exactly: shadPS4, the bloodborne-native ports and droogie's host agree on the
  implicit `MAP_FIXED`. No list, a zero length or an operation above 4 answers `EINVAL`.
  - *Map direct (0)* maps the pool range over reserved space, with the CPU bits.
  - *Unmap (1)* is the munmap above. *Protect (2)* is `sceKernelMprotect`.
  - Stops by name: a map outside reserved space (what a fixed map replaces is not modelled), an
    aliased map with GPU access (the GPU reaches each pool range at one address,
    `runtime/gpu_memory.h`), and the flexible-map and type-protect operations (3, 4), which the
    executable does not use.
- **`sceKernelReleaseFlexibleMemory(address, length)`** unmaps (maintainer's ruling of
  2026-11-10; the bloodborne-native ports and droogie's host unmap, shadPS4 keeps the mapping and
  drops its pages). The allocator frees whole flexible blocks with it (`0x02482721`). `EINVAL` for
  no address, a zero length or one that is not whole 16 KiB pages; direct memory in the range stops
  by name.

## GNM: binding vertex and pixel shaders (`sceGnmSetVsShader`, `sceGnmSetPsShader`, issue #39)

`sceGnmSetVsShader` and `sceGnmUpdateVsShader` take (command buffer, dwords, registers, modifier);
`sceGnmSetPsShader` and `sceGnmUpdatePsShader` take (command buffer, dwords, registers). The
argument order is shadPS4's `libSceGnmDriver` (`gnmdriver.cpp`). A boot shows the registers argument
holding the shader's register block (7 words for a vertex shader, 12 for a pixel shader; see
Paleblood's `docs/RENDERER.md`).

| Detail | Answer |
|---|---|
| The reserved space | one PM4 NOP packet, as for the other command-buffer writers |
| The shader | handed to the renderer, placed at the command-buffer address (runtime/renderer.h); nothing without the display |
| The vertex shader's modifier | replaces `PGM_RSRC1`'s SGPR count (bits 6-9) and input VGPR count (bits 24-25): (`PGM_RSRC1 & 0xfcfffc3f`) OR the modifier, as shadPS4 applies it. The game's vertex shaders pass one (they call a fetch shader). A modifier with any other bit stops by name |
| A null pixel shader block | no pixel shader (a depth-only pass): recorded as such, and a draw with it stops in the renderer |
| No vertex shader registers | stops by name |

## GNM: queries, debug calls and freeing compute queues (issue #39)

- **Compute queues.**
  - `sceGnmMapComputeQueueWithPriority` maps as `sceGnmMapComputeQueue`. The runtime runs no GPU
    queues, so the priority changes nothing (shadPS4, the deadinside28 port and droogie bbhost
    ignore it too).
  - `sceGnmUnmapComputeQueue(id)` (teardown at `0x0148c81c`, answer ignored) forgets the queue and
    answers 0. A later map takes the freed slot first. An id that names no mapped queue stops by
    name.
- **`sceGnmAreSubmitsAllowed`** answers 1. The console refuses submissions only while the system
  holds the GPU (shadPS4: "no submission lock held"; bbhost answers 1).
- **`sceGnmFlushGarlic`** is a memory fence. The runtime's GPU reads the CPU's memory.
- **`sceGnmGetEqEventType(event)`** answers the kevent's data field (offset 16; shadPS4 and
  deadinside28 agree). A NULL event stops by name.
- **`sceGnmSetGsRingSizes(esgs, gsvs)`** answers 0 for sizes from 4 to 8 MiB in whole MiB, and
  `0x08000000` otherwise (shadPS4's check). No geometry shader stage runs.
- **The mip statistics report** follows shadPS4.
  - Setup answers `0x80000001` for no buffer or one not 64-byte aligned. shadPS4 also refuses
    addresses past 40 bits, the console GPU's range; the runtime's memory is the host's, so that
    check is left out.
  - Setup answers `0x80000002` for a size under 0x1040 or one not 0x40 past a 2 KiB multiple.
  - Otherwise setup zeroes the buffer, writes 0x40 to its first word and answers 0. Request and
    Disable answer 0.
- **The Razor capture tools** exist only on development kits. The retail answers (shadPS4):
  - `sceRazorIsLoaded` and `sceGnmDriverCaptureInProgress` answer 0;
  - `sceGnmDriverTriggerCapture` answers `0x80d15001`;
  - the four `sceRazorCapture*` calls answer `0x80d1500f`.

**The last six (maintainer's rulings of 2026-11-10).** Five of them are unreachable in the stock
game: no instruction or relocated pointer in the executable reaches their wrappers (`0x01489a40`,
`0x01489a50`, `0x01489a70`, `0x01489b40`, `0x01489b70`).
- **`sceGnmAddEqEvent(queue, id, udata)` and `sceGnmDeleteEqEvent(queue, id)`** register and remove
  a graphics-core event (filter -14) whose ident and data are the id (shadPS4). The GPU's
  completion step raises it, after the work before the packet, on the renderer's thread (or at once
  with the display off). This happens when an `EVENT_WRITE_EOP` or `RELEASE_MEM` asks for an
  interrupt (select bits 24 and up: 1 interrupt only, 2 after the write, 3 for `RELEASE_MEM`).
  Graphics submissions raise id `0x40`, the end of pipe. A compute queue's buffers raise its pipe
  number, 0 to 6 (shadPS4's interrupt numbers). No queue answers `EBADF` (`0x80020009`). Deleting
  an event that is not registered, an EOP select of 3 and a compute pipe above 6 stop by name.
  Event queues gained `rt_equeue_remove` (`runtime/include/runtime/equeue.h`). The renderer is
  untouched: the step is gnm.c's own.
- **`sceGnmRequestFlipAndSubmitDone(command buffer, bytes, handle, index, mode, argument)`** takes
  shadPS4's six-argument shape (droogie's host takes four; the GNM header and the workload variant
  agree on six). Fewer than `0x100` bytes answers `0x80d11001` (not enough resources). Otherwise it
  writes one NOP over the first `0x100` bytes and submits them with the flip as the step after
  them, as `sceGnmSubmitAndFlipCommandBuffers` does.
- **`sceGnmGetEqTimeStamp` and `sceGnmDebugHardwareStatus`** answer 0 and write nothing. shadPS4
  (no arguments) and droogie's host (a pointer they write through) agree on the answer, not on the
  arguments.
- **`sceGnmLogicalCuIndexToPhysicalCuIndex(shader engine, index)` and
  `sceGnmLogicalCuMaskToPhysicalCuMask(shader engine, mask)`** map by identity. The performance
  counter set-up (`0x01482804` and eleven more; `0x014818fa`, `0x01481eb4`) passes the shader
  engine first and reads back a byte and a 16-bit mask, only when `sceGnmIsUserPaEnabled` is on,
  which it never is.

## GNM: compute queues (`sceGnmMapComputeQueue`, issue #39)

OpenOrbis' `GnmDriver.h` only names this call, so its shape comes from the engine's wrapper
(`0x01489a80`, called from its compute queue setup at `0x0148c7b0`).

**What the engine does:**
- The wrapper passes the pipe and queue from the queue object, the ring buffer, its size in dwords,
  and the address of the GPU's read pointer.
- A negative result is failure. Otherwise the wrapper keeps the result as the queue's id, and sets
  the read pointer to 0.
- The setup then reports success only when that id is not 0.

**What the runtime does.** It does not run the GPU, so the ring is never read. It records each
queue for the engine-level graphics layer, and answers ids from 1.

**What stops by name:** mapping the same pipe and queue twice, and a missing ring or read pointer.

**The renderer and compute queues.** `sceGnmDingDong` completes each indirect buffer the ring points
to (the temporary stand-in in `runtime/syslib/gnm.c`), then hands it to the renderer, which drops its
recorded calls without running them (`rt_renderer_skip`): compute queues do not run on the renderer
yet. Dropping them matters because a shader bound in a command buffer is recorded by its address. A
compute queue's buffer is never submitted to the graphics queue, so before this its bindings stayed
waiting, and a graphics buffer later built in the same memory picked them up: on the main menu after
Play Offline, a stale compute shader replaced the one the graphics buffer had just bound.

## Kernel event queues (`sceKernelCreateEqueue`, `sceKernelWaitEqueue`, `sceKernelDeleteEqueue`, issue #34)

The PS4 kernel is FreeBSD's, whose kqueue delivers `struct kevent`: ident (64-bit), filter (16-bit),
flags (16-bit), fflags (32-bit), data (64-bit) and udata (a pointer), 32 bytes on amd64. The OpenOrbis
homebrew SDK's `libkernel.h` declares create and delete; its `_types/kernel.h` lists the filters
(display -13, graphics core -14, ...).

**What the executable does.** Right after asking video out for a flip, it waits with room for one
event, a variable for how many came back, and no timeout (`0x015e41b6`, `0x02ad6f26`). So the wait
blocks until the flip is done.

**What the runtime does:**
- The queue is a list of registrations, added and triggered by the runtime's own event sources
  (`runtime/include/runtime/equeue.h`; video out is the first).
- A wait returns the triggered events, oldest first, and clears them, so each is reported once per
  trigger.
- An event's flags and fflags are 0: what the PS4 sets there is not established, and no caller reads
  them.

**What stops by name:**
- a timeout (no call passes one);
- deleting a queue a thread is waiting on;
- an event array or count that does not fit;
- the error values.

## Video out (`libSceVideoOut`, issue #38)

The OpenOrbis homebrew SDK's `VideoOut.h` declares the calls. Its `_types/video.h` gives:
- the 40-byte buffer attribute: format, tiling mode, aspect, width, height, pitch in pixels;
- the 64-byte flip status: flip count, two times, the flip argument, pending counts, current buffer;
- flip rates (0: 60 Hz, 1: 30 Hz, 2: 20 Hz) and flip modes (1: vsync, 2: hsync).

**What the executable does.** It sets video out up twice, in the same order (`0x015e488e` and
`0x02ad61d4`):
1. open, with (0, 0, 0, no parameter);
2. flip rate 0;
3. an event queue with a flip event;
4. a buffer attribute (format `0x80000000`; 1920 × 1080 in the first);
5. its buffers.

Each frame, it submits a flip with mode 2 and waits on the queue (`0x015e419a`, then
`0x015e41b6`). Its other loop (`0x02ad6ee9`) reads the flip status until the flip argument reaches
the frame it submitted.

**What the runtime does.** It has no display yet. It keeps each port's buffers and status, and an
hsync flip completes at once:
- the current buffer and flip argument change, and the flip count grows;
- the port's flip events are triggered.

The flip event's ident is 0, and its data the flip argument. The PS4's values are not established,
and no caller reads them.

**What stops by name:**
- a vsync flip, and other flip modes or rates;
- a flip status before the first flip (its initial argument is not established);
- other open arguments;
- errors.

The buffer-attribute setter and buffer registration share the 40-byte
`rt_video_out_buffer_attribute` layout. The setter copies all six input DWORDs
without interpreting them and clears the 16 reserved bytes; unaligned output is
supported. The public API is void. The independently examined native leaf also
leaves the zero-extended pitch in RAX, which this implementation preserves for
callers that observe it. A null output stops by the API name before any store.

## Implementation rule

Implement the established contract and test the `RT_SYSLIB` entry through its library and symbol name. Keep target-specific values separate from host values. When an argument, ID, return value, or error case is unknown, fail loudly with the call name and leave the question open; never choose a value to make `tools/boot.py` advance.

## Module TLS (`__tls_get_addr`)

Boot assigns TLS module IDs in mapped-object order, beginning at one, and prepares
all nonempty templates before module entry. The executable reuses its static TLS
block: the base is the thread pointer minus its rounded allocation size. Other
blocks copy the relocated initialization bytes, zero-fill the rest and respect
alignment. Lookup allocates nothing. Each context belongs to its host thread;
unbinding and rebinding retain values, and destruction preserves borrowed storage.
`scePthreadCreate` installs static executable TLS but no module context, so module
lookups on those threads stop as `__tls_get_addr`.

### Evidence and limits

The libkernel library SHA-256 is
`b3e13917da667d01a353ccf49025d6b1308a32cf86f3f351f96b3a13cd5c7c2f`.
Its `__tls_get_addr` export, NID `vNe1w4diLCs`, begins at ELF address `0x289c0`.
The body reads a two-word 64-bit module/offset index, consults TCB/DTV and linker
state, masks a slot to 48 bits and adds the full offset without a bounds check.
Invalid states can diagnose and execute `INT 0x44`. Generation updates, lazy
allocation, alternate modes and linker helpers remain unsupported. Adjacent helpers
at `0x2a9a0` and `0x2aca0` do not establish a general allocation-residue rule.
The title libc template has virtual-address residue 8, while LibcInternal has zero;
this implementation aligns the block base to the template's declared alignment.

Paleblood accepts full-width IDs and offsets through the template's logical size,
including a one-past pointer. Unknown IDs, larger offsets and absent contexts stop
by name. These bounds are runtime support limits, not native error behavior.
There is no native DTV layout, lazy loading or unloading. The initialization model
follows the [TLS ABI](https://www.akkadia.org/drepper/tls.pdf) and
[Oracle linker documentation](https://docs.oracle.com/cd/E19120-01/open.solaris/819-0690/chapter8-70/index.html).
No other PS4 runtime implementation was used.

### Validation

The loader rejects duplicate headers, initialized size beyond logical size,
out-of-file bytes, inconsistent segment aliases, invalid alignment and templates
outside the mapped image. Zero alignment becomes one; TBSS need not be readable.
Boot checks allocation arithmetic and allocates aligned storage.
`test_module_tls` checks the registry, initialization, owned 256- and 4096-byte
alignment, zero-fill, stable
writes, borrowed storage, separate threads, full-width requests and named stops.
`test_module_tls_boot.py` runs synthetic AMD64 fixtures through boot, checking
static/dynamic main-block aliasing, rounded sizes, module-ID relocations, relocated
pointers, constructor persistence and rejected IDs. Loader tests cover malformed
metadata.

## References

- [POSIX `clock_gettime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_gettime.html) and [clock definitions](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/time.h.html)
- [POSIX `getpid`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpid.html)
- [Older POSIX `gettimeofday` specification](https://pubs.opengroup.org/onlinepubs/009604599/functions/gettimeofday.html)
- [RFC 2822 section 3.3, date and time specification](https://www.rfc-editor.org/rfc/rfc2822#section-3.3); [PS4Recomp `libSceRtc`](https://github.com/wie-project/PS4Recomp/tree/main/pkg/runtime/modules/libSceRtc) and [shadPS4 `rtc.cpp`](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/rtc/rtc.cpp) (facts only: the year-1 tick epoch, the SDK-dependent `sceRtcSetTime_t`, the two-digit day, the network tick answered from the clock)
- [FreeBSD `getpagesize(3)`](https://man.freebsd.org/cgi/man.cgi?query=getpagesize&sektion=3&manpath=FreeBSD+13.3-RELEASE)
- [ELF ABI, program headers and `p_align`](https://refspecs.linuxfoundation.org/elf/gabi4%2B/ch5.pheader.html) (loadable segments must be congruent modulo the page size; `p_align` is the alignment the loader honors)
- [FreeBSD AMD64 `param.h`](https://github.com/freebsd/freebsd/blob/releng/9.1/sys/amd64/include/param.h) (stock `PAGE_SIZE` 4096) and [Sony's open-source page for the PS4](https://www.playstation.com/en-us/oss/ps4/)
- [FreeBSD `clock_gettime(2)`](https://man.freebsd.org/cgi/man.cgi?query=clock_gettime&sektion=2&manpath=FreeBSD+15.1-STABLE) and [`gettimeofday(2)`](https://man.freebsd.org/cgi/man.cgi?query=gettimeofday&sektion=2&manpath=FreeBSD+14.4-RELEASE)
- [Linux `getpagesize(2)`](https://man7.org/linux/man-pages/man2/getpagesize.2.html), [`getpid(2)`](https://man7.org/linux/man-pages/man2/getpid.2.html), [`clock_gettime(2)`](https://man7.org/linux/man-pages/man2/clock_gettime.2.html), and [`gettimeofday(2)`](https://man7.org/linux/man-pages/man2/gettimeofday.2.html)
- [PSP2SDK `sceNetHtonl`/`sceNetHtons`/`sceNetNtohl`/`sceNetNtohs` declarations](https://psp2sdk.github.io/net_8h.html)
- [FreeBSD `htonl(3)`/`htons(3)`/`ntohl(3)`/`ntohs(3)`](https://man.freebsd.org/cgi/man.cgi?format=html&query=htons&sektion=3)
- [POSIX `sched_yield`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_yield.html), [FreeBSD `sched_yield(2)`](https://man.freebsd.org/cgi/man.cgi?query=sched_yield&sektion=2) and [FreeBSD `pthread_yield(3)`](https://man.freebsd.org/cgi/man.cgi?query=pthread_yield&sektion=3); [Linux `pthread_yield(3)`](https://man7.org/linux/man-pages/man3/pthread_yield.3.html) for the `int`-returning form
- [ISO C (N1570 draft) 7.24.2.1 `memcpy` and 7.24.6.1 `memset`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
- [FreeBSD `usleep(3)`](https://man.freebsd.org/cgi/man.cgi?query=usleep&sektion=3) and [SUSv2 `usleep`](https://pubs.opengroup.org/onlinepubs/7908799/xsh/usleep.html)
- [POSIX `sleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sleep.html), [FreeBSD `sleep(3)`](https://man.freebsd.org/cgi/man.cgi?query=sleep&sektion=3) and FreeBSD libc [`sleep.c`](https://cgit.freebsd.org/src/tree/lib/libc/gen/sleep.c) (the `INT_MAX` split and the rounded-up remainder)
- [POSIX `nanosleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nanosleep.html), [FreeBSD `nanosleep(2)`](https://man.freebsd.org/cgi/man.cgi?query=nanosleep&sektion=2) and FreeBSD [`kern_time.c`](https://cgit.freebsd.org/src/tree/sys/kern/kern_time.c) (`kern_nanosleep` in stable/9, `kern_clock_nanosleep` in main: the `EINVAL` check, the zero or negative time, when `rmtp` is written)
- [FreeBSD 9.0 `write(2)`](https://man.freebsd.org/cgi/man.cgi?query=write&sektion=2&manpath=FreeBSD+9.0-RELEASE) and [current `write(2)`](https://man.freebsd.org/cgi/man.cgi?query=write&sektion=2) (`write` and `pwrite`: `EBADF`, `EINVAL` for the size and a negative offset, the `O_APPEND` note in BUGS), and FreeBSD stable/9 [`sys_generic.c`](https://cgit.freebsd.org/src/tree/sys/kern/sys_generic.c?h=stable/9) (`sys_write`, `sys_pwrite`, `kern_pwritev`: the order of the checks)
- [POSIX `pthread_rwlock_rdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_rdlock.html), [`pthread_rwlock_wrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_trywrlock.html), [`pthread_rwlock_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_unlock.html), [`pthread_rwlock_destroy`/`init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_destroy.html); FreeBSD libthr [`thr_rwlock.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_rwlock.c) and [`thr_private.h`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_private.h) (`THR_RWLOCK_DESTROYED`); FreeBSD kernel [`kern_umtx.c`](https://github.com/freebsd/freebsd-src/blob/main/sys/kern/kern_umtx.c) (`do_rw_rdlock`, `do_rw_wrlock`, `do_rw_unlock`)
- [LSB Core `__stack_chk_fail`](https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/libc---stack-chk-fail-1.html) and [FreeBSD libc `libc_stack_protector.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libc/secure/libc_stack_protector.c) (`__stack_chk_fail`, `__fail`)
- [POSIX `pthread_cond_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_wait.html), [`pthread_cond_signal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_signal.html), [`pthread_cond_destroy`/`pthread_cond_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_destroy.html); FreeBSD libthr [`thr_cond.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_cond.c), [`thr_mutex.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_mutex.c) (`_mutex_owned`, `_mutex_cv_unlock`, `mutex_unlock_common`, `mutex_self_lock`, `mutex_self_trylock`) and [`thr_private.h`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_private.h) (`THR_COND_DESTROYED`); for `pthread_cond_timedwait`, [POSIX `pthread_cond_timedwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_timedwait.html) (the `CLOCK_REALTIME` default, `ETIMEDOUT`, `EINVAL`), [`pthread_condattr_getclock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_getclock.html), libthr `thr_cond.c` (`_pthread_cond_timedwait` in stable/9 and main, `cond_init`) and [`thr_init.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_init.c) (`_pthread_condattr_default`)
- [vitasdk/vita-headers `psp2/net/netctl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/net/netctl.h) - PS Vita, a different operating system: shape of the `SceNetCtl` declarations, never a PS4 value
- [PSP2SDK `psp2/net/netctl.h` file reference](https://psp2sdk.github.io/netctl_8h.html) and [vitasdk `SceNetCtl` user group](https://docs.vitasdk.org/group__SceNetCtlUser.html) - the same declarations, with no prose describing any return value in this group
- [vitasdk/vita-headers `psp2/error.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/error.h) - `0 on success` as the documented SCE-family return convention
- [OpenOrbis PS4 toolchain `Sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Sysmodule.h) and [`_types/sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/sysmodule.h) - the `sceSysmoduleLoadModule`, `sceSysmoduleIsLoaded` and `sceSysmoduleUnloadModule` declarations and return convention, and the system module ids (after [psdevwiki, "Libraries"](https://www.psdevwiki.com/ps4/Libraries))
- [POSIX `pthread_attr_setdetachstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getdetachstate.html) - the detach state semantics, the invalid-value error, and the recommended `EINVAL` for an uninitialised attribute
- [POSIX `pthread_self`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_self.html) and [`pthread_attr_init`/`pthread_attr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_destroy.html)
- [FreeBSD `pthread_attr(3)`](https://man.freebsd.org/cgi/man.cgi?query=pthread_attr_setdetachstate&sektion=3), [libthr `thr_attr.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_attr.c) (`_pthread_attr_setdetachstate`, `_pthread_attr_get_np`), [`thr_create.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_create.c) (`_pthread_create`: the default attribute for a null or empty variable) and [`include/pthread.h`](https://cgit.freebsd.org/src/tree/include/pthread.h) (`PTHREAD_CREATE_JOINABLE`, `PTHREAD_CREATE_DETACHED`)
- [OpenOrbis `include/orbis/_types/pthread.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/pthread.h) - the PS4 homebrew SDK's `ORBIS_PTHREAD_CREATE_*` values, which follow FreeBSD's
- FreeBSD 9.0 [`mkdir(2)`](https://man.freebsd.org/cgi/man.cgi?query=mkdir&sektion=2&manpath=FreeBSD+9.0-RELEASE), [`rmdir(2)`](https://man.freebsd.org/cgi/man.cgi?query=rmdir&sektion=2&manpath=FreeBSD+9.0-RELEASE), [`unlink(2)`](https://man.freebsd.org/cgi/man.cgi?query=unlink&sektion=2&manpath=FreeBSD+9.0-RELEASE) and [current `unlink(2)`](https://man.freebsd.org/cgi/man.cgi?query=unlink&sektion=2); FreeBSD [`vfs_syscalls.c`](https://cgit.freebsd.org/src/tree/sys/kern/vfs_syscalls.c?h=stable/9) (stable/9 and main: `kern_mkdirat` and its mode, `kern_rmdirat`/`kern_frmdirat`, `kern_unlinkat`/`kern_funlinkat` and `EPERM` for a directory) and [`vfs_lookup.c`](https://cgit.freebsd.org/src/tree/sys/kern/vfs_lookup.c?h=stable/9) (stable/9 and main: the read-only checks and the `.` check, and their order); [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h) and [`_types/kernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/kernel.h) (`OrbisKernelMode`) for the shapes
