# Bounded implementation generation

`tools/implementation_generator.py` adds an opt-in proposal stage before the existing
[automation pipeline](AUTOMATION_PIPELINE.md). Generation is disabled by default. There is no
built-in implementation template: an explicitly configured provider receives a structured JSON
analysis package and returns proposed C++, a signature, assumptions and evidence references.
The included Codex CLI adapter can invoke an actual coding model. Unit tests use an explicitly
labeled mock provider; neither a mock nor model output proves game behavior.

## Supported increment

The initial generator accepts small, register-only leaf functions with sequential instructions
and a terminal return, at most 256 bytes. Arguments must be explicitly evidenced unsigned
32/64-bit integers in consecutive System V AMD64 argument registers. Return width, stack
alignment, absence of input/writable memory regions and execution bounds must be explicit.
Pointers, floating point, calls, branches, data-memory operands, unsupported decoder forms and
ambiguous extents are rejected. These restrictions bound this increment, not the whole verifier.
ABI-note inputs are bounded to 32 KiB. Private inputs/output are refused inside any Git worktree,
including other checkouts of the same project.
The instruction allowlist is in `implementation_generator.py`; the existing tracer still decides
whether actual original and compiled replacement traces are complete.

The current catalog must identify an unimplemented, freshly unclaimed candidate with supported
boundaries and original-execution qualification. Instructions are decoded again from the exact
local executable. Every byte must decode inside the selected extent, with the catalog instruction
count and executable SHA matching. Caller/type information is included without treating inferred
names as behavior. The package records executable/instruction hashes, qualification and ownership,
ABI notes and tool identity. Textual instruction analysis stays private; it is never committed.

ABI references currently resolve to supplied local notes as `urn:sha256:<notes hash>`. The note
contents accompany the private package. This establishes input identity, not independent approval.
`--experimental` permits a proposal before replacement-development admission. It **cannot** admit
an ABI, change function status or become eligible for a function PR.

## Prepare and invoke

Keep the catalog, executable, analysis notes, captures, provider configuration and output outside
Git. Refresh ownership and qualification with the existing catalog commands first.

```sh
python3 tools/implementation_generator.py "$ADDRESS" \
  --state "$CATALOG" --elf "$ELF" --abi "$ABI" --abi-evidence "$ABI_NOTES" \
  --output "$PRIVATE_RUNS" --experimental --dry-run

# Explicitly authorize the selected provider to receive the private analysis package.
python3 tools/implementation_generator.py "$ADDRESS" \
  --state "$CATALOG" --elf "$ELF" --abi "$ABI" --abi-evidence "$ABI_NOTES" \
  --output "$PRIVATE_RUNS" --experimental \
  --enable-generation --provider-config "$PRIVATE_PROVIDER_CONFIG" \
  --cases "$CONSTRUCTED_FIXTURES" --loader "$BUILT_LOADER"
```

Dry-run validates analysis without invoking a provider, compiling or writing output. Omitting
`--enable-generation` has the same disabled behavior. No default model, credentials or network
provider are selected automatically. The provider executable is trusted operator configuration;
the generator uses an argv array, no implicit shell, a minimal environment and a private working
directory. Never configure an adapter to upload game binaries, captured data or unreviewed private
material. Review the provider's data handling before authorizing private textual analysis.

Example private provider configuration (replace all installation/model values explicitly):

```json
{
  "kind": "model",
  "provider": "codex-cli",
  "model": "YOUR_CONFIGURED_MODEL",
  "argv": ["/usr/bin/python3", "/your/checkout/tools/generation_provider_codex.py",
           "--codex", "/your/codex", "--model", "YOUR_CONFIGURED_MODEL",
           "--auth-directory", "/your/existing/codex-auth-directory"],
  "timeout_seconds": 300,
  "max_attempts": 1
}
```

The Codex adapter defaults to explicitly labeled project-owned synthetic analysis. Other analysis
requires its separate `--allow-private-analysis` flag as well as the generator opt-in. Authentication
remains operator-managed; credentials are neither loaded into analysis nor printed in reports.
The adapter uses ephemeral, read-only `codex exec` with a structured response schema and ignores
user configuration that could enable unrelated integrations. See the official
[non-interactive execution documentation](https://developers.openai.com/codex/noninteractive/).
The coding CLI remains a trusted provider process; its read-only mode is distinct from the offline
namespace used to execute generated code.

The JSON stdin request contains `schema`, `analysis_sha256` and `analysis`. A response must contain:

- `analysis_sha256`: the exact request identity.
- `signature`: the exact supplied symbol, calling convention, return and argument types.
- `source`: independently reconstructed C++ exporting the supplied symbol with C linkage.
- `assumptions`: an explicit list, possibly empty.
- `evidence`: supplied ABI-note references supporting the proposal.

Inline assembly and arbitrary preprocessor directives are refused. This is a conservative source
filter, not an automatic originality/readability review or a C++ security sandbox. Source remains
quarantined; human review is mandatory before production integration.

## Compilation, replay and recovery

GCC and Clang compile the proposal with an exact-signature assertion, warnings as errors and the
project's floating-point flags. Compilation and replay require Linux bubblewrap user/mount/PID
namespaces. They have no network, home directory, Git credentials or repository write access.
Only explicit input files, `/usr`, the host's read-only `/lib`/`/lib64` runtime layout and a private run directory are mounted; replay mounts the run
read-only. Missing namespace support is a blocker, never an unsafe fallback. CPU, address-space,
wall-clock, file/output and attempt limits are bounded. Limits are per process, not a claim of a
system-wide cgroup quota or protection against kernel vulnerabilities.

On Ubuntu runners the build installs bubblewrap and prefers its distribution-provided AppArmor
profile. Where the image lacks it, CI explicitly installs a small launcher-specific `userns` policy
on that disposable runner. Global AppArmor restrictions remain enabled and network isolation is
unchanged; no workstation policy is installed by the tool. An invalid profile or unavailable
sandbox fails the required regression job. The suite checks a separate network namespace and zero
effective child capabilities. This follows Ubuntu's documented
[per-application namespace policy](https://discourse.ubuntu.com/t/understanding-apparmor-user-namespace-restriction/58007).

Optional replay reuses the existing native harness, loader, tracer, comparator and strict memory/
CFG gate. Both compilers must pass the exact same 1–64 fixtures; missing cases, ABI conflicts,
unexpected pointer/import/global assumptions, partial traces, host transitions and mismatches
fail. Evidence retains both execution sides under an identity binding package, fixtures, loader,
compiled libraries and relevant tool sources. Trace differences between equivalent internal paths
remain diagnostic, not mismatches solely because PCs differ.

Scalar replay also rejects the harness's `stack` argument list and `gs` thread-pointer setup.
Neither is declared by this increment's register-only ABI. The legacy `stack_args` spelling is
also refused; checking that spelling alone does not constrain the harness's actual stack input.

Replay here is deliberately **constructed-input regression**, even if someone supplies JSON with
capture metadata. It never sets `genuine_original_game_differential_passed`. That status and PR
eligibility require the existing independent pipeline with recorded provenance and its minimum
coverage. Passing a few generated cases is not recorded-game verification.

Run identity includes analysis, provider configuration and executable/script hashes, generator
source and compiler versions. Atomic stage state and a run lock preserve responses, output hashes,
attempt counts and compilation results. Completed generation is reused without another model
call. Replay is rerun because fixtures/loader/tooling can change; stale success is cleared first.
Interrupting an in-flight provider consumes an attempt; it cannot cause unlimited retries. A
completed response can resume after interruption. Ambiguous attempts require inspection or a
deliberate new configuration, not silent resubmission. Model generation need not be deterministic;
the saved request/response and hashes make an accepted proposal replayable.
Nested provider processes bind their lifetime to their parent so an interrupted adapter cannot
leave its separately grouped coding CLI running. A regression kills the adapter and checks that
the nested provider also terminates.

## Handoff to the existing pipeline

Readable registration/integration is still a manual step. Supply the normal integration patch
plus `--generation-report <run>/generation.json --generated-source-path game/<system>/<file>.cpp`.
The pipeline checks qualified/model origin and exact source identity/symbol, then independently
rechecks all ownership, ABI, recorded-input, complete-trace, CFG, compiler, policy and review gates.
Experimental/mock proposals are rejected, even if their report claims success. The generated file
must match exactly; changing it creates a separately reviewed, prewritten implementation rather
than silently retaining generation provenance. No generated-stage flag authorizes a PR or status
promotion. Draft PR creation remains a separate opt-in after the complete pipeline; no merge is
automatic.

## Initial investigation: `0x02ab7be0`

This address has a small import-free instruction path and caller evidence supporting two 32-bit
machine inputs and a 32-bit return. Prior constructed-input traces establish that the bounded
instruction path is observable. They do not establish source-language types, gameplay meaning,
an admitted replacement ABI or recorded-game coverage. The package can be validated in explicit
experimental mode, but the candidate remains ineligible for the production pipeline.

There are currently no matching original-game recordings in the inspected local libraries. The
next prerequisites are independent ABI review/admission, legitimate naming and supported capture
through the existing contributor-owned recording work. This increment does not duplicate that
capture implementation. Real model-backed reconstruction can be demonstrated separately with
repository-owned synthetic instructions; such results do not change verified-function counters
or boot evidence.

A separate interactive-agent proposal for this target was compiled locally and compared with the
actual executable on eight constructed fixtures under both compilers, with no mismatches. Three
deliberately wrong proposals (value mask, position mask and return value) were rejected by original-
instruction comparisons under GCC; both compilers compiled the mutants, but rejection stopped
their replay before Clang. These are experimental comparisons, not automatic model reconstruction
of a game function, gameplay recordings, ABI admission or a verification-status promotion. The
private instruction package was not sent to the external model during the synthetic provider demo.
