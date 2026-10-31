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

## Interfaces and known behavior

| Function | Arguments and representation | Publicly documented result and errors | Evidence and open questions for PS4 |
|---|---|---|---|
| `getpagesize(void)` | No arguments. Returns the target's page size in bytes. The numeric value is platform-specific. | `getpagesize` is not in current POSIX; portable code is directed to `sysconf(_SC_PAGESIZE)`. Linux and FreeBSD document the byte count; their manuals do not define a general error return for this function. Linux exposes it as a libc interface, not necessarily a dedicated kernel syscall. FreeBSD notes the system page size may differ from the underlying hardware page size. | The import is present twice (`eboot` from `libScePosix`, `libc.elf` from `libkernel`), so both library names are registered against one implementation. The target images bound the value but do not pick one: every `PT_LOAD` of the eight hash-verified ELFs has `p_align` 0x4000 with `p_offset` congruent to `p_vaddr` modulo 0x4000, and the ELF ABI's congruence rule then limits the system page size to a divisor of 16384 (4096, 8192 or 16384); `p_align` is the alignment the image requires, not the page size the system reports. Stock FreeBSD on AMD64 uses 4096, and Sony's open-source page confirms the FreeBSD kernel is used but publishes the upstream sources, not the console's modifications, so no public source names the PS4's own value. The runtime entry is therefore registered with `RT_SYSLIB_UNRESOLVED`, so a boot that reaches it names the call, and it does not count as implemented (`runtime/syslib/kernel.c`). What would settle it: one observed `getpagesize()` or `sysconf(_SC_PAGESIZE)` result on a console, or a guest call site whose page arithmetic shows the granularity the system uses. See the section "getpagesize: observed, inferred, unknown" below; do not hard-code the build host's page size.
| `getpid(void)` | No arguments. Returns a `pid_t` naming the calling process. | POSIX specifies that it succeeds and has no error return. Linux process IDs are scoped by PID namespaces; other systems have their own process-ID scope and lifecycle rules. | The import is present, but this repository does not establish the guest-visible process ID or its scope. Returning the host PID is not justified by the API contract alone. |
| `clock_gettime(clock_id, tp)` | `clock_id` selects a clock; `tp` points to a `struct timespec`. On the POSIX interface, `tv_sec` is seconds and `tv_nsec` is nanoseconds in `[0, 1,000,000,000)`. The exact target structure layout still needs ABI evidence. | POSIX uses `0` for success and `-1` with `errno` for failure. An unsupported clock ID is an `EINVAL` case. Linux and BSD manuals document additional pointer-error details; use the target contract rather than assuming the host's errno mapping. | The imported wrapper is present. The verified `sceKernelClockGettime` call requests ID `4` (`CLOCK_MONOTONIC`). That is evidence for the SCE entry point only, not proof of the libc wrapper's IDs. Other IDs and exact PS4 error and pointer handling remain unresolved. Unknown IDs must not silently return a fabricated time. |
| `gettimeofday(tv, tz)` | `tv` points to a `struct timeval`: seconds since the Epoch and microseconds in `[0, 1,000,000)`. The historical timezone argument is passed as null at the confirmed game call site. The exact target structure layout still needs ABI evidence. | The wall clock can jump and is not a monotonic elapsed-time source. Historical POSIX specified a zero return on success and left errors undefined; Linux documents `-1`/`EFAULT` for inaccessible pointers and ignores the timezone argument. POSIX.1-2024 removed the interface. | The imported wrapper and game call sites are present; the game passes null for the second argument. Exact target behavior for bad pointers, non-null timezone arguments, and error mapping is not established. Do not silently ignore an error without evidence. |
| `sched_yield(void)` | No arguments. Returns `int`. | POSIX: the calling thread gives up the processor until it is again at the head of its thread list; `0` on success, `-1` with `errno` otherwise, and POSIX defines no error case for it. FreeBSD documents the same call. | Imported by `libc.elf` from `libkernel`. Implemented: a guest thread runs on a host thread, so the runtime yields the host thread and returns `0` (`runtime/syslib/kernel_thread.c`). A host failure has no FreeBSD error number to report, so it stops by name. Not observable in a unit test: that the host scheduler actually ran another thread. |
| `scePthreadYield` | Asks for the same yield through the thread library. The PS4-visible signature is not established. | FreeBSD's `pthread_yield(3)` is `void`; Linux's is `int` (always `0`). No PS4-specific public source gives the SCE name's declaration. | Imported by `eboot` and `libc.elf` from `libkernel`; no call site in this repository shows whether the caller reads a result. Registered with `RT_SYSLIB_UNRESOLVED`: the entry stops by name and does not count as implemented. What would settle it: a guest call site showing whether the return register is read after the call, or a PS4-specific public declaration. Do not assume an `int` or `0` from the FreeBSD or Linux analogues. |
| `memcpy(dest, src, n)`, `memset(dest, c, n)` (libSceLibcInternal) | `void *memcpy(void *, const void *, size_t)` and `void *memset(void *, int, size_t)`, the ISO C declarations. | ISO C 7.24.2.1 and 7.24.6.1: `memcpy` copies `n` bytes from `src` to `dest`; `memset` writes `c` converted to `unsigned char` into the first `n` bytes; both return `dest`. Overlapping `memcpy` regions and null pointers are undefined behaviour. | Imported by `libSceFios2.elf` from `libSceLibcInternal` (`symbols/imports.csv`). Implemented with the host's own functions, since the standard fixes the whole result and no target-specific value is involved (`runtime/syslib/kernel.c`). Not modelled: what the PS4 C library does for the undefined cases; no public source or call site establishes it. |
| `sceKernelUsleep(microseconds)` (libkernel), `usleep(microseconds)` (libScePosix) | One unsigned 32-bit count of microseconds, returning `int`. For `sceKernelUsleep` the guest fixes the shape: the verified call sites in `game/frame_timing/frame_limiter.cpp` and `game/render/swap_chain_present.cpp` declare it as `int(uint32_t microseconds)`, and their cases expect `0`. FreeBSD's `useconds_t` is 32-bit unsigned. | FreeBSD `usleep(3)`: suspends the thread for the given microseconds, `0` on success, `-1` with `EINTR` if a signal interrupts it. SUSv2 also let `usleep` fail with `EINVAL` from 1,000,000 on; FreeBSD and POSIX.1-2008 (which removed `usleep`) have no such limit. | `sceKernelUsleep` is imported by `eboot` and `libSceFios2.elf`, `usleep` by `eboot`. Implemented: the host thread sleeps at least the requested time and the call returns `0`; a host signal is not a guest event, so the sleep resumes with the time left (`runtime/syslib/kernel.c`). Open: `usleep` with 1,000,000 or more, which stops by name because no PS4-specific source says whether the guest sleeps or gets `EINVAL`; and the SCE error, if any, for `sceKernelUsleep`, which the guest's sites never check. |
| `scePthreadRwlockInit(rwlock, attr, name)`, `scePthreadRwlockDestroy(rwlock)`, `scePthreadRwlockRdlock(rwlock)`, `scePthreadRwlockWrlock(rwlock)`, `scePthreadRwlockTryrdlock(rwlock)`, `scePthreadRwlockTrywrlock(rwlock)`, `scePthreadRwlockUnlock(rwlock)` (libkernel) | A pointer to the guest's rwlock variable (`Init` also an attribute pointer and a name, as `scePthreadMutexInit`); each returns `int32_t`. No call site in this repository shows these shapes; they come from the public [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h) declarations, supporting evidence for the shapes only, and match FreeBSD's `pthread_rwlock_*`. | POSIX `pthread_rwlock_*`, as FreeBSD libthr (`thr_rwlock.c`) and the kernel's umtx read-write lock (`kern_umtx.c`) implement it: a static initializer (`NULL`) set up on first use and a destroyed rwlock holding `1` (`thr_private.h`); `EINVAL` for a destroyed rwlock; `EBUSY` from the try forms when the lock is not available; destroying does not check whether the lock is held, and leaves an unused initializer in place; unlocking is `EPERM` when another thread holds the write lock or when nobody holds the lock, and readers are counted, not named; a thread that locks again while holding the write lock is not given an error, it waits forever (no owner check in `do_rw_wrlock` or `do_rw_rdlock`). The SCE name returns `0x80020000` plus the error number. | Imported by `eboot` and `libSceFios2.elf` (`Tryrdlock` by `libSceFios2.elf` only; `symbols/imports.csv`). Implemented on a host rwlock that also records the writer and the reader count, which the host does not expose, for the unlock answers (`runtime/syslib/kernel_rwlock.c`). A missing variable answers `EINVAL`, as the mutex calls do; unlocking a never-used initializer is `EPERM`, as for the mutex. Stops by name: `Rdlock` or `Wrlock` while the caller holds the write lock (the system would wait forever, the host answers `EDEADLK`; neither is useful to return), and an attribute object passed to `Init` (no imported call creates one, so its contents are not established; an empty attribute variable means the defaults, as in libthr). Not part of this entry: `scePthreadRwlockTimedrdlock` and `Timedwrlock` (imported by `eboot`; only OpenOrbis gives their microsecond timeout, no call site does) and the `libScePosix` names. |
| `__stack_chk_fail(void)` (libkernel) | No arguments, no result: `void __stack_chk_fail(void)`, and it never returns (LSB; FreeBSD libc declares it `__dead2`). Compiler-generated code calls it, not application code. | LSB Core: the function that called it is aborted with a message that a stack overflow was detected, and the program then exits. FreeBSD libc logs "stack overflow detected; terminated" and ends the process with `SIGABRT`. | Imported by `eboot`, `libSceFios2.elf` and `libc.elf` from `libkernel` (`symbols/imports.csv`). Implemented as a stop by name (`rt_fatal`, which aborts): ending the program is the whole guest-visible contract, so the entry counts as implemented (`runtime/syslib/kernel.c`). Not covered: the PS4's own message or crash report, which the guest cannot observe after it ends; and `__stack_chk_guard`, a separate data import whose PS4 value is not established (an unbound data import gets a zeroed page, `runtime/boot.c`). |
| `scePthreadCondInit(cond, attr, name)`, `scePthreadCondDestroy(cond)`, `scePthreadCondSignal(cond)`, `scePthreadCondBroadcast(cond)`, `scePthreadCondWait(cond, mutex)`, `scePthreadCondTimedwait(cond, mutex, microseconds)` (libkernel) | Pointers to the guest's cond and mutex variables; each returns `int32_t`. The guest fixes the wait shapes: the verified `game/kernel/condition_wait.cpp` declares `Wait` as `int32_t(void *cond, void *mutex)` and `Timedwait` as `int32_t(void *cond, void *mutex, uint32_t timeout_us)`, a relative timeout in microseconds, not POSIX's absolute `timespec`. The public [OpenOrbis `libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h) declares all six with these shapes (`Init` with an attribute pointer and a name, as `scePthreadMutexInit`); it is supporting evidence for the shapes only. | POSIX `pthread_cond_*`, as FreeBSD libthr implements it (`thr_cond.c`): a static initializer (`NULL`) set up on first use; a destroyed cond holds `1` (`thr_private.h`); `EINVAL` for a destroyed cond; `EBUSY` to destroy one with waiters; destroying an unused initializer succeeds and leaves it in place; `EPERM` when the waiter does not own the mutex, for every mutex type, `EINVAL` when the mutex is destroyed (`_mutex_owned`, `thr_mutex.c`); `ETIMEDOUT` when a timed wait expires; a recursive mutex is released completely for the wait and its count restored after (`_mutex_cv_unlock`, `_mutex_cv_lock`). The SCE name returns `0x80020000` plus the error number. | `scePthreadCondWait` and `Timedwait` are imported by `eboot`, `libSceFios2.elf` and `libc.elf`, the other four by `libSceFios2.elf` and `libc.elf` (`symbols/imports.csv`). The guest's call site checks exactly `0x80020001` (EPERM), `0x80020016` (EINVAL) and `0x8002003c` (ETIMEDOUT). Implemented on a host cond on the monotonic clock (`runtime/syslib/kernel_mutex.c`): the timed wait times out once the given microseconds have passed since the call (`0`: at once). A missing cond or mutex variable answers `EINVAL`, as the mutex calls do. Stops by name: an attribute object passed to `Init` (no imported call creates one, so its contents are not established; an empty attribute variable means the defaults, as in libthr). A wait with a recursive mutex releases every level the caller holds and takes them back after, as libthr does: the runtime tracks each recursive mutex's owner and depth, and releases the extra levels itself around the host wait. Not reproduced: the ownership check for normal and adaptive mutexes, which the host does not make; the error-checking default and the recursive type are checked. The `libScePosix` names `pthread_cond_init`, `_destroy`, `_signal`, `_broadcast` and `_wait` (imported by `eboot`) are the same calls with errno values: the executable's call at `0x0247e6d7` passes `(&cond, NULL)` and its caller compares the result with bare error numbers (12, 16, 34, 35). `pthread_cond_timedwait` is not part of this entry: it takes an absolute `timespec` whose PS4 layout and clock are not established, so it stays unbound and stops by name. |

| `sceSysmoduleLoadModule(id)`, `sceSysmoduleIsLoaded(id)`, `sceSysmoduleUnloadModule(id)` | One system module id, a `uint16_t`; each returns `int32_t`, by the public [OpenOrbis `Sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Sysmodule.h) declarations `int32_t f(enum OrbisSysModule moduleId)`, the enum being `uint16_t` ([`_types/sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/sysmodule.h)). | OpenOrbis documents each as "0 = Success/Valid or Negatives values are error codes"; no public source gives the error values. | Implemented for the modules `game/modules.csv` lists with action `stub`: a load marks the module loaded and succeeds without mapping anything, a query of a loaded one answers 0, an unload clears the mark. Everything else stops by name: a module that is not a stub (the runtime cannot load system modules yet), a second load of a loaded stub (whether the system counts loads is not public), and a query or unload of a stub that is not loaded (the "not loaded" error value is not public). See "Module loading at run time" below. |

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

**Unknown**: the error values (a module not loaded, an id the system does not know), and whether a
second load of a loaded module is counted, ignored or refused. Each is a named fatal until a console
measurement or a guest call site settles it.

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

**Still required before implementation:** independently establish the main thread's actual
mask, how a fresh attribute differs from one filled by `scePthreadAttrGet`, the exact output
write extent, success/error results, and invalid/null attribute or output handling. Host CPU
availability and a guessed console core count establish none of these. No specific affinity
implementation is added or registered by this investigation; the import remains unbound.

A useful target measurement would record the console/firmware and executable identities, the
results of attribute initialization and copying from `scePthreadSelf`, the getter's return bits,
the output bytes with adjacent canaries, and repeated observations without changing affinity.
Check current claims in [issue #34](https://github.com/Yharnam-Hunters/Paleblood/issues/34) before
taking implementation work; mentions of the boot blocker are not themselves contributor claims.
Generic gameplay capture remains separate contributor-owned work, not a reason to synthesize
the missing affinity observation.

## Clock IDs and platform differences

The only clock ID confirmed by a decompiled game call in this repository is `4`, used with `sceKernelClockGettime` and identified there as `CLOCK_MONOTONIC`. This does not prove that the game passes `4` to the imported `clock_gettime` wrapper. POSIX defines clock identifiers symbolically; their numeric values are implementation-specific. Linux adds clocks such as `CLOCK_MONOTONIC_RAW` and `CLOCK_BOOTTIME`; FreeBSD has its own clock set, including `CLOCK_UPTIME` variants. Their names and numeric values are not interchangeable evidence for PS4 behavior.

`CLOCK_MONOTONIC` represents elapsed time from an unspecified starting point and is not the wall clock. The precise effects of suspend, rate correction, and resolution vary by implementation. `gettimeofday` and `CLOCK_REALTIME` represent wall time tied to the Epoch and can move when system time changes. No source examined here establishes that the game asks the imported `clock_gettime` wrapper for `CLOCK_REALTIME`, `CLOCK_MONOTONIC_RAW`, `CLOCK_BOOTTIME`, or another ID.

## Network byte-order helpers

The four `libSceNet` imports `sceNetHtonl`, `sceNetHtons`, `sceNetNtohl`, and `sceNetNtohs` take and return 32-bit or 16-bit unsigned integers, respectively. Public PSP2SDK API declarations provide those widths for the SCE names. The standard byte-order contract converts host order to network (big-endian) order for `hton*`, and reverses that conversion for `ntoh*`; this is documented by the [FreeBSD byte-order manual](https://man.freebsd.org/cgi/man.cgi?format=html&query=htons&sektion=3). Paleblood's guest ABI is AMD64, so these four conversions are byte reversals. Their implementations do not call host networking APIs or depend on host socket state.

This evidence supports only the four integer conversions. It says nothing about `libSceNet` socket, resolver, epoll, offline-state, or error behavior; those imports remain unresolved until their own contracts are established.

## IPv4 text conversion (`sceNetInetPton`, issue #41)

The executable imports `sceNetInetPton` from `libSceNet`. Target call-site inspection found four unconditional call instructions (at `0x0085785e`, `0x011c7a7b`, `0x02f984f6`, and `0x02df348d`); each passes address family `2` and supplies the result as an IPv4 address. The consumers use a positive return as success. This establishes Bloodborne's observed call shape, not every address family supported by the PS4 library. The import thunk and pointer-table references are not additional call sites.

For the observed AF_INET path, the runtime uses the documented BSD [`inet_pton(3)` contract](https://man.freebsd.org/cgi/man.cgi?query=inet_pton): parse dotted-decimal IPv4 text into four network-order bytes, return 1 for valid input and 0 for malformed input. This conversion does not open a socket or perform network I/O. Other target address families and null-pointer error behavior remain unresolved, so the runtime emits a named diagnostic for those inputs instead of forwarding host-specific errors.

## `getpagesize`: observed, inferred, unknown

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
settle it: one observed `getpagesize()` or `sysconf(_SC_PAGESIZE)` result on a console.

## Network connection state (`libSceNetCtl`, issue #41)

The executable imports exactly six functions from `libSceNetCtl` and neither `sceNetCtlInit` nor
`sceNetCtlTerm` (`symbols/imports.csv`), so the runtime answers the connection queries itself and the
game takes its offline paths. The public declarations of the same API family on PS Vita
([`psp2/net/netctl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/net/netctl.h)
in vitasdk/vita-headers, and the PSP2SDK header reference) give the shape of the calls: a
state query through an `int *`, an information query by an `int` module id into a structure pointer,
one NAT query through a structure pointer, and a callback check that takes nothing and returns `int`.
Vita is a different operating system, so those declarations are evidence for the shape only and their
numbers are never proof of a PS4 value. No value in this group is taken from another runtime's
behaviour: where no public source reaches, the guest's own call sites decide. They have settled the
state numbers (0 is not up, 3 is connected) and the callback keying (an id handed back, not a state),
and everything they do not reach - error codes, structure layouts, the offline answers - stays a
fatal that names the question and the evidence that would close it.

| Function | Arguments and representation | Publicly documented result and errors | Evidence and open questions for PS4 |
|---|---|---|---|
| `sceNetCtlGetState(int *state)` | One pointer to a 32-bit `int`, by the Vita-era declaration. Writes the state the system is in. | The Vita-era family names the disconnected state first in its state enum, so 0 there. Public sources give no error return for a query that succeeds. | Implemented: an offline runtime reports the disconnected state and succeeds. The guest fixes the numbers for us - one site tests the returned slot against 0 at `0x00dfd260` to decide whether to keep waiting, another bounds it with `cmp $0x3; ja` before a four-entry table at `0x020413f3` (so the states it can see are {0, 1, 2, 3}), and equality with 3 is the connected branch at `0x010b839f` and in its own callback at `0x010b45e5`. Still open: what 1 and 2 mean, the PS4 enumerator names, and the error value for a null state pointer (fatal). |
| `sceNetCtlCheckCallback(void)` | No arguments; returns `int`, by the Vita-era declaration. Runs the callbacks made due by a state change. | No public source describes the return value at all, only the declaration. | Implemented as 0, the one value both candidate contracts give while nothing is due: the runtime never leaves the disconnected state, so no callback runs. Which reading is the PS4's (a success code, or the number of callbacks it ran)? It only matters once a state change is possible. |
| `sceNetCtlRegisterCallback(void *callback, void *arg, int *cid)` | A callback pointer, an argument pointer and an out-parameter `int *cid`, returning `int`. The guest's own call at `0x010b8352` passes a function in the image, `this`, and `lea -0x78(%rbp)`, a slot it never writes, then reads that slot back at `0x010b8428` into its object at `+0x130`. | Vita-era registration is keyed by a callback id the caller receives back (`func, arg, int *cid`); the guest's sites show the same shape on PS4. The two public Vita-era sources disagree about the callback's return type, so the pointer stays opaque and no signature is modelled for it - the guest's callback at `0x010b45e0` does show it being dispatched as (state, arg). | Implemented: each registration gets a fresh id written through the caller's pointer, numbered from 1 so an id can never look like an unwritten slot, and nothing is ever dispatched while offline. Open: the error values for a null callback or a null id slot, the table's capacity, and whether re-registering the same callback replaces or adds (we add, being keyed by id). |
| `sceNetCtlUnregisterCallback(int cid)` | One `int`: the id a previous register call handed back. The guest passes the field it stored from that out-parameter (`0x010b8f8d`). | The Vita-era form takes the callback id `int cid`, which the sites match. | Implemented for an id we handed out: remove it and succeed. Fatal for an id we never handed out, or one already removed - no public source gives the PS4 answer and the guest's own paths never reach it. |
| `sceNetCtlGetInfo(int module, void *info)` | Claimed as an `int` module id and a structure pointer, by the Vita-era `sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info)`. | The Vita-era union's members and its size are another operating system's; nothing public describes the PS4 structure. | Not implemented: fatal before touching the buffer. The guest narrows the question - every site in the executable passes module id 14 (`0x00dfd2b1`, `0x020413ee`), each with a buffer in the second argument that it prints as a string on the success path, and no other id appears anywhere. Open: what id 14 means on PS4, its offline answer, and the structure's size and layout. The fatal names the id, so a boot run reports which ones the guest asks for. |
| `sceNetCtlGetNatInfo(void *nat_info)` | Claimed as one structure pointer, by the Vita-era `sceNetCtlGetNatInfo(SceNetCtlNatInfo *natinfo)`. | The Vita-era structure starts with a `size` field the caller fills in; the PS4 layout is not public. | Not implemented: fatal before writing. The guest's sites bound it - each writes 16 at offset 0 before the call (`0x01889417`) and reads back only offset 8 afterwards (`0x01889436`). Open: the PS4 meaning of the value at offset 8, the NAT type values, and what a system with no connection reports. The reported size is named in the fatal, so a caller reporting something other than 16 is itself evidence that the layout is not what the sites suggest. |

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
  (`0x10` and `0x20` in the public SDK list). The runtime has no GPU yet, so the host mapping
  takes the CPU bits.
- **What stops by name.** Every case whose PS4 answer is not established stops with the function's
  name:
  - lengths, starts or alignments that are not multiples of 16 KiB (the granularity the
    executable's calls use);
  - memory types other than 0, 3 and 10;
  - map flags, and an address hint without flags;
  - protection other than CPU and GPU read and write;
  - a range that does not fit or was not allocated;
  - releasing part of a range or a mapped one;
  - the error values.

Measured with a local boot that let the libc affinity probe return (not committed): with these four
calls the executable's heap setup succeeds and the boot reaches `entry`. It stops next at
`scePthreadRwlockInit`.

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
units are there, and waiters are served in the order they arrived. Signal adds units and wakes the
waiters.

**What stops by name.** Every case whose PS4 answer is not established stops with the function's
name:
- timeouts;
- a missing semaphore, variable or name;
- an option parameter, and attributes other than 1 and 2;
- counts outside `0 <= initial <= max`;
- signalling past the maximum;
- a poll that cannot be satisfied now;
- deleting a semaphore with waiters;
- a second waiter under priority order (guest thread priorities are not modelled);
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

**Stops on other threads.** A worker thread can fault or reach an unimplemented import. The boot
records the first such stop and wakes the main thread (`SIGUSR1`) to make the usual report. Workers
leave the timeout and that signal to the main thread.

A local boot with the affinity probe and the rwlocks let through (not committed) reaches the
`threads` milestone and stops next at `sceUserServiceInitialize`.

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

`sceUserServiceGetUserName` and `sceUserServiceGetEvent` stay unbound until the boot reaches them.
A local boot (affinity probe and rwlocks let through, not committed) stops next at
`sceCommonDialogInitialize`.

## The common dialog's initialize (`libSceCommonDialog`, issue #40)

`sceCommonDialogInitialize` is the only call `eboot` imports from `libSceCommonDialog`. It takes
no arguments (OpenOrbis `CommonDialog.h`, as supporting evidence).

The executable calls it twice:
- at `0x024e1e8c`, logging a failure;
- at `0x02023fbb`, accepting success or exactly `0x80b80002`.

So the first call succeeds and a repeat answers `0x80b80002`, the "already initialized" code. No
dialog is shown: each dialog library's own calls do that, and none is provided yet.

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

## Audio job manager start-up (`sceAjmInitialize`, `sceAjmModuleRegister`, issue #37)

The audio job manager (libSceAjm) decodes compressed audio in batches of jobs. The OpenOrbis
homebrew SDK's `Ajm.h` names these two calls without signatures, so their shapes come from the
executable's only calls:
- **`0x01118a65`:** runs while its context variable still holds -1, and passes 0 and the
  variable's address. A nonzero result is failure.
- **`0x01118a81`:** then passes that context, 1 and 0, and again treats nonzero as failure.

**What the runtime does.** It hands out a context id and records the registered module. Decoding
belongs to the batch and instance calls, which stop by name until they are implemented.

**What stops by name:**
- a nonzero first argument to initialize;
- a module other than the observed 1 (which codec it names is not public);
- a nonzero third argument;
- a context the runtime did not hand out;
- registering a module twice.

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

**What the runtime does.** It records each open port and hands out handles from 1. Sound output
belongs to the output calls, which stop by name until they are implemented.

**What stops by name:** any open other than the observed ones, and running out of handles.

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

**What stops by name:** a missing block, and a handle the runtime did not hand out.

`int32_t sceAudioOutGetPortState(handle, void *state)`, as the SDK declares it, reports a port's
state. Its `_types/audio_out.h` gives the layout (32 bytes): a 16-bit output, an 8-bit channel
count, a volume and a 64-bit flag. The executable's only call (`0x0111bb41`) asks about its second
port, and reads only output, testing it for 0.

**What the runtime does.** It plays nothing on the host, so no port is output anywhere. It answers
output 0, and 0 in the fields whose encoding is not established. This must change when the runtime
plays sound.

**What stops by name:** an unknown handle, and a missing state.

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

## Memory protection queries (`sceKernelQueryMemoryProtection`, issue #34)

`sceKernelQueryMemoryProtection(void *address, void **start, void **end, int32_t *protection)`, as
the OpenOrbis homebrew SDK's `libkernel.h` declares it, gives the protection of the mapping that
holds an address.

**What the executable does.** Its helpers (`0x02abad98`, `0x02abadf8`) pass no start or end. They
test the protection's CPU read bit (1) and GPU read bit (`0x10`), on memory the game mapped itself.

**What the runtime does.** It records every direct and flexible mapping with the protection the
guest asked for, GPU bits included, and answers that.

**What stops by name:**
- a start or end (whether end is inclusive is not established);
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

## Implementation rule

Implement the established contract and test the `RT_SYSLIB` entry through its library and symbol name. Keep target-specific values separate from host values. When an argument, ID, return value, or error case is unknown, fail loudly with the call name and leave the question open; never choose a value to make `tools/boot.py` advance.

## References

- [POSIX `clock_gettime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_gettime.html) and [clock definitions](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/time.h.html)
- [POSIX `getpid`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpid.html)
- [Older POSIX `gettimeofday` specification](https://pubs.opengroup.org/onlinepubs/009604599/functions/gettimeofday.html)
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
- [POSIX `pthread_rwlock_rdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_rdlock.html), [`pthread_rwlock_wrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_trywrlock.html), [`pthread_rwlock_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_unlock.html), [`pthread_rwlock_destroy`/`init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_destroy.html); FreeBSD libthr [`thr_rwlock.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_rwlock.c) and [`thr_private.h`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_private.h) (`THR_RWLOCK_DESTROYED`); FreeBSD kernel [`kern_umtx.c`](https://github.com/freebsd/freebsd-src/blob/main/sys/kern/kern_umtx.c) (`do_rw_rdlock`, `do_rw_wrlock`, `do_rw_unlock`)
- [LSB Core `__stack_chk_fail`](https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/libc---stack-chk-fail-1.html) and [FreeBSD libc `libc_stack_protector.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libc/secure/libc_stack_protector.c) (`__stack_chk_fail`, `__fail`)
- [POSIX `pthread_cond_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_wait.html), [`pthread_cond_signal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_signal.html), [`pthread_cond_destroy`/`pthread_cond_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_destroy.html); FreeBSD libthr [`thr_cond.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_cond.c), [`thr_mutex.c`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_mutex.c) (`_mutex_owned`, `_mutex_cv_unlock`) and [`thr_private.h`](https://github.com/freebsd/freebsd-src/blob/main/lib/libthr/thread/thr_private.h) (`THR_COND_DESTROYED`)
- [vitasdk/vita-headers `psp2/net/netctl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/net/netctl.h) - PS Vita, a different operating system: shape of the `SceNetCtl` declarations, never a PS4 value
- [PSP2SDK `psp2/net/netctl.h` file reference](https://psp2sdk.github.io/netctl_8h.html) and [vitasdk `SceNetCtl` user group](https://docs.vitasdk.org/group__SceNetCtlUser.html) - the same declarations, with no prose describing any return value in this group
- [vitasdk/vita-headers `psp2/error.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/error.h) - `0 on success` as the documented SCE-family return convention
- [OpenOrbis PS4 toolchain `Sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Sysmodule.h) and [`_types/sysmodule.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/sysmodule.h) - the `sceSysmoduleLoadModule`, `sceSysmoduleIsLoaded` and `sceSysmoduleUnloadModule` declarations and return convention, and the system module ids (after [psdevwiki, "Libraries"](https://www.psdevwiki.com/ps4/Libraries))
- [POSIX `pthread_attr_setdetachstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getdetachstate.html) - the detach state semantics, the invalid-value error, and the recommended `EINVAL` for an uninitialised attribute
- [POSIX `pthread_self`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_self.html) and [`pthread_attr_init`/`pthread_attr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_destroy.html)
- [FreeBSD `pthread_attr(3)`](https://man.freebsd.org/cgi/man.cgi?query=pthread_attr_setdetachstate&sektion=3), [libthr `thr_attr.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_attr.c) (`_pthread_attr_setdetachstate`, `_pthread_attr_get_np`), [`thr_create.c`](https://cgit.freebsd.org/src/tree/lib/libthr/thread/thr_create.c) (`_pthread_create`: the default attribute for a null or empty variable) and [`include/pthread.h`](https://cgit.freebsd.org/src/tree/include/pthread.h) (`PTHREAD_CREATE_JOINABLE`, `PTHREAD_CREATE_DETACHED`)
- [OpenOrbis `include/orbis/_types/pthread.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/pthread.h) - the PS4 homebrew SDK's `ORBIS_PTHREAD_CREATE_*` values, which follow FreeBSD's
