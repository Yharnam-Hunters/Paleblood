# Engine utilities

These shared helpers expose small engine contracts used by other systems. Replacement and
verification status is tracked in [the function registry](../../symbols/functions.csv).

| Address | Role |
|---|---|
| `0x011c4970` | Compare zero-terminated strings using signed byte values |
| `0x011c4990` | Compare a bounded prefix using signed byte values |
| `0x01314320` | Resolve an enabled entry manager's handle through its low-word index |
| `0x0217ffb0` | Initialize one guarded static empty wide string |
| `0x02a44140` | Release a wide string and reset it to empty inline storage |

## Static wide string

The initializer uses the existing [WideString layout](../../game/engine/wide_string.h), with
eight UTF-16 code units of inline storage and capacity seven for an empty inline string.
Its object is at `0x05aceb90`; the separate initialization guard follows it at `0x05acebc8`.
The object's leading eight bytes and unused inline/padding bytes are preserved.

| Field | Offset | Initializer write |
|---|---:|---|
| Allocator | `0x28` | Captured full pointer |
| Capacity | `0x20` | Eight-byte value 7 |
| Length | `0x18` | Eight-byte zero |
| First inline code unit | `0x08` | Two-byte zero terminator |
| Ownership byte | `0x30` | One-byte value 1 |

These writes occur before destructor registration. Clearing the complete inline buffer or
widening the ownership-byte store would change bytes the original leaves alone.

The entry test reads only the guard's low byte. Any nonzero byte returns immediately, including
values other than one. A zero low byte enters initialization even when the guard's upper bytes
are nonzero. After destructor registration returns, the initializer writes the complete
eight-byte value one, clearing those upper bytes. This is the observed guard protocol; it adds
no locking or atomic initialization behavior.

The default allocator comes from `0x05940670`. If absent, the initializer calls the original
no-argument acquisition thunk at `0x02fa2b10` and publishes its returned pointer before querying
the allocator's vtable slot `0x20`. That call receives an output pointer, the captured allocator,
and integer zero. The original provides eight bytes before its saved compiler canary and
consumes only bit `0x20` of the first returned byte. The initializer's local wire view describes
that available prefix without changing the wider shared `AllocatorInfo` view or asserting the
complete callee output type.

If the flag bit is clear, the original fatal diagnostic receives a null file, line `0x3e`, and
the incompatible-heap message. Execution continues after that call. Both the info query and the
diagnostic may change the allocator singleton; the string still receives the originally
captured allocator.

Registration passes the original destructor at `0x02a44140`, the string's address, and DSO
address `0x0553b490` to the `__cxa_atexit` entry at `0x02fbe548`. Its return value is ignored.
The guard is published after that call, with no subsequent object-field initialization.
The acquisition routine, info implementation, destructor and registration routine continue to
run through their original bindings.

The authored edge fixtures supply all eight available info-output bytes and compare the object
and old guard at the registration call. They cover allocator publication and capture, returning
diagnostics, guard widths, preserved bytes, ignored registration results and callback effects.
They are synthetic inputs; genuine gameplay recordings remain required for full verification.

## Wide-string destructor

The destructor at `0x02a44140` compares the complete unsigned capacity with eight. Values below
eight skip the allocator entirely. All other values call the allocator's `free` slot at `0x70`,
passing the captured heap pointer even when it is null. The ownership byte does not participate
in this decision. The callback receives the original object fields before any reset.

After the callback returns, the destructor writes capacity seven, length zero and a two-byte
zero terminator at the beginning of inline storage. It leaves the allocator, ownership byte,
leading unknown word, remaining union bytes and padding alone. Callback changes to those
preserved fields survive; changes to capacity, length or the first code unit are overwritten.
The callback's return value is ignored, and the destructor has no defined return value.

The authored cases cover unsigned capacity boundaries and high bits, ignored ownership,
null heap pointers, callback snapshots and changes, exact reset widths and surrounding canaries.
