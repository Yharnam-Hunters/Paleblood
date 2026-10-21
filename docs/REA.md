# Private REA drafts

`tools/rea_drafts.py` collects static function dossiers through one REA stdio MCP session. It
does not register an MCP server in Codex or modify client configuration. It uses an installed
public `rea` executable when available, otherwise `npx -y rea-agents@6.7.0 --mcp`; the latter may
download the pinned package to npm's cache. A working caller-supplied native provider is required.

Prepare your own executables and run `tools/check_target.py` first. The exporter accepts the
pinned original `eboot.elf`, or a private analysis image built by `tools/rea_image.py` with its
matching `--provenance`. The actual loaded image must still match `target.image.sha256`.
Decompiled text and complete Evidence remain outside the repository, as required by
[CONTRIBUTING.md](../CONTRIBUTING.md).

The original Orbis ELF has no section headers. A generic provider may discover only a small
part of the catalog or merge neighboring entries. To supply the catalog as explicit analysis
hints, create a separate view:

```sh
python3 tools/rea_image.py /path/to/eboot.elf \
    --output /path/to/private/analysis.elf \
    --provenance /path/to/private/analysis-provenance.json
```

The builder checks the original target pins, preserves every loadable byte and program header,
and appends sections and zero-size function symbols. Its provenance binds both file identities
and the catalog. These seeds are analyst metadata; they do not establish independently recovered
boundaries. Use the original ELF for execution and differential verification.

```sh
python3 tools/rea_drafts.py /path/to/eboot.elf 0x0111a7f0 \
    --output /path/to/private/rea-clock --provider hopper
```

Addresses are repository catalog addresses. The fixed catalog origin `0x400000` is subtracted to
obtain zero-based ELF addresses used by Hopper's generic ELF loader. For a provider configured at
the project image base, pass `--analysis-base 0x400000`; this changes provider coordinates while
preserving ELF source-file coordinates. Every selected entry must
be in a file-backed executable `PT_LOAD`; REA must return that exact function entry and the same
original-file offset before a dossier is collected. A lookup that returns a neighboring or
containing procedure is rejected.

`--all --limit 8` selects a bounded catalog pass, with eight uncached function attempts by default.
`--resume` reuses successful dossier files only when their hashes, input and catalog identities,
provider environment, server/tool inventory and observed native analysis profile match. Previously
attempted missing/error entries are skipped so successive bounded passes advance through the
catalog; add `--retry-failed` to retry those entries explicitly. `drafts.json` records dossiers, missing procedures,
errors and nonempty pseudocode separately. These are analysis coverage counts; they do not change
the function registry, replacement status, verification results or progress counters.

```sh
python3 tools/rea_drafts.py /path/to/analysis.elf \
    --provenance /path/to/analysis-provenance.json \
    --output /path/to/private/rea-pass --all --limit 8 --resume
```

The exporter inherits the provider environment. For example, a larger
`REA_HOPPER_STARTUP_TIMEOUT_MS` can allow a large image's startup analysis to finish; set the
exporter's `--timeout` in seconds to at least that interval. Explicit `HOPPER_LOADER_ARGS_JSON`
loader arguments are part of the resume identity, and REA's returned native profile digest is
also checked. Selecting a different loader analysis mode requires a fresh output directory.
REA 6.7.0 always requests Hopper's background analysis. Passing `-A` or `--no-analysis` in the
loader override conflicts with that request and is rejected by Hopper. Allow the initial
analysis to finish rather than combining those flags.

Each batch exports the complete REA Evidence bundle, then requests `close_binary` in cleanup.
Timed-out requests are cancelled and given a bounded drain interval. If a request does not drain,
or REA cannot confirm cleanup, the command fails and records cleanup as unconfirmed. Follow the
provider's diagnostic before reopening a target; process exit alone does not establish that a
native document closed.

Pseudocode is a draft for analysis. A finished replacement still needs readable source,
differential verification, deliberately failing mutations, original-game recordings and independent
review under [VERIFY.md](VERIFY.md). The native verification harness requires Linux x86-64;
collecting static drafts on another host does not satisfy those checks.
