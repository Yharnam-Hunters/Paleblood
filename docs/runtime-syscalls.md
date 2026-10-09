# Runtime syscall behavior notes

This page records the contract used to implement the small POSIX-like calls in issue [#34](https://github.com/Yharnam-Hunters/Paleblood/issues/34). Public standards and manuals describe portable or platform-specific API behavior; they do not by themselves prove every detail of the PS4 implementation. Keep the distinction below, and leave unresolved behavior unresolved until public documentation, our own reverse engineering, or observed behavior settles it.

## Evidence in this repository

`symbols/imports.csv` records `getpagesize` and `gettimeofday` as imports of `eboot` from `libScePosix`; `libc.elf` imports `clock_gettime`, `getpagesize`, `getpid`, and `gettimeofday` from `libkernel`. Import rows establish that the names are required, not their arguments, return values at every call site, or all clock IDs.

The decompiled frame limiter calls `gettimeofday` with a null timezone argument. A separate [verified clock helper](../game/frame_timing/monotonic_clock.cpp) calls `sceKernelClockGettime` with clock ID `4`, named `CLOCK_MONOTONIC` in that code. This confirms one monotonic clock request in the game; it does not establish every ID passed through the imported `clock_gettime` wrapper, or that no other ID is used in unexamined code.

## Interfaces and known behavior

| Function | Arguments and representation | Publicly documented result and errors | Evidence and open questions for PS4 |
|---|---|---|---|
| `getpagesize(void)` | No arguments. Returns the target's page size in bytes. The numeric value is platform-specific. | `getpagesize` is not in current POSIX; portable code is directed to `sysconf(_SC_PAGESIZE)`. Linux and FreeBSD document the byte count; their manuals do not define a general error return for this function. Linux exposes it as a libc interface, not necessarily a dedicated kernel syscall. | The import is present. The target page-size value and behavior under unusual target configurations are not established here. Do not hard-code the build host's page size. |
| `getpid(void)` | No arguments. Returns a `pid_t` naming the calling process. | POSIX specifies that it succeeds and has no error return. Linux process IDs are scoped by PID namespaces; other systems have their own process-ID scope and lifecycle rules. | The import is present, but this repository does not establish the guest-visible process ID or its scope. Returning the host PID is not justified by the API contract alone. |
| `clock_gettime(clock_id, tp)` | `clock_id` selects a clock; `tp` points to a `struct timespec`. On the POSIX interface, `tv_sec` is seconds and `tv_nsec` is nanoseconds in `[0, 1,000,000,000)`. The exact target structure layout still needs ABI evidence. | POSIX uses `0` for success and `-1` with `errno` for failure. An unsupported clock ID is an `EINVAL` case. Linux and BSD manuals document additional pointer-error details; use the target contract rather than assuming the host's errno mapping. | The imported wrapper is present. The verified `sceKernelClockGettime` call requests ID `4` (`CLOCK_MONOTONIC`). That is evidence for the SCE entry point only, not proof of the libc wrapper's IDs. Other IDs and exact PS4 error and pointer handling remain unresolved. Unknown IDs must not silently return a fabricated time. |
| `gettimeofday(tv, tz)` | `tv` points to a `struct timeval`: seconds since the Epoch and microseconds in `[0, 1,000,000)`. The historical timezone argument is passed as null at the confirmed game call site. The exact target structure layout still needs ABI evidence. | The wall clock can jump and is not a monotonic elapsed-time source. Historical POSIX specified a zero return on success and left errors undefined; Linux documents `-1`/`EFAULT` for inaccessible pointers and ignores the timezone argument. POSIX.1-2024 removed the interface. | The imported wrapper and game call sites are present; the game passes null for the second argument. Exact target behavior for bad pointers, non-null timezone arguments, and error mapping is not established. Do not silently ignore an error without evidence. |

## Clock IDs and platform differences

The only clock ID confirmed by a decompiled game call in this repository is `4`, used with `sceKernelClockGettime` and identified there as `CLOCK_MONOTONIC`. This does not prove that the game passes `4` to the imported `clock_gettime` wrapper. POSIX defines clock identifiers symbolically; their numeric values are implementation-specific. Linux adds clocks such as `CLOCK_MONOTONIC_RAW` and `CLOCK_BOOTTIME`; FreeBSD has its own clock set, including `CLOCK_UPTIME` variants. Their names and numeric values are not interchangeable evidence for PS4 behavior.

`CLOCK_MONOTONIC` represents elapsed time from an unspecified starting point and is not the wall clock. The precise effects of suspend, rate correction, and resolution vary by implementation. `gettimeofday` and `CLOCK_REALTIME` represent wall time tied to the Epoch and can move when system time changes. No source examined here establishes that the game asks the imported `clock_gettime` wrapper for `CLOCK_REALTIME`, `CLOCK_MONOTONIC_RAW`, `CLOCK_BOOTTIME`, or another ID.

## Implementation rule

Implement the established contract and test the `RT_SYSLIB` entry through its library and symbol name. Keep target-specific values separate from host values. When an argument, ID, return value, or error case is unknown, fail loudly with the call name and leave the question open; never choose a value to make `tools/boot.py` advance.

## References

- [POSIX `clock_gettime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_gettime.html) and [clock definitions](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/time.h.html)
- [POSIX `getpid`](https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpid.html)
- [Older POSIX `gettimeofday` specification](https://pubs.opengroup.org/onlinepubs/009604599/functions/gettimeofday.html)
- [FreeBSD `getpagesize(3)`](https://man.freebsd.org/cgi/man.cgi?query=getpagesize&sektion=3&manpath=FreeBSD+13.3-RELEASE)
- [FreeBSD `clock_gettime(2)`](https://man.freebsd.org/cgi/man.cgi?query=clock_gettime&sektion=2&manpath=FreeBSD+15.1-STABLE) and [`gettimeofday(2)`](https://man.freebsd.org/cgi/man.cgi?query=gettimeofday&sektion=2&manpath=FreeBSD+14.4-RELEASE)
- [Linux `getpagesize(2)`](https://man7.org/linux/man-pages/man2/getpagesize.2.html), [`getpid(2)`](https://man7.org/linux/man-pages/man2/getpid.2.html), [`clock_gettime(2)`](https://man7.org/linux/man-pages/man2/clock_gettime.2.html), and [`gettimeofday(2)`](https://man7.org/linux/man-pages/man2/gettimeofday.2.html)
