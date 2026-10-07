# Style

Code under `game/` is read far more often than it is written: by contributors learning how the
game works, by reviewers, and by whoever replaces the next function. A replacement is only done
when it is **verified and readable** (CONTRIBUTING.md, "Definition of done"). A literal
transcription of the disassembly can live on a work branch while you work, never on `main`.

`tools/check_readable.py` checks the mechanical rules below (pre-commit and CI); the independent
review checks the rest. Files written before this rule are listed in
`tools/readable_allowlist.txt`, which may only shrink.

## What readable means

**Names, not addresses.** Call other original functions and read globals through named
declarations, one per function or global, next to the system that owns them:

```cpp
RT_ORIGINAL(0x02224090, ai_group_reset, void(AiGroup *, int32_t, int32_t));
RT_GLOBAL(0x059401a0, ai_manager_instance, AiManager *);

ai_group_reset(&owner->groups[i], 1, 0);
AiManager *manager = ai_manager_instance.get();
```

Never `rt::fn<...>(0x...)`, a cast of an address to a function pointer, or an address literal in
an expression.

**Structs, not offsets.** Describe the objects a function touches as structs with named fields;
pin each known offset with `static_assert(offsetof(...))`, and fill unknown space with padding
named for its offset (`uint8_t unknown_0x0010[0x2bb8];`; `void *unknown_slots[N];` for vtable
slots). Never `get<T>(p, 0x..)`, `put<T>`, `ptr_at(p, 0x..)` or `p[0x..]`. A field whose meaning is unknown gets a name that says what is
known (`flags_2c91`, `debug_mode`) and a comment; rename it when someone finds out.

**Named constants.** A number that means something gets a `constexpr` name (or an enum): flag
bits, modes, sizes, sentinel values. Literals left in expressions are 0 and 1, and values whose
meaning is the literal itself (an index into a fixed array declared nearby, a bit shift by a named
width).

**Vector code in one place.** SSE intrinsics (`_mm_*`) live only in `game/engine/`, behind types
and functions with names (`Vec4`, `with_w`, `all_equal`). Functions use those.

**No recording code in functions.** Recording the inputs of a function is the runtime's job, not
the function's: no recorder objects (`rec.`), capture JSON or `rt_capture_*` calls under `game/`.

**Floating point as written.** The game library is built with `-ffp-contract=off -fno-fast-math`
and nothing that lets the compiler fuse, reassociate or simplify float operations
(`tools/check_float_flags.py` checks every game source's flags, in CI). A commutative operation
(`+`, `*`, min, max) whose two operands can both be NaN uses the order-pinned helpers in
`game/engine/scalar.h` (`engine::add`, `multiply`, `subtract`, `divide`, `min_of`, `max_of`; for
vectors, `Vec4`'s operators) in the original's operand order: which NaN's payload survives depends
on it, and plain `a + b` may be emitted with the operands swapped. With a constant or an
integer-derived operand, plain operators are fine. Functions with float inputs get NaN-payload
edge cases, which catch a swapped order.

**Faithful, then clear.** The replacement does what the original does, including its quirks
(a value read again after a fatal error, an unusual loop order). Keep the quirk, name it, and say
in a comment why it is there. Restructure freely otherwise: early returns, helper functions,
range loops over a struct's array instead of index arithmetic.

**Comments say why.** The header comment says what the function is for, who calls it, and
anything a reader would not guess. Comments inside explain the non-obvious; they do not narrate
each line or repeat offsets the struct already names.

## Layout

- One system per directory, `game/<system>/`; shared engine types and the vector helpers in
  `game/engine/`.
- A system's structs and its `RT_ORIGINAL` / `RT_GLOBAL` declarations go in a header next to the
  code (`game/ai/hk_ai.h`), so the next function in that system reuses them.
- The exported replacement keeps its registry name (`bb_<name>` in `game/hooks.csv`).
