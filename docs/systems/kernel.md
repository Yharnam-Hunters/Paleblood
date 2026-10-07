<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Kernel

## Summary

The engine's core library (Dantelion2, "ClassLibrary/Core/Kernel" in its source paths): thin
wrappers around the PS4 kernel for threads, mutexes and condition variables, which report every
unexpected result through the engine's fatal error function (`0x024b55b0`, file, line, message).

`kernel_condition_wait` (`0x02483e80`) waits on a condition variable: forever when the timeout
is -1, else with a timed wait that returns -3 on timeout. It is called about 40 times a frame in
gameplay. The community frame-rate patches replace its timed-wait call with a return that would
jump to a garbage address if it ran; every wait recorded in gameplay is untimed, so it never
does.

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
