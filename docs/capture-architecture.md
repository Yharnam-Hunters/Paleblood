# Capture architecture: return-aware observation

## Current hook boundary

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

## Observation approaches

| Approach | ABI and return behavior | Stack/register behavior | Pointer memory and imports | Concurrency and failure | Decision |
| --- | --- | --- | --- | --- | --- |
| Typed wrapper dispatch | A wrapper declared with the exact function-pointer type lets the compiler perform the platform ABI call and return the exact result. Unsupported types can be rejected before calling. | Ordinary typed calls handle register/stack assignment; this is only established for each declared signature and build ABI. No universal wrapper is implied. | Explicit descriptors can snapshot bounded input/in-out memory. Nested pointers, globals, and imports still need descriptors or dedicated observers. | Use per-invocation state; reject missing/invalid descriptors before invoking the target. A failed write must not fabricate a completed case. | Smallest candidate for a later production design, but production dispatch needs a way to select and install typed wrappers. The current host interface does not provide one. |
| Assembly trampoline | Could preserve and forward unknown calls, but must handle each supported return class exactly (integer, vector/float, and any ABI-specific aggregate class). | Must preserve argument registers, stack arguments, alignment, red zone, callee-saved state, return address, unwind behavior, and nested/reentrant calls. Signal/exception paths and per-thread nesting need explicit treatment. | Still cannot infer pointee extents or import side effects. Descriptors remain necessary. | Needs per-thread nesting state, bounded storage, recursion tests, and defined behavior on allocation/I/O failure. A bug can corrupt arbitrary calls. | Not the first implementation: too broad and risky before a typed contract exists. |
| Isolated typed test harness | Calls only explicitly declared synthetic signatures; compiler handles that signature. No production hook or runtime ABI change. | Tests a declared call and replay behavior, but makes no claim about arbitrary signatures or a production trampoline. | Synthetic descriptors state exact pointer roles and sizes. Existing verifier scripts imports and compares memory effects. | Deterministic local fixtures, fail-closed descriptor checks, and isolated test directories. | Chosen for this milestone: smallest safe proof that validates the metadata and case/replay contract without game data. |

## Synthetic proof scope

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

## Fail-closed conditions

The proof rejects an unsupported function signature, absent descriptor, wrong signature tag,
incorrect argument/register mapping, missing pointer range, zero/oversized buffer extent, or invalid
function/run identifier before calling the function. These checks are deliberately in test-only
code; production hooks remain unchanged and capture remains opt-in/off by default.
