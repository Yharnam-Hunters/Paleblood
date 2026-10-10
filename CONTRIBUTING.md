# Contributing

Paleblood is a decompilation of Bloodborne with its own runtime: the game's code rewritten as readable C++, one verified function at a time.
<!-- status:start -->
The game doesn't run on Paleblood yet: 21 of 157757 functions are verified (0.01%). On [our own runtime](runtime/) the executable's boot gets as far as `scePthreadAttrGetaffinity` (libkernel, called by libc.elf), with 238 of 686 system imports provided.
<!-- status:end -->

The goal is a complete source port: every function rewritten and proved, running on our own
runtime ([README](README.md#the-goal)). It is open to everyone who wants to help. Reverse
engineering, C and C++, PS4 internals, the runtime's system libraries and writing up how a system
works all count, and the smallest correct pull request is
welcome.

## Getting started

1. Read the [README](README.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the
   port works, and [QUIRKS.md](QUIRKS.md) for the traps.
2. Choose a track. Decompilation work uses your own console and your own game copy; follow
   [docs/ONBOARDING.md](docs/ONBOARDING.md). Runtime system-call work needs no game files, dump,
   or captures.
3. Pick an issue labelled **good first issue**, a function in [issue #34](https://github.com/Yharnam-Hunters/Paleblood/issues/34),
   or a target from [NEXT.md](NEXT.md).

Questions go in [Discussions](https://github.com/Yharnam-Hunters/Paleblood/discussions) or in
the issue you are working on. How the systems work, the devlog and the progress dashboard are on
the [documentation site](https://yharnam-hunters.github.io/byrgenwerth-site/).

## Runtime system calls (issue #34)

Runtime contributions are separate from decompilation. Read
[docs/runtime-syscalls.md](docs/runtime-syscalls.md) and review the evidence for the call before
implementing it. Claim one function or a tightly related group in issue #34 before starting, so
maintainers can avoid duplicate work. Use a branch named `runtime/<function>`; for a group, use
the lead function's name.

Implement the call in `runtime/syslib/<group>.c` and register it with `RT_SYSLIB`. Add or update
the applicable cases in `test/test_syslib.c`. Unknown target behavior must stay explicit and fail
loudly; do not choose guessed return values just to advance `tools/boot.py`.

Runtime code is clean-room: use public API documentation, independent reverse engineering, and
observed behavior as facts. Do not copy, adapt, or model implementation code from another PS4
runtime or emulator. Keep one call or a
tightly related group in each PR. Runtime-only PRs need no proprietary game dump, captures, or
function-level `verify.py` results. CI compiles with GCC and Clang, runs the RT_SYSLIB test and
the rest of CTest, and retains the repository's policy checks. CI selects the concise runtime
report from changed files, not a manually applied label. Include sources, confirmed behavior,
unresolved questions, tests, and the clean-room basis in that report.

After the PR merges, a maintainer runs `tools/boot.py` locally and posts the result on the PR.
This boot check is not required from an outside contributor and does not require sharing game
files or captures.

## Decompilation workflow

1. **Claim it.** Find or open an issue and comment before you start (the **Claim function or
   cluster** template); one open claim per function, so nobody works twice.
2. **Fork** the repository on GitHub and clone your fork. Run `tools/install_hooks.sh` (the
   hooks refuse game data and other mistakes before they reach a commit).
3. **Branch** from current `main`: one function or one tightly coupled cluster per branch.
4. **Replace and verify** as in [docs/VERIFY.md](docs/VERIFY.md): readable code
   ([STYLE.md](STYLE.md)), `tools/verify.py` on the recorded cases in the capture library and on
   edge cases, and deliberately wrong versions that fail; then the independent review.
5. **Open a pull request** from your fork's branch to `main`, with the session report (below).
   Keep it small enough to review in one sitting.
6. **CI and review** both have to pass. The first time you contribute, a maintainer approves
   CI to run on your pull request. Resolve conflicts yourself; rebasing and force-pushing your
   own branch is fine.
7. A maintainer merges.

Systems that lack documentation get a **Document a system** issue first: its page in
[`docs/systems/`](docs/systems/) is the map for everyone else.

### Your fork is public

Forks of a public repository are public. Everything you push there, including branches you
never open a pull request for, is visible to anyone: never push game files, dumps, decompiler
projects or raw decompiler output, not even temporarily. Keep your dump, your Ghidra project,
drafts and captures outside the repository (the tools default to `../data`, next to your clone).

## Pull request contents

Runtime-only PRs with runtime implementation or RT_SYSLIB tests use the concise runtime report
in the PR template. Documentation-only PRs use the concise documentation report. CI chooses the
path from changed files, not a label; unknown code paths use the full **session report**.
Decompilation and mixed-track PRs run the full report path even when they also change runtime
files. Run `tools/session_report.py` on your branch (attach `verify.py` results with `--verify`
and the case directories with `--captures`) and paste its output into the PR. CI checks that
every function replacement in the report has matching, non-empty, passing verification evidence,
and that the report includes each replacement status change found in `symbols/functions.csv`.

Every pull request that replaces game code contains, in the description:

- the `tools/verify.py run` output for each function, on cases recorded in a real run and on
  edge cases ([docs/VERIFY.md](docs/VERIFY.md)),
- the deliberately wrong versions you tried and how each failed (the mutation test),
- the `symbols/functions.csv` and `game/hooks.csv` rows for the change.

A function's status in `symbols/functions.csv` goes through three levels:

| Status | Means |
|---|---|
| `replaced` | it is in the hook registry and builds |
| `edge-verified` | `verify.py` passes on its edge-case generator's cases, against the original |
| `verified` | it also passes on inputs recorded in the game (the capture library), and the independent review approved it |

`edge-verified` is for functions whose code the game doesn't reach where you can record (a
Chalice Dungeon state, say, without a save that gets there). You don't upgrade it by hand:
`tools/promote.py` (run by `tools/end_session.sh`) re-runs every `edge-verified` function once
recordings for it exist in your capture library, and marks it `verified` only if every recorded
and edge case passes. A failure leaves it where it was and is reported.

### Definition of done

A function goes to `main` only when it is **verified and readable**:

- **Readable** means it follows [STYLE.md](STYLE.md): named calls and globals instead of
  addresses, structs with named fields instead of offsets, named constants, vector code only in
  `game/engine/`, no recording code. `tools/check_readable.py` checks the mechanical part in the
  pre-commit hook and in CI. A literal transcription of the disassembly is a fine way to start,
  on your work branch; it never goes to `main`. Files written before this rule are listed in
  `tools/readable_allowlist.txt`; that list may only shrink, and CI fails if it grows.
- **Reviewed:** before a function becomes `verified`, an independent reviewer (not the author:
  another contributor, or, for AI-assisted work, a separate review session that did not write
  the code) checks the verification and the readability against STYLE.md, and approves. The
  approval is a row in `symbols/reviews.csv` (address, name, date, reviewer, verdict, notes);
  `tools/validate_functions.py` refuses a `verified` function without one, and
  `tools/promote.py` won't promote without one.
- **No unexplained failures:** every run of a function's cases is recorded with
  `tools/verify.py run --record` in `symbols/verification.csv` (counts only). A `verified`
  function needs current recorded and edge results (for the code as it is now), and no failing
  case anywhere, under any option, unless the case is in `symbols/quarantine.csv` with the
  reason it can't be used (docs/VERIFY.md, "Recorded results and quarantine").
  `tools/validate_functions.py` checks it in CI.

Investigation notes and logs go in the pull request, not in the repository. No screenshots or
clips of the game there either (see Rules).

## Your own copy (decompilation work)

Decompilation work starts from your own console and your own copy of the game: you dump
Bloodborne and its 1.09 update from your own PS4, and every tool in this repository works only
on that dump, on your machine. The project does not provide or point to game files, pkgs,
firmware, keys or decryption tools.

- **Requests for game files, or links to them, are removed**, wherever they are posted: issues,
  pull requests, Discussions, commits, comments. That includes pkgs, dumps, extracted files,
  firmware and keys, and asking someone to "send" or "share" them. Repeated or deliberate
  requests or links may lead to a ban from the organization.
- Never attach anything from your dump to an issue, a pull request or a Discussion.

## Rules

- **Never commit** game binaries (ELF, SELF, PRX, PKG), assets, extracted data,
  decompiler projects or raw decompiler dumps. The hook and CI reject them. If you are unsure,
  do not commit it.
- **No screenshots, clips or images of the game**, anywhere in this repository or other project repositories:
  not in commits, issues, pull requests or Discussions. The hook and CI refuse image and video
  files (by extension and by contents). The only images allowed are recordings of our own tools'
  output (such as a terminal running `tools/verify.py`), under `docs/assets/` and listed in
  `docs/assets/ALLOWLIST` with the command that produced them (`tools/terminal_gif.py`).
- Write source, not a transcription. Readable names and structure; behavior has
  to match, bytes do not.
- Do not leave `FUN_`, `DAT_`, `undefined8` or `local_` names from the
  decompiler in committed code.
- `runtime/` is generic and holds nothing about this game. `game/<system>/`
  holds the game's code. `tools/check_agnostic.py` enforces it.
- Unknown behavior fails loudly with the address. No silent fallbacks.
- Numbers in docs are generated by `tools/progress.py`. Never type a count or
  percentage by hand.
- Run the hooks: `tools/install_hooks.sh`.

## Naming

- Systems are the directories under `game/` (`frame_timing`, `event`, `kernel`, `camera`,
  `input`, `render`, `loading`, `audio`). A new system is a new directory and a
  **Document a system** issue.
- `symbols/functions.csv` name: lowercase snake_case starting with the system,
  for example `frame_timing_update_delta`.
- Replacement: `bb_` plus the name, `extern "C"`, for example `bb_frame_timing_update_delta`.
- Files: `game/<system>/<topic>.cpp` (C++20; plain C in `.c` is fine too), snake_case.
- A function must be at least 14 bytes to be hooked (the jump), checked by the validator.
- Addresses: `0x` and 8 lowercase hex digits, ascending in every CSV. They are PS4
  virtual addresses, **Ghidra image base `0x400000`**: the ELF `p_vaddr` plus `0x400000`, the
  same as the community patch files. Import the eboot with that base. Raw ELF virtual addresses
  are these minus `0x400000` (QUIRKS.md, "Address conventions").
- `notes` in `functions.csv`: one line, 200 characters at most.

## Drafts

Replacements may start from a draft: the decompiler's output for the function, written by
`tools/draft.sh` from your own Ghidra project into a directory outside the repository. A draft
is a starting point, never a commit to `main`: what gets merged is the replacement after it is
renamed and restructured into readable code ([STYLE.md](STYLE.md); names from `functions.csv`,
no `FUN_`/`DAT_`/`local_` names, no decompiler artefacts) and verified like any other. The decompiler is often wrong about
types, signedness and calling conventions, so the disassembly decides, not the draft.

## Commits

`system: what changed`, imperative, with `Refs #N` or `Closes #N` at the end.

## License

The project is GPL-2.0-or-later. By contributing you license your work under the same terms.
New source files start with `SPDX-License-Identifier: GPL-2.0-or-later`.

## Conduct

Be kind and assume good faith. Review the code, not the person. Credit others' findings. No game
piracy, no requests for or links to game files (see **Your own copy**), no harassment: those get
removed and may lead to a ban. Report problems to the
maintainer in an issue or privately.

## AI-assisted work

Allowed if disclosed and verified. Every AI-assisted commit must keep a
`Co-Authored-By` trailer naming the tool actually used. For Codex, use
`Co-Authored-By: Codex <noreply@openai.com>`. A pull request note does not replace the trailer.
Do not rewrite existing commits to add or change disclosure. AI-assisted decompilation is
verified like any other: `verify.py` output and the independent review are mandatory, and the
author answers for every line.
