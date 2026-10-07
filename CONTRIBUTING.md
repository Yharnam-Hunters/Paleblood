# Contributing

Paleblood is open to everyone who wants to help rebuild Bloodborne, one verified function at a
time. Reverse engineering, C and C++, PS4 internals, testing in the game and writing up how a
system works all count, and the smallest correct pull request is welcome.

## Getting started

1. Read the [README](README.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the
   port works, and [QUIRKS.md](QUIRKS.md) for the traps.
2. Start from your own console and your own copy: Bloodborne (CUSA03173, EU) with the 1.09
   update, dumped from your own PS4 (see **Your own copy** below). Nobody here will send you
   game files, and nobody will ask you for yours.
3. Follow [docs/ONBOARDING.md](docs/ONBOARDING.md): from your own dump to your first pull
   request in about an hour.
4. Pick an issue labelled **good first issue**, or a target from [NEXT.md](NEXT.md).

Questions go in [Discussions](https://github.com/Yharnam-Hunters/Paleblood/discussions) or in
the issue you are working on. How the systems work, the devlog and the progress dashboard are on
the [documentation site](https://yharnam-hunters.github.io/byrgenwerth-site/).

## Workflow

1. **Claim it.** Find or open an issue and comment before you start (the **Claim function or
   cluster** template); one open claim per function, so nobody works twice.
2. **Fork** the repository on GitHub and clone your fork. Run `tools/install_hooks.sh` (the
   hooks refuse game data and other mistakes before they reach a commit).
3. **Branch** from current `main`: one function or one tightly coupled cluster per branch.
4. **Replace and verify** as in [docs/VERIFY.md](docs/VERIFY.md): `tools/verify.py` on cases
   recorded in a real run and on edge cases, deliberately wrong versions that fail, and the
   in-game test.
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

Every pull request carries a **session report**: run `tools/session_report.py` on your branch
(attach your `verify.py` results with `--verify` and the case directories with `--captures`) and
paste its output into the PR. It lists the functions you named, replaced and verified, the
verify results, and the findings and open questions from your commit messages (`Finding: ...`,
`Question: ...` lines). CI checks that it is there and that no attached result fails.

Every pull request that replaces code contains, in the description:

- the `tools/verify.py run` output for each function, on cases recorded in a real run and on
  edge cases ([docs/VERIFY.md](docs/VERIFY.md)),
- in-game test steps: what you did, where in the game, and what you saw, so a
  reviewer can repeat it on their own dump,
- the `symbols/functions.csv` and `game/hooks.csv` rows for the change.

A function is `replaced` once it is in the hook registry and builds. It becomes
`verified` only with passing `verify.py` output and the in-game test.

Investigation notes and logs go in the pull request, not in the repository. No screenshots or
clips of the game there either (see Rules).

## Your own copy

Everything here starts from your own console and your own copy of the game: you dump
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
- **No screenshots, clips or images of the game**, anywhere in this repository or in bbport:
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
  same as the community patches. Import the eboot with that base. bbport's guest offsets are
  these minus `0x400000` (QUIRKS.md, "Address conventions").
- `notes` in `functions.csv`: one line, 200 characters at most.

## Drafts

Replacements may start from a draft: the decompiler's output for the function, written by
`tools/draft.sh` from your own Ghidra project into a directory outside the repository. A draft
is a starting point, never a commit: what gets committed is the replacement after it is renamed
and restructured into readable code (names from `functions.csv`, no `FUN_`/`DAT_`/`local_`
names, no decompiler artefacts) and verified like any other. The decompiler is often wrong about
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

Allowed if disclosed and verified. Disclose with a `Co-Authored-By` trailer on
the commits or a note in the pull request description, and tick the box in the
template. AI-assisted functions are verified like any other: `verify.py` output
and the in-game test are mandatory, and the author answers for every line.
