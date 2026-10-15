<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Kernel

## Summary

The engine's core library (Dantelion2, "ClassLibrary/Core/Kernel" in its source paths): thin
wrappers around the PS4 kernel for threads, mutexes and condition variables, which report every
unexpected result through the engine's fatal error function (`0x024b55b0`, file, line, message).

The executable and bundled `libc` import POSIX-like calls such as `getpagesize`, `getpid`,
`clock_gettime`, and `gettimeofday`; an import does not mean Paleblood implements it. In the
current runtime, both `getpagesize` entries are registered as fail-loud unresolved diagnostics
because the PS4 page-size value is not established, as recorded in merged Paleblood [PR #66](https://github.com/Yharnam-Hunters/Paleblood/pull/66).
See the runtime system-call notes for implementation status and evidence limits. In the game
evidence reviewed so far, the frame limiter passes null as `gettimeofday`'s timezone argument;
numeric clock ID `4` is confirmed for `sceKernelClockGettime`, not for the imported
`clock_gettime` wrapper.

`kernel_condition_wait` (`0x02483e80`) waits on a condition variable: forever when the timeout
is -1, else with a timed wait that returns -3 on timeout. It is called about 40 times a frame in
gameplay. The community frame-rate patches replace its timed-wait call with a return that would
jump to a garbage address if it ran; every wait recorded in gameplay is untimed, so it never
does.

### Runtime thread-call implementations

These are Paleblood runtime services, not newly decompiled game functions. Merged [PR #62](https://github.com/Yharnam-Hunters/Paleblood/pull/62)
implements `scePthreadEqual` by comparing the object pointers used as guest thread handles. Its
synthetic registry tests cover one handle compared with itself, distinct handles in both argument
orders, and null; the public FreeBSD `pthread_equal(3)` contract supports zero for unequal and
nonzero for equal, but the PS4's exact nonzero value is unknown. Merged [PR #72](https://github.com/Yharnam-Hunters/Paleblood/pull/72)
implements imported `sched_yield` by yielding the host thread and returning zero, following POSIX
and FreeBSD's documented contract. Unit tests check the result but cannot show that another host
thread actually ran.

The similarly named `scePthreadYield` remains registered as an unresolved, fail-loud diagnostic.
FreeBSD's `pthread_yield` returns `void`, Linux's analogue returns `int`, and no PS4 declaration or
inspected guest call site settles the guest-visible signature. Do not infer that it is implemented
from its registry entry. None of these changes has been verified by a new Bloodborne boot.

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x02483e80` | 204 | `kernel_condition_wait` | verified | Dantelion2 PthreadCondition wait (untimed when timeout is -1); the FPS patches put a ret on the timed path |
## Structs and globals

Not documented yet.

## Patch points

- `30 FPS++`, `60 FPS++`, `90 FPS++` and `Uncap FPS++` write a return over the timed-wait call
  in `0x02483e80` (see above); our replacement keeps the original behaviour.

## Open questions

- Which objects the ~40 condition waits per frame belong to (job queues, the render thread?).
