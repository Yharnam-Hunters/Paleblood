<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Kernel

The engine's core library: thin wrappers around the PS4 kernel for threads, mutexes and condition variables, which report every unexpected result through one fatal error function.

**At a glance**

- **Scope:** Dantelion2's kernel layer ("ClassLibrary/Core/Kernel" in its source paths), and the kernel and `libc` calls the executable imports.
- **Verified:** `kernel_condition_wait` (`0x02483e80`), the condition-variable wait the frame-rate patches edit.
- **Open:** Which objects the condition waits made each frame belong to.

## Summary

### What the kernel layer is

Dantelion2 is the engine's core library. Its kernel classes wrap the PS4 kernel's threads,
mutexes and condition variables. Each wrapper checks the result of the call it makes, and any
unexpected result goes to the engine's fatal error function (`0x024b55b0`), which takes a file,
a line and a message.

### The current thread

`kernel_thread_self` (`0x0111a8a0`) stores the calling thread's handle from `scePthreadSelf`
through its pointer argument and returns 0. It is reached only through a function pointer. By
[@evill33t](https://github.com/evill33t) in #105, merged in
[#203](https://github.com/Yharnam-Hunters/Paleblood/pull/203): its 7 constructed edge cases pass
against the original on current `main`, and an independent review approved it.

### Reading files

Four small wrappers handle the engine's file access ([#283](https://github.com/Yharnam-Hunters/Paleblood/pull/283), by [@evill33t](https://github.com/evill33t)):
`kernel_file_open` (`0x0111a620`) opens a file read-only and reports its size (from the system's
file-status record) and its descriptor; `kernel_file_close`, `kernel_file_read_count` and
`kernel_file_seek_absolute` (`0x0111a6b0`, `0x0111a6e0`, `0x0111a720`) close, read and seek. Each
was checked on constructed edge cases against the original and approved by an independent review.

### FMOD's semaphores and recursive mutexes

FMOD (the sound engine; see [Audio](audio.md)) wraps the kernel's semaphores and mutexes in small
functions of its own, and these are the first of them mapped:

- **Semaphores** ([#271](https://github.com/Yharnam-Hunters/Paleblood/pull/271)): create
  (`0x0111ad10`) makes an empty first-in-first-out semaphore with room for 0xffff units and stores
  its handle only if the kernel succeeds; destroy (`0x0111ad80`), wait (`0x0111ada0`, one unit, no
  time limit) and signal (`0x0111adc0`, one unit) pass the handle on.
- **Recursive mutexes** ([#275](https://github.com/Yharnam-Hunters/Paleblood/pull/275)): create
  (`0x0111abb0`) allocates the mutex from FMOD's memory pool (or uses one shared static slot),
  sets it up as recursive and hands it back only after every step succeeded; destroy, lock and
  unlock (`0x0111ac80`, `0x0111acd0`, `0x0111acf0`) pass it on, and lock and unlock do nothing for a
  null mutex.

All of them turn any kernel error into `0x21` (a failed allocation gives `0x2b`). By
[@evill33t](https://github.com/evill33t) in #111 and #108: each was brought up to date with `main`,
its constructed edge cases pass against the original on current `main`, and an independent review
approved it.

### How a condition wait works

`kernel_condition_wait` (`0x02483e80`) waits on a condition variable. It has two paths:

1. **Timeout of -1**: it waits forever (an untimed wait).
2. **Any other timeout**: it makes a timed wait, and returns -3 if the time runs out.

It is called about 40 times a frame in gameplay. Every wait recorded in gameplay is untimed,
which matters for the frame-rate patches (see [Patch points](#patch-points)).

### Imports are not implementations

The executable and its bundled `libc` import POSIX-like calls such as `getpagesize`, `getpid`,
`clock_gettime` and `gettimeofday`. An import only says the game can call something; it does not
mean Paleblood implements it. For implementation status and evidence limits, see the
runtime system-call notes.

Two clock details are established so far:

- The frame limiter passes null as `gettimeofday`'s timezone argument.
- Numeric clock ID `4` is confirmed for `sceKernelClockGettime`, not for the imported
  `clock_gettime` wrapper.

<details>
<summary>getpagesize: why it stops by name</summary>

In the current runtime, both `getpagesize` entries are registered as fail-loud unresolved
diagnostics because the PS4 page-size value is not established, as recorded in merged
Paleblood [PR #66](https://github.com/Yharnam-Hunters/Paleblood/pull/66).

</details>

### Runtime thread-call implementations

These are Paleblood runtime services, not newly decompiled game functions: the runtime answers
some thread and timing calls itself. Two rules run through them:

- A call is implemented only when its guest-visible contract is known. Where the PS4's exact
  behaviour is not established, the call stays a fail-loud diagnostic that stops by name.
- The SCE names and the POSIX names of the same call differ in their errors: the POSIX names
  return the FreeBSD error number, where the SCE names return `0x80020000` plus it.

The current boot stop, `scePthreadAttrGetaffinity`, remains unimplemented until the main thread's
real affinity mask is established. None of these changes has been verified by a new Bloodborne
boot.

<details>
<summary>What each runtime change implements (PRs #62, #72, #81, #94, #95, #97)</summary>

Merged [PR #62](https://github.com/Yharnam-Hunters/Paleblood/pull/62) implements
`scePthreadEqual` by comparing the object pointers used as guest thread handles. Its
synthetic registry tests cover one handle compared with itself, distinct handles in both
argument orders, and null; the public FreeBSD `pthread_equal(3)` contract supports zero for
unequal and nonzero for equal, but the PS4's exact nonzero value is unknown.

Merged [PR #72](https://github.com/Yharnam-Hunters/Paleblood/pull/72) implements imported
`sched_yield` by yielding the host thread and returning zero, following POSIX and FreeBSD's
documented contract. Unit tests check the result but cannot show that another host thread
actually ran.

Later runtime merges extend the same thread and timing group:

- [PR #81](https://github.com/Yharnam-Hunters/Paleblood/pull/81) adds the detach-state
  setters for thread attributes under both the SCE and POSIX names. Reading a detach state
  that the system itself supplies still stops by name, because the PS4 default is not
  established.
- [PR #95](https://github.com/Yharnam-Hunters/Paleblood/pull/95) registers the POSIX names
  `pthread_self`, `pthread_attr_init` and `pthread_attr_destroy`. They return the FreeBSD
  error number where the SCE names return `0x80020000` plus it.
- [PR #94](https://github.com/Yharnam-Hunters/Paleblood/pull/94) sleeps the host thread for
  `sceKernelUsleep`, the call the verified frame limiter and swap-chain present functions
  make, and for `usleep` below one second.
- [PR #97](https://github.com/Yharnam-Hunters/Paleblood/pull/97) ends the program by name on
  `__stack_chk_fail`.

These are runtime services checked by synthetic registry tests, not decompiled game
functions.

</details>

<details>
<summary>scePthreadYield answers zero</summary>

The similarly named `scePthreadYield` yields the same way and answers zero (Paleblood #308).
Its PS4 signature is unpublished: FreeBSD's `pthread_yield` returns `void`, Linux's analogue
returns `int`. Zero is right under either, and all 26 direct call sites in the executable
ignore the result.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a620` | 129 | `kernel_file_open` | edge-verified | Open read-only and query FreeBSD amd64 stat; publish low32 file size before int64 descriptor; compiler stack guard omitted |
| `0x0111a6b0` | 23 | `kernel_file_close` | edge-verified | Forward the full opaque descriptor; translate nonzero low32 close status to 19; synthetic wire edge cases only |
| `0x0111a6e0` | 60 | `kernel_file_read_count` | edge-verified | Publish uint32 transferred bytes after kernel buffer writes; full64 received count controls errors and short reads |
| `0x0111a720` | 22 | `kernel_file_seek_absolute` | edge-verified | Forward the full opaque descriptor and zero-extended unsigned32 absolute offset; full64 seek result sign maps to 0 or 19; synthetic wire edge cases only |
| `0x0111a8a0` | 26 | `kernel_thread_self` | edge-verified | Copy the complete opaque current-thread handle to the output and return zero |
| `0x0111abb0` | 207 | `kernel_mutex_create` | edge-verified | Create a recursive mutex in static or allocated storage; low-byte ownership flag; publish storage only after all kernel stages succeed; no error cleanup |
| `0x0111ac80` | 78 | `kernel_mutex_destroy` | edge-verified | Destroy the mutex and release storage only after kernel success with a zero low-byte static flag; errors map to 0x21; ignore release result |
| `0x0111acd0` | 30 | `kernel_mutex_lock` | edge-verified | Accept a null mutex without a call; preserve the full kernel pointer and API writes; map any nonzero low32 result to 0x21 |
| `0x0111acf0` | 30 | `kernel_mutex_unlock` | edge-verified | Accept a null mutex without a call; forward its full pointer to unlock; preserve API writes and translate nonzero low32 results to 0x21 |
| `0x0111ad10` | 103 | `kernel_semaphore_create` | edge-verified | Create the fixed empty FIFO semaphore; preserve all six API arguments and full opaque handle; publish only after low32 kernel success; errors map to 0x21 |
| `0x0111ad80` | 23 | `kernel_semaphore_destroy` | edge-verified | Forward the full opaque handle including null to kernel deletion; preserve kernel writes and translate any nonzero low32 result to 0x21 |
| `0x0111ada0` | 30 | `kernel_semaphore_wait` | edge-verified | Wait for one unit using the full opaque handle including null and a null timeout pointer; preserve kernel writes; nonzero low32 results map to 0x21 |
| `0x0111adc0` | 28 | `kernel_semaphore_signal` | edge-verified | Signal one unit using the full opaque handle including null; preserve kernel writes and translate every nonzero low32 result to 0x21 |
| `0x02483e80` | 204 | `kernel_condition_wait` | verified | Dantelion2 PthreadCondition wait (untimed when timeout is -1); the FPS patches put a ret on the timed path |

## Structs and globals

Not documented yet.

## Patch points

- `30 FPS++`, `60 FPS++`, `90 FPS++` and `Uncap FPS++` write a return over the timed-wait call
  in `0x02483e80`. If that path ever ran, the return would jump to a garbage address. Every wait
  recorded in gameplay is untimed, so it never does. Our replacement keeps the original
  behaviour.

## Open questions

- Which objects the ~40 condition waits per frame belong to (job queues, the render thread?).
