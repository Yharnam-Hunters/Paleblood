# Function intelligence catalog

`tools/function_catalog.py` turns the committed function inventory and optional analysis artifacts
into a local, resumable SQLite catalog. It stores no executable bytes and never records the path to
the executable. The portable `export` command writes JSON without machine-specific source paths.

## Evidence and unknowns

The current `symbols/ghidra_functions.csv` has an entry address and byte size for every function in
the Ghidra export. Those are Ghidra-defined extents, not independently confirmed boundaries. The
catalog does not infer instruction counts from byte sizes. `symbols/functions.csv` and
`symbols/verification.csv` provide named replacement and verification status; verification records
are compared with the current replacement source hash so stale evidence is visible. No call
relationship is inferred from address proximity or naming.

Optional Ghidra exports add function metrics and references. `tools/ghidra/ExportFunctionAnalysis.java`
scans the analyzed listing and writes two CSVs. `--edges` accepts:

| Column | Meaning |
| --- | --- |
| `caller` | Function entry address containing the call instruction. |
| `callee` | Function address for direct/indirect calls, `external:<name>` for imports, blank for unresolved calls. |
| `kind` | `direct`, `indirect`, `import`, `unresolved_direct`, or `unresolved_indirect`. |
| `reference` | Address of the call instruction. |
| `source` | Analysis method/artifact description. |
| `confidence` | Evidence label, such as `direct-reference`, `computed-reference`, or `unresolved`. |

`--metrics` uses `address,instruction_count,complexity,fixture_difficulty,data_references,boundary_confidence,analysis_complete,source`.
`complexity` is a documented static estimate (one plus Ghidra conditional-flow instructions), not a
behavioral measure. Fixture difficulty is `low`, `medium`, `high`, or `unknown`; the exporter sets it
to unknown because disassembly alone cannot establish fixture setup cost. Data references are JSON
arrays of referenced addresses and Ghidra reference types, not proof that each address is a global variable. An
`analysis_complete` value means the script completed its listing scan of that Ghidra function body;
it supports the reported static call graph and reference list, but does not certify Ghidra's
function boundaries or recovered indirect targets. Static references are recorded under
`memory_dependencies.static_references`; pointer-derived/indirect accesses remain unknown, and
full memory-dependency analysis is always marked incomplete. A completed call scan does not prove
that a function has no other memory or global-state dependencies.

Local capture metadata can be supplied with `--captures` as JSON containing a `functions` array.
Each item has an `address` and a `captures` list with `evidence_type` (`gameplay`, `synthetic`, or
`other`) and optional `capture_set`, `case_id`, `recorded_at`,
`source_sha256`, and/or a non-path `reference`. The importer rejects local filesystem paths and
capture content; it records provenance identifiers only. The repository currently has no per-
function gameplay capture provenance artifact, so those values remain unknown unless supplied.

Runtime relevance is optional and requires reviewed evidence. A `--relevance` CSV has
`address,score,source`; score is 0..100 and should cite a boot trace or other evidence. The ranking
does not guess boot relevance from a subsystem name.

### Ownership synchronization

Run `ownership sync` before ranking or qualifying work. It checks the replacement registry,
repository-maintained ownership metadata (when present), all open issue descriptions and comments,
all open PR descriptions and changed files, and registered local worktrees. The local ownership
snapshot is stored in SQLite and may also be written to a machine-local JSON file. Each source has
a check timestamp and a stable content fingerprint; unchanged rechecks keep cached qualifications,
while a claim, source inventory, function record, executable, analysis tool, or harness change
invalidates them. Addresses are retained only when they identify a catalog function. PR/issue
claims about imported syscalls are retained by symbol as separate evidence and do not get confused
with game-function addresses. A release comment removes that contributor's matching issue claim.

Ownership has five states: `confirmed_unclaimed`, `contributor_owned`,
`active_automation_worker`, `already_implemented`, and `unknown`. A missing source, API failure, or
unavailable non-prunable worktree makes the whole snapshot incomplete and prevents qualification.
Qualification also requires complete per-source timestamps no older than 24 hours; rerun sync before
resuming a stale campaign.
Unclaimed means every configured source was checked successfully and no applicable claim matched;
it is not inferred from one issue or an absent local claim. SQLite stores the source snapshot and
qualification decisions. No local filesystem path or capture data is exported.

### Boundary and execution qualifications

Ghidra extents begin as `ghidra_only`. The bounded qualifier labels a candidate
`cross_tool_supported` only when its complete byte extent lies in a file-backed executable ELF
`PT_LOAD`, GNU objdump independently decodes every byte and agrees with Ghidra's instruction
count, the extent ends at a recognized terminator, no catalog extent overlaps it, and at least one
direct-call edge targets its entry. This is cross-tool static corroboration, not an
`independently_confirmed` boundary or proof that the game's control flow respects the extent.
Non-executable extents, import/tail thunks, possible overlaps, decoder disagreements, and extents
without a direct-call cross-reference remain separately classified and blocked.

The candidate decoder records direct callees, import calls, indirect and unresolved calls, stack
accesses, RIP-relative references, register-indirect memory accesses, static Ghidra references,
and possible writes. A missing or incomplete dependency remains `unknown`; no memory dependency is
called absent merely because Ghidra's static reference list is empty. `eligible_for_fixture_generation`
requires a complete call scan, no calls, no data-memory or stack-frame accesses (the implicit return-stack read is bounded), and a
cross-tool-supported extent. `eligible_for_original_execution` adds bounded control flow and
instruction-safety checks for the existing original-code harness.
`eligible_for_replacement_development` additionally requires an independently checked ABI and
behavior contract. These stages are distinct. Qualification is not an execution result or a
verified replacement; synthetic register fixtures do not prove original-game behavior. Cached
decisions are hidden from `status`, `eligible`, and `explain` when the executable, catalog,
ownership snapshot, catalog/qualification code, decoder, harness, or loader-source identity no longer
matches the current evidence.

Scores are weighted heuristics, not probabilities. `confidence_percent` reports how many of the
ten evidence dimensions have supporting data. Each result includes per-component weight, evidence
ratio, weighted score, and a reason. Defaults are feasibility 20, dependency complexity 15, fixture
difficulty 10, analysis confidence 10, unlock potential 15, runtime relevance 15, ownership 5,
original execution 15, engineering effort 5, and test infrastructure 5.
Override any subset with `rank --weights weights.json`; missing keys keep their defaults. For
example, `{"unlock": 30, "runtime_relevance": 25}` gives extra weight to boot leverage.

## Build and query

The default database is `$XDG_CACHE_HOME/paleblood/function-catalog.sqlite3` (or
`~/.cache/paleblood/function-catalog.sqlite3`). Override it with `--state`. SQLite transactions
make a completed build visible atomically; interrupted writes roll back. Cache keys include the
executable SHA-256 (when supplied), catalog/tool version, explicit analysis version, canonical
configuration hash, function address, and hashes of all input artifacts. Raw configuration values
and source paths are not included in portable metadata. Changing any input changes the catalog
identity and per-function keys. `invalidate` explicitly drops cached state.

```sh
# Inventory only: missing execution and dependency facts remain unknown.
python3 tools/function_catalog.py build
python3 tools/function_catalog.py status
python3 tools/function_catalog.py function 0x017cfcc0
python3 tools/function_catalog.py dependencies 0x017cfcc0
python3 tools/function_catalog.py rank --limit 20
python3 tools/function_catalog.py cluster
python3 tools/function_catalog.py leaves --limit 20
python3 tools/function_catalog.py unresolved
python3 tools/function_catalog.py export /tmp/paleblood-function-catalog.json

# With fresh Ghidra analysis exports:
python3 tools/function_catalog.py build --executable /path/to/eboot.elf \
  --edges /path/to/call_edges.csv --metrics /path/to/function_metrics.csv \
  --analysis-version 'Ghidra 12.x / ExportFunctionAnalysis 1'

# Refresh ownership, qualify a bounded batch, and inspect the evidence.
python3 tools/function_catalog.py ownership sync --output /path/to/local-ownership.json
python3 tools/function_catalog.py qualify --batch-size 20 --executable /path/to/eboot.elf
python3 tools/function_catalog.py eligible --stage fixture-generation
python3 tools/function_catalog.py eligible --stage original-execution
python3 tools/function_catalog.py explain 0x017cfcc0
python3 tools/function_catalog.py blockers
python3 tools/function_catalog.py clusters --eligible
```

Add `--json` before the subcommand for automation. The database itself is local and resumable; JSON
export is an explicit portable snapshot.

## Example from available analysis

Paleblood source `c78efe1d327e899fab8fbfa407bcd1f18da89b08` has 157,757 Ghidra-exported entries.
For the local executable with SHA-256
`ca8813862d7b1345a6791b7d88cc1d383ede2a6bbc944f39dd3c0e0e3ad9e285`, Ghidra 12.0.3 with
GhidraOrbis 1.0144 produced 643,402 static call/reference edges and instruction metrics for all
157,757 entries. This is a reproducible static-analysis inventory, not independent confirmation
of each function boundary or runtime behavior. A complete local ownership check across the
replacement registry, optional ownership metadata, open issues/comments, open PRs, and registered
worktrees resolved the inventory into 31 already-implemented entries, one active local assignment,
and 157,725 entries without a matching recorded claim. This is a time-stamped snapshot, not a
permanent ownership guarantee; refresh it before scheduling.

A 20-entry qualification batch independently decoded each candidate's bytes with GNU objdump and
checked file-backed executable ELF ranges, direct-call cross-references, extent overlap, imports,
calls, memory operands, and bounded control flow. Twenty extents had cross-tool static support; 14
passed the gates for fixture generation and original-instruction execution, while six were blocked
by calls or memory dependencies. The 14 remain **un-named Ghidra entries**, not 14 recovered
functions: their ABI and behavior are unknown, none is eligible for replacement development, and
none has an independently confirmed boundary. Candidate `0x0073feb0` is a 3-byte, 2-instruction
extent with a direct caller, no decoded calls or data-memory operands, and only the bounded return
stack read. Its original instructions were separately run once in the harness with a deterministic
synthetic register state: the return was `RAX=0`, with no calls or writes and callee-saved registers
preserved. This one-case smoke check was not a differential comparison, game boot, semantic
identification, or verification result. The verified/replaced function counts did not change.

Address `0x017cfcc0` is named `event_flag_write`, has a 177-byte Ghidra-defined extent, 59 scanned
instructions, and an estimated complexity of 10 (conditional-flow count plus one). It remains
`edge-verified`: the recorded original-instruction differential has 14 passing fixtures, but that
does not meet the project's full `verified` promotion bar. Ghidra reports 28 known static callers
and no call sites within its body; its data-reference list is empty. Pointer-derived memory and
global-state dependencies remain unknown. This example shows how the catalog reports the evidence
and its limits together.
