# Instruction-trace support matrix

The optional instruction tracer is a bounded observation tool for fixture executions on Linux
x86-64. It does not implement x86 instructions. It decodes Capstone detail metadata, records
selected guest memory accesses around single-stepped instructions, and makes verification fail
when a known access class is outside that model. A `complete` trace means complete only for the
supported rows below; it is not a proof that arbitrary x86-64 instructions were exhaustively
understood.

| Instruction family | Status | Current guarantee |
| --- | --- | --- |
| Legacy scalar instructions with explicit memory operands | Supported when Capstone supplies a known read/write class and a 1–64-byte width | Effective address, declared-region bounds, bytes before the instruction, and bytes after writes are recorded. Invalid address registers, segments, widths, or access classifications stop the trace before the instruction executes. |
| `LEA` register/address-expression form (opcode `8D`) | Supported address calculation, not a memory access | The decoder retains the address expression separately and single-steps the instruction without a load/store event. A computed address need not name a valid data region. Real loads/stores through its result still receive the normal bounds checks. An ambiguous LEA operand layout or LOCK prefix fails closed. |
| Legacy MMX/SSE explicit memory operands | Supported when the operation has a single, fully described memory operand no wider than 64 bytes | Operand bytes are observed as above. `MOVSD`/`CMPSD` string spellings are distinguished from scalar SSE using Capstone's operand text. |
| Near `CALL`/`RET`, `PUSH`/`POP`, `PUSHF`/`POPF`, and `LEAVE` | Supported for modeled near control transfer and 2- or 8-byte stack widths | Implicit stack read/write is traced. `POP` to an RSP/ESP-based memory destination uses post-increment RSP/ESP; the popped source read uses pre-increment RSP. `PUSH [rsp]` reads from pre-decrement RSP. 16-bit register stack operations have synthetic tests. Segment-register push/pop is rejected because stack width and segment/interrupt-shadow state are not modeled; far calls/returns are rejected. |
| `MOVS`, `CMPS`, `STOS`, `LODS`, `SCAS`, `INS`, and `OUTS` string operations | Unsupported; fail closed before executing the instruction | Implicit RSI/RDI accesses, index/flag changes, and I/O behavior are not traced. |
| REP/REPE/REPNE-prefixed string operations | Unsupported; fail closed before executing the instruction | Repeated accesses and iteration-dependent state are not traced. Diagnostics retain the prefixed mnemonic. |
| VEX/EVEX instructions with memory operands, including masked and gather/scatter forms | Unsupported; fail closed before executing the instruction | Vector widths, masks, partial writes, and lane-dependent accesses are not inferred from a generic memory operand. |
| `LOCK`-prefixed memory operations, memory `XCHG`, and `CMPXCHG8B`/`CMPXCHG16B` | Unsupported; fail closed before executing the instruction | Atomicity and ordering semantics are outside the single-step memory-event model; the latter two have implicit atomic semantics even without a `LOCK` prefix. |
| System transitions and privileged/system-state instructions | Unsupported; fail closed before executing the instruction | Includes syscall/interrupt/return transitions, port I/O, control/system-register operations, virtualization transitions, transactional-memory control instructions, command-enqueue instructions, PKRU controls, FSGSBASE operations, and CET shadow-stack controls. Their effects may involve kernel memory or state outside the tracee's declared regions. |
| Hardware random-number instructions (`RDRAND`, `RDSEED`) | Unsupported; fail closed before executing the instruction | Results are not deterministic fixture inputs and cannot be replayed as ordinary register-only behavior. |
| Special memory operations | Unsupported; fail closed before executing the instruction | Includes `XLAT`, `ENTER`, descriptor-table operations, prefetch/cache-maintenance operations, FPU/SIMD save/restore or environment images whose access footprint is special or feature-dependent, `MASKMOVQ`/`MASKMOVDQU`/`VMASKMOVDQU`'s implicit RDI destination, and `MOVDIR64B`'s register-addressed destination. |
| Undecodable instructions and unexpected execution signals | Unsupported; fail closed | The trace includes the instruction address when known, mnemonic (or `<undecoded>`), capability, and rejection reason. The instruction is not stepped again. |
| Calls into host code outside declared guest/replacement ranges | Partial; trace is incomplete | The tracer may step over the host call to let a fixture finish, but the call body is explicitly reported as an `execution_scope` gap. Such a trace cannot pass differential verification. |
| Recognized harness import entry/return snapshots | Diagnostic only; trace and CFG remain partial | Bounded GPR and stack/buffer snapshots report net changes, not reads, same-value writes, transient writes, host instructions, or host CFG. Missing/truncated bytes remain explicit. These observations are excluded from verification acceptance and coverage feedback. See [VERIFY.md](VERIFY.md#import-boundary-observations). |

The implementation's classified implicit and special families are explicit in
`tools/instruction_trace.py`. Capstone does not provide a universal flag for every instruction
whose architecture-defined behavior implicitly touches memory. This matrix therefore documents
the classified families and their boundaries; it must be extended whenever a new implicit or
special memory operation is identified. An unlisted architectural implicit-memory instruction
is a remaining coverage risk and must not be treated as approved support evidence until audited.

For supported instructions, dynamic CFG coverage records basic-block entries and observed outgoing
edges per fixture. Conditional outcomes, loop backedges, calls, returns, and observed indirect
targets are represented using guest virtual addresses or replacement module-relative offsets.
Coverage states are `complete`, `partial`, or `unavailable`; an unsupported instruction before the
first step has unavailable coverage, while a fully observed linear path can have complete coverage
with zero branch edges. Partial or memory-incomplete traces are excluded from feedback. This is
bounded per-fixture path coverage, not a complete static CFG for the executable.

The differential result maps blocks and edges back to fixture IDs. `tools/differential.py explore`
accepts `--coverage-from <differential.json>` to rank seed fixtures with newly observed edges or a
conditional outcome whose counterpart has not yet been seen. The ranking is advisory and only
uses complete prior traces. Seeds without complete matching evidence are labeled `unknown` rather
than treated as zero-risk. Generated inputs remain subject to the same evidence-backed ABI rules.

Unsupported instructions stop before execution. The trace is marked incomplete and
`tools/verify.py compare` fails the case even if both sides stop at the same instruction. Its
diagnostic names the guest address, decoded mnemonic, capability category, and reason. POP/RSP
effective-address tests and out-of-bounds destination tests are retained in
`test/test_harness.py`.
The LEA regression also checks a deliberate real-load variant: address arithmetic alone is
complete, while dereferencing the resulting out-of-bounds address remains incomplete and fails
verification. This follows the LEA effective-address semantics in the Intel instruction reference
linked below; recognizing the mnemonic does not grant support to other pseudo-memory operands.

Instruction-family details were checked against the [Intel SDM Volume 2B](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf)
and [combined Volume 2 instruction reference](https://cdrdv2-public.intel.com/774492/325383-sdm-vol-2abcd.pdf), including the implicit RDI destination of `MASKMOVDQU`, the register-addressed 64-byte destination of `MOVDIR64B`, and the implicit atomic behavior of `CMPXCHG8B`/`CMPXCHG16B`.

These are deterministic synthetic fixtures. They test tracer behavior only; they are not original
Bloodborne execution or function-equivalence evidence and do not change function verification
counts.
