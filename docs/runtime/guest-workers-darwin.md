# Guest workers on Darwin

The current thread provider runs the same guest create/join/detach, explicit exit,
once, priority and key-destructor paths on Darwin x86-64. It uses Mach-O symbol
names for the stack switch, the existing non-replacing helper for the low 40-bit
thread-handle range, and portable anonymous stack flags. Linux retains its GS-base
binding and scheduling behavior. This change does not modify CPU affinity policy.

Darwin reserves GS for its host thread data. `guest_pointer_owned` therefore owns
one public pthread key for the process lifetime, using the existing `guest_tls_owned`
API. The numeric key gives only a bounded candidate displacement. Before guest entry
on **each host thread**, binding compares direct GS reads with public TSD for two
distinct live witnesses, NULL and that thread's real variant-II TCB. Mismatch stops
by name; an invalid direct read needs the caller's execution fault-recovery scope
or terminates the process. Qualification is specific to the running host profile,
not a public Darwin layout guarantee.

The loader may request the displacement only after the calling thread has bound
successfully. Every worker must bind independently before running rewritten code.
No GS base or host TCB is overwritten. Guest key destructors still run with the
worker's TCB bound, including destructor rounds after an explicit guest exit. The
worker clears its public/direct slot before returning to native pthread teardown.
The runtime's existing TCB/stack ownership is retained; this library does not free
caller storage. The process key is never deleted while workers or rewritten code
could still refer to it.

Guest names remain recorded as before. Darwin can set only the calling host thread's
debugger label, so worker startup and self-renames apply that label locally; renaming
another guest thread still records its name without changing the calling host label.

## Tests

Use a whole-process x86-64 configuration on Intel macOS or under Rosetta. The Metal
library may still target either host architecture independently; ARM compilation
alone does not execute AMD64 guest instructions.

```sh
cmake -S test/host_threads -B build-guest-threads -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-guest-threads --parallel 2
ctest --test-dir build-guest-threads --output-on-failure --no-tests=error
```

Linux uses the same isolated project without the architecture option. Root Linux
CTest also registers the thread tests alongside the existing full `syslib` suite.

The production provider executes the existing thread lifecycle suite, simultaneous
TCBs with initialized/zero-filled template bytes, host and guest key isolation,
priority inheritance, and destructor rounds. An owned executable ELF fixture goes
through the real loader's FS-to-GS rewrite, then performs TLS loads/stores on real
runtime workers. Controlled public API failures cover rejected qualification and
failed clear ownership. A separate observer around the production worker's clear
call checks that guest destructors precede clear and that clear precedes native
teardown. This avoids relying on pthread cleanup automatically clearing the slot.
Native Mach-O thread-local storage can already be torn down by the time a public
pthread-key destructor runs; host TLV canaries are checked during entry and guest
key destructors, while they remain in the guest execution lifetime.

This is worker support, not a complete boot port. Darwin startup still needs to bind
the main thread inside a recovery scope and pass its qualified displacement to the
loader. Executable TLS support here does not establish arbitrary FS/GS instruction
patterns, per-worker dynamic module TLS, cancellation, original-game behavior or
successful macOS guest startup. No game bytes are used by these tests.
