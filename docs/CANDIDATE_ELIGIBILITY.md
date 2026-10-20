# Evidence-backed automation candidates

Type discovery and a high scheduler score help select investigations. They do not establish ABI,
behavior, recording provenance, ownership or verification readiness. This workflow preserves those
separate prerequisites and keeps all executable analysis and captures outside Git.

## Refresh ownership and qualification

Use the existing `function_catalog.py ownership sync` and `qualify` commands described in
[FUNCTION_CATALOG.md](FUNCTION_CATALOG.md). A current snapshot requires successful checks of all
five sources: the replacement registry, repository ownership metadata, open issues and comments,
open PRs, and current local assignments. Every source timestamp and the snapshot timestamp must be
within 24 hours and not future-dated. Missing sources, stale claims and conflicting owners block
selection; they never automatically release a contributor's work. Already implemented functions
are excluded from new replacement development. The pipeline consumes this same computed ownership
record rather than a second, incompatible ownership representation.

Supported type relationships, classification conflicts and measured complexity influence bounded
qualification ordering after call and memory constraints. They do not override qualification
blockers. An execution-investigation candidate is not a replacement-development candidate.
The static dependency scan distinguishes `LEA` address arithmetic from a load. It records the
calculation explicitly; a later dereference still needs memory fixtures. That distinction can
remove a false execution-investigation blocker, but cannot establish a pointer's extent, a
function signature, gameplay provenance or replacement eligibility.
For a selected investigation, pass `qualify --address ADDRESS` (repeat for a short list).
This limits which entries are investigated while retaining the full catalog for overlap checks,
the existing graph and ownership gates. An absent address is an error; selecting an entry does
not claim it or make it eligible.

## Inventory local captures

```sh
python3 tools/function_catalog.py --state "$CATALOG" --json trial-inventory \
  --captures "$PRIVATE_CAPTURES" --limit 20 --minimum-recorded 32 \
  --output "$PRIVATE_OUTPUT/inventory.json"
```

Repeat `--captures` for separate private roots. By default, the bounded report covers recognized
capture directories, current qualification records, and a ranked subset. `--address ADDRESS` can
restrict it. Unknown capture sets and ambiguous multiple directories remain explicit. Unavailable
directories have null counts, distinct from available directories containing zero recordings.

Each row records executable identity, canonical replacement status, fresh ownership, ABI evidence,
root and edge counts, provenance problems, instruction/CFG evidence, blockers and ranking context.
Recorded cases for new candidates need a matching function name, nonempty run ID, and exact
`capture.executable_sha256`. Missing legacy executable provenance is unknown, not silently inferred
from today's executable. Generated inputs and exploration variants are not recordings. Duplicate
identities within a capture run are rejected. `abi.json` is a manifest, never a fixture.

The report does not include fixture bodies, run IDs or private paths. Store it privately anyway:
it contains unpublished research metadata. Per-candidate results are transactionally cached in the
existing catalog's `candidate_inventories` table. Its identity binds catalog/function metadata,
ownership, qualification, exact fixture files, ABI, trace evidence, tool sources and policy limits.
Changed inputs require regeneration. Explicit catalog invalidation clears inventories.

## Establish original-only observations

Only attempt this after evidence establishes a safe ABI and current qualification permits original
execution. An inventory cannot manufacture those prerequisites.

```sh
python3 tools/differential.py --state "$CATALOG" original "$ADDRESS" \
  --elf "$PRIVATE_ELF" --cases "$PRIVATE_CASES" --trace-instructions \
  --trace-instruction-limit 100000 --json
python3 tools/differential.py --state "$CATALOG" evidence "$ADDRESS" \
  --out "$PRIVATE_OUTPUT/evidence" --json
# Use the printed run identity to select evidence/<RUN_ID>/evidence.json.
python3 tools/function_catalog.py --state "$CATALOG" --json trial-inventory \
  --captures "$PRIVATE_CASES" --address "$ADDRESS" \
  --trace-evidence "$PRIVATE_OUTPUT/evidence/$RUN_ID/evidence.json" \
  --output "$PRIVATE_OUTPUT/inventory.json"
```

The original runner normalizes the harness's file-keyed batch results into an explicit case list,
retains faults and rejects missing cases. Requested tracing returns a failure when observations
are incomplete. Evidence must match executable, address, fixture hashes, ABI and current harness,
decoder, loader, verification and engine sources. Every case needs complete bounded instruction
tracing, valid memory observations and complete CFG feedback eligibility. External transitions,
import observations, skipped host calls, malformed observations and invalid regions block use.

`trial_inputs_ready` means recording/ABI/ownership/qualification prerequisites passed.
`automation_trial_eligible` additionally requires complete current original observations. Neither
means a replacement exists or is equivalent. A synthetic unit test can exercise these contracts
without becoming real-game evidence.

## Run the existing opt-in pipeline

Pass `--eligibility-report` and `--trace-evidence` along with the existing private ABI, captures,
executable and prewritten clean-room patch. The pipeline recomputes inventory eligibility; it does
not trust a saved Boolean. It still builds and differentially checks the patch under both compilers,
enforces all existing memory/CFG/provenance gates, and permits only an explicitly requested draft PR.
See [AUTOMATION_PIPELINE.md](AUTOMATION_PIPELINE.md). The optional
[generator](IMPLEMENTATION_GENERATOR.md) can propose quarantined source; readable integration and
the existing evidence gates remain separate. No command
here promotes function status, merges a PR or publishes documentation.

Import snapshots at `sceKernelClockGettime` remain diagnostic evidence only. They do not trace the
host dispatcher or restore complete memory/CFG observations. Existing verified clock captures are
regression inputs, not new-function candidates. The next real automation trial requires an unowned,
unimplemented function with supported semantics/ABI and matching recordings on a completely
observable path; missing evidence must remain a blocker.
