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
- [vitasdk/vita-headers `psp2/net/netctl.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/net/netctl.h) - PS Vita, a different operating system: shape of the `SceNetCtl` declarations, never a PS4 value
- [PSP2SDK `psp2/net/netctl.h` file reference](https://psp2sdk.github.io/netctl_8h.html) and [vitasdk `SceNetCtl` user group](https://docs.vitasdk.org/group__SceNetCtlUser.html) - the same declarations, with no prose describing any return value in this group
- [vitasdk/vita-headers `psp2/error.h`](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/error.h) - `0 on success` as the documented SCE-family return convention
