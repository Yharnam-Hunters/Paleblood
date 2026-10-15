# Bounded candidate automation

`tools/automation_pipeline.py` coordinates the existing Phase 1.5 catalog and Phase 2 differential
engine for one candidate. It is opt-in and fail-closed; it does not promote function status, merge
PRs, or touch Byrgenwerth.

## Current boundary

The command accepts a **prewritten clean-room patch** with `--patch`. It does not generate
replacement source. A normal candidate must have a fresh catalog decision
`eligible_for_replacement_development`, a complete current ownership snapshot declaring it
`confirmed_unclaimed`, an evidence-backed ABI manifest, and the minimum number of local recorded
original-game fixtures. Each fixture for a new candidate must include the recorder's function and
run-ID provenance; legacy fixture sets are accepted only for a regression canary backed by a
current passing recorded-verification row. ABI evidence references must resolve to repository
files or HTTPS sources. Existing verified functions are refused except in `--regression-only`
mode, which cannot create a function PR or affect verification counts.

The local run sequence is:

1. Validate catalog qualification, ownership, executable identity, fixture identity, and ABI.
2. Create a deterministic isolated Git worktree at the selected base; apply only the supplied
   patch. Candidate patches are fenced to game/test sources, the function and hook registries,
   generated progress README, and subsystem articles; changes to runtime/tooling/policy/CI or
   symlinks are refused.
3. Copy local fixtures into a private run directory, attach the ABI evidence, and add bounded
   deterministic variants through `tools/differential.py` where ABI metadata supports them.
4. Build the isolated replacement with GCC and Clang.
5. Run original-versus-replacement comparisons with instruction tracing under both builds. Reject
   missing or incomplete instruction traces, unsupported/invalid memory accesses, incomplete CFG
   coverage, mismatches, and fixture counts below the configured minimum.
6. Run both CTest suites, Python tests, policy/readability checks, session audit, and hook tests.
7. Write a local evidence report that excludes fixture bodies and private filesystem paths from its
   portable summary.

The durable run state is keyed by base SHA, executable SHA-256, catalog identity, fixture and ABI
hashes, patch hash, and deterministic seed. Completed stages are checkpointed atomically. A run
lock prevents concurrent writers; retrying a completed run reuses stage results. The output and
worktree roots must be private locations outside the Paleblood repository.

`--dry-run` performs local work only and cannot push or create a PR. `--open-draft-pr` is a separate
explicit opt-in, forbidden with dry-run and for regression-only runs. It is reachable only after
all stages pass. It commits the supplied patch, checks the required PR session-report format,
pushes through ordinary Git hooks, and creates or reuses a **draft** PR. It never merges. GitHub CI
and human review remain required before merge.

## Example

```sh
python3 tools/automation_pipeline.py 0x01234567 \
  --name analyzed_function \
  --state "$BB_CATALOG_STATE" --elf "$BB_ELF" \
  --captures "$BB_CAPTURES/analyzed_function" --abi /private/abi.json \
  --patch /private/replacement.patch \
  --worktree-root /private/paleblood-automation-worktrees \
  --output /private/paleblood-automation-runs --dry-run
```

The ABI JSON follows the existing schema in `tools/differential.py`; each argument location,
type, direction, stack alignment, input/writable region, execution bound, and evidence reference
must be explicit. Legacy capture JSON can be normalized locally by attaching that manifest; raw
captures and executable files remain outside Git.

The synthetic tests for stage success, rejection, interruption recovery, and PR idempotence test
the orchestrator's control flow only. They are not evidence of Bloodborne behavior or function
equivalence. A passing differential run also does not promote a function: the project's existing
verification evidence and independent review remain mandatory.
