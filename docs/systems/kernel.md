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
<summary>scePthreadYield is not implemented</summary>

The similarly named `scePthreadYield` remains registered as an unresolved, fail-loud
diagnostic. FreeBSD's `pthread_yield` returns `void`, Linux's analogue returns `int`, and no
PS4 declaration or inspected guest call site settles the guest-visible signature. Do not
infer that it is implemented from its registry entry.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
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
