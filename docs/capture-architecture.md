# Capture architecture

## Recording by tracing the original

Replacements carry no recording code (STYLE.md), and the hook registry knows only an address, a
size and a name, so no typed descriptor can say what a function reads. The runtime therefore
learns a function's inputs by watching the original run: a sampled call of a hooked function runs
the original's own code one instruction at a time, and every byte the code reads before writing it
is an input. That is the definition `tools/verify.py` replays against: the inputs the original
actually consumed, in the memory layout it consumed them.

**Tracer** (`runtime/trace.c`, `runtime/trace_entry.S`). `rt_trace_call` runs guest code on the
calling thread with the trap flag set, on a private stack holding a copy of the caller's stack
arguments. The kernel delivers SIGTRAP after each instruction; the handler decodes the next
instruction with Capstone before it runs, computes each memory operand's effective address from
the saved registers, and notes the bytes: the first value of a byte read before it is written is an
input, and every written byte is noted so the case gives it somewhere to land. String instructions
with `rep` cover `rcx` elements in the direction flag's direction. The function's own frame is left
out; reads of its stack arguments are counted. The handler allocates nothing (tables are mapped
before the call) and reads values with `process_vm_readv` on its own process, so an address that
would fault is reported instead of crashing.

The call always runs to the end and returns its real result to the game. When anything cannot be
expressed in a case, tracing stops and the rest of the call runs natively; the log names the reason
and no case is written. Fail-closed reasons today: an instruction the tracer does not model
(`xsave`, `fxsave`, `xlat`, `syscall`, `int3`, ...), a segment-relative access (thread-local
storage), a vector-indexed access (gather, scatter), a read of `xmm2`..`xmm7` before the code writes
it (cases give only `xmm0` and `xmm1`), a stack argument past the 70th, code outside the traced
ranges (an import or other native code), unreadable memory, the instruction limit
(`BB_TRACE_STEP_LIMIT`, default 200,000) or the byte-table limit.

**Case** (`runtime/capture_trace.cpp`). A complete trace becomes a schema-1 case:

- Bytes inside the executable's image are `memory` entries at their PS4 addresses.
- Every other touched byte belongs to a buffer: bytes closer than 256 apart join one buffer, which
  starts on a 16-byte boundary so aligned vector loads stay aligned. Read bytes hold the value
  read; written-only bytes are zero.
- An eight-byte input whose value points into a buffer or into the image becomes a `pointer` or
  `guest` entry, so it still points at the same thing in the verifier's sandbox.
- Arguments (`rdi`..`r9`, `xmm0`, `xmm1`) and the stack arguments read are written the same way:
  `buf:NAME+off`, `guest:0xADDRESS`, or a number.
- `returns` comes from the hook table: the return class is data (`game/hooks.csv`), not inferred.
- The `capture` provenance names the function and the run; files are written by `rt_capture_write`
  (owner-only, never overwriting), under the same sampling as before (`runtime/capture.h`).

Capstone is optional: without it the runtime builds `runtime/trace_none.c`, tracing is unavailable,
and nothing is recorded.

**Proof.** `test/test_capture_trace.cpp` traces synthetic guest code (`test/capture_trace_guest.S`)
mapped as an image: a pointer argument to a struct holding a pointer, an image global, stack
arguments, a float argument and result, a call between guest functions, and `rep movsb`. It checks
the results and the inputs, and that an `xmm2` read, a thread-local access, a call into native code
and the instruction limit give no case while the call still completes.
`test/capture_trace_integration.py` then replays every recorded case with `tools/verify.py`: each
passes against a native replacement, and a replacement with one deliberate change per function
fails.

**Hooked calls** (`runtime/capture_hooks.c`, `runtime/capture_entry.S`). With capture on
(`BB_CAPTURE_DIR`) and tracing asked for (`BB_CAPTURE_TRACE=1`), `bbgame_init` prepares each hook
before installing it, while the original is still intact:

1. The original's first instructions (at least the 14 bytes the hook's jump overwrites) are copied
   into a block mapped within reach of the image, with RIP-relative operands and 32-bit relative
   calls and jumps rewritten to keep their targets, followed by a jump back into the rest of the
   original (`rt_trace_relocate`).
2. A function whose own branches jump back into those first bytes (a loop at its very start) is not
   recorded: its original can no longer run once hooked (`rt_trace_branches_into`). Nor is one that
   short branches or out-of-reach operands keep from relocating, or one without a return class.
3. The hook's target becomes a small thunk that loads the hook's record and enters
   `rt_capture_entry`, which saves every argument register (and `al`, for variadic calls) and the
   caller's stack pointer, then asks `rt_capture_dispatch` what to do.
4. A redirect maps the original's entry to its relocated copy: traced code that calls another
   hooked function follows that function's original, as the verifier runs it.

At run time an unsampled call, or any call while the thread is already tracing, goes on to the
replacement with every register exactly as it arrived. A sampled call (`runtime/capture.h`: the
first 50 calls, then every 1000th, at most 200, filtered by `BB_CAPTURE_ONLY`) runs the original
under the tracer from its relocated entry, with the caller's arguments and up to 64 stack
arguments copied, and returns the original's result to the caller; the case is written when the
trace is complete. Reads of the relocated code itself (the jump back reads its own target) are not
inputs. A function whose traces give no case three times in a row is not traced again in the run.
Tracing costs microseconds per instruction (default limit 200,000 per call), so it is opt-in and
best paired with `BB_CAPTURE_ONLY`.

`game/hooks.csv` carries each function's return class (`returns`), checked against the
replacement's declaration by `tools/validate_functions.py`.

**Proof.** `test/test_capture_hooks.cpp` hooks six synthetic functions through `bbgame_init` and the
version-1 host installer and calls them through the image, as the game does: sampled calls run the
original traced and return its result, unwanted calls reach the replacement, a traced original's
call to another hooked function follows that function's original, a function with a loop into its
first bytes is never recorded, and one whose trace always leaves for native code stops being traced
after three tries. The recorded cases replay through `tools/verify.py` (`capture_hooks_replay`).

Limits: a traced call runs on a private stack, so a function that keeps the address of one of its
stack arguments after it returns, unwinds past its caller (`longjmp`), or switches stacks or fibers
mid-call is not safe to trace; none is known among the hooked functions.

**Imports** (`runtime/trace.c`, `runtime/capture_hooks.c`). When traced code reaches native code
at the address an import's slot holds (through `call [slot]`, a `jmp [slot]` thunk, or a register
loaded from the slot), the call runs natively and is recorded instead of ending the trace: the
tracer snapshots up to 4096 bytes behind each argument register, sends the import's return to a
one-byte `int3` pad, and lets it run untraced. At the pad it records the return value and every byte
that changed behind each pointer argument, notes those bytes as written (later reads of them are not
inputs: the case's script writes them), puts the real return address back and resumes tracing. The
case scripts each call in order: `{"name", "argc": 0, "ret", "writes": [{"arg", "offset", "bytes"}]}`.
Reads of an import's slot are never inputs, since the verifier binds its own stub there.

The host's loader binds the imports, so the runtime finds the slots itself at install time: it maps
the executable (`BB_ELF`) again into scratch memory with each function import bound to a numbered
marker, finds the slots holding markers, and names each from the symbol list (`BB_CAPTURE_IMPORTS`,
`symbols/imports.csv`; `tools/run_game.sh` points it there) by matching each name's NID. A slot whose
NID has no name in the list is not scripted, and a call through it ends the trace.

Limits: arguments are not compared (`argc` 0), since their count is not known; a return value is
scripted from `rax`, so an import returning a float is not reproduced; memory an import changes
other than behind its pointer arguments (a global, `errno`) is not scripted, so such a case fails
verification rather than passing wrongly; an import that calls back into guest code runs those
callbacks untraced (replay runs no callback either); one that unwinds past its caller ends the trace.

**Proof.** `test/test_capture_trace.cpp` traces a guest function that calls an import through a thunk
and its slot: the import runs, its result and the bytes it wrote are recorded, what it wrote is not
an input, its slot is not an input, and tracing resumes. `test/test_capture_hooks.cpp` hooks the same
function: with the executable and the symbol list the slot is found and named and the call gives a
case; without them it gives none. Both replay through `tools/verify.py` with the verifier's own
import stub bound by NID (`capture_trace_replay`, `capture_hooks_replay`).

### Next steps

1. **Thread-local storage**: a `gs:` access becomes a buffer for the thread block and the case's
   `gs` value.
2. **Import arguments**: compare an import's arguments once their count is data (a declaration
   list per import), as hooks carry their return class.

## Typed descriptors (earlier proof)

### Hook boundary

The version-1 host interface in `runtime/include/runtime/host.h` exposes an image pointer,
image size, and `install_hook(offset, size, target)`. `runtime/plugin.c::bbgame_init` passes each
replacement function pointer directly as `target`. The replacement entry then owns the call; the
host interface exposes no callback after it returns and no original-call trampoline.

`rt_hook_entry` contains an address, code size, name, and `void(void)` function pointer. The
registry does not carry the actual function signature, argument register classes, stack argument
count, return class, pointer direction or extent, or the memory reachable through nested pointers.
The Recorder therefore cannot infer a safe schema-1 case from a registry row. A raw address or
register dump would not describe replayable memory and must not be labelled a verification case.

Schema-1 verification cases also need import/stub effects, image globals, and the initial bytes of
every described buffer. Their replay observes return registers, preserved registers, writes, and
scripted calls. Those inputs are function-specific facts, not recoverable from the hook address.

### Observation approaches

| Approach | ABI and return behavior | Stack/register behavior | Pointer memory and imports | Concurrency and failure | Decision |
| --- | --- | --- | --- | --- | --- |
| Typed wrapper dispatch | A wrapper declared with the exact function-pointer type lets the compiler perform the platform ABI call and return the exact result. Unsupported types can be rejected before calling. | Ordinary typed calls handle register/stack assignment; this is only established for each declared signature and build ABI. No universal wrapper is implied. | Explicit descriptors can snapshot bounded input/in-out memory. Nested pointers, globals, and imports still need descriptors or dedicated observers. | Use per-invocation state; reject missing/invalid descriptors before invoking the target. A failed write must not fabricate a completed case. | Smallest candidate for a later production design, but production dispatch needs a way to select and install typed wrappers. The current host interface does not provide one. |
| Assembly trampoline | Could preserve and forward unknown calls, but must handle each supported return class exactly (integer, vector/float, and any ABI-specific aggregate class). | Must preserve argument registers, stack arguments, alignment, red zone, callee-saved state, return address, unwind behavior, and nested/reentrant calls. Signal/exception paths and per-thread nesting need explicit treatment. | Still cannot infer pointee extents or import side effects. Descriptors remain necessary. | Needs per-thread nesting state, bounded storage, recursion tests, and defined behavior on allocation/I/O failure. A bug can corrupt arbitrary calls. | Not the first implementation: too broad and risky before a typed contract exists. |
| Isolated typed test harness | Calls only explicitly declared synthetic signatures; compiler handles that signature. No production hook or runtime ABI change. | Tests a declared call and replay behavior, but makes no claim about arbitrary signatures or a production trampoline. | Synthetic descriptors state exact pointer roles and sizes. Existing verifier scripts imports and compares memory effects. | Deterministic local fixtures, fail-closed descriptor checks, and isolated test directories. | Chosen for this milestone: smallest safe proof that validates the metadata and case/replay contract without game data. |

### Synthetic proof scope

`test/test_capture_poc.cpp` declares one signature explicitly:

```cpp
using Fixture = int32_t (*)(int32_t *out, const int32_t *input, int32_t factor);
```

Its descriptor binds `out` to `rdi` as a four-byte in/out buffer, `input` to `rsi` as a four-byte
input buffer, and `factor` to `rdx` as a signed scalar; the return is `i32`. The adapter snapshots
only those declared bytes, calls through the typed function pointer, and serializes a schema-1
case. The synthetic guest and native replacement are then replayed by `tools/harness.py` and
compared by `tools/verify.py`.

This validates typed-call metadata, deterministic serialization, and compatibility with the
existing replay verifier. It does not instrument `bbgame_init`, install production wrappers, infer
arbitrary signatures, capture real game calls, or establish correctness of a machine-code
trampoline. The fixture covers three integer/pointer arguments in `rdi`, `rsi`, and `rdx`, an `i32`
return, and two distinct four-byte buffers. It does not test stack arguments, floating-point or
aggregate argument/return classes, imports, globals, nested pointers, concurrent calls, or output
file failures. Adding any such production path requires a separately reviewed design and ABI tests.

### Fail-closed conditions

The proof rejects an unsupported function signature, absent descriptor, wrong signature tag,
incorrect argument/register mapping, missing pointer range, zero/oversized buffer extent, or invalid
function/run identifier before calling the function. These checks are deliberately in test-only
code; production hooks remain unchanged and capture remains opt-in/off by default.
