### Runtime-only report
<!-- CI selects the runtime-only report from the changed file paths. Labels and this text do not
     change the selected path. Runtime-only means every changed file is in the runtime scope. -->
<!-- Use this block only when all changed files are runtime-only. Keep only the report block
     selected by changed files. Runtime PRs do not need a proprietary dump, captures, or
     function-level verify.py. -->

Calls or tightly related group:

Public sources and independent evidence:

Confirmed behavior:

Unresolved behavior:

Tests run (CI includes the RT_SYSLIB unit test):

Clean-room basis: <!-- cite public docs, independent reverse engineering, or observed behavior -->

Maintainer boot verification: pending after merge.
AI-assisted commits: include a `Co-Authored-By` trailer naming the actual tool used; a PR note
does not replace it.

<!-- runtime-report:start -->
<!-- runtime-report-json {"schema":1,"track":"runtime","calls":["replace with call name(s)"],"sources":["replace with public source or repository evidence"],"confirmed":["replace with confirmed behavior"],"unresolved":[],"tests":["replace with command and result; CI also runs RT_SYSLIB"],"clean_room":true,"no_emulator_code":true,"clean_room_basis":"replace with the independent evidence used; no external runtime code copied or modeled","game_data":false,"function_verify":"not-required","maintainer_boot":"pending-after-merge"} -->
<!-- runtime-report:end -->

### Documentation-only report
<!-- CI selects this report only when every changed file is a Markdown document under docs/ or
     at the repository root. Runtime documentation with no runtime code change uses this report. -->

Pages changed:

Summary:

<!-- documentation-report:start -->
<!-- documentation-report-json {"schema":1,"track":"documentation","pages":["replace with docs/page.md"],"summary":"replace with the documentation change","implementation_changes":false} -->
<!-- documentation-report:end -->

### Decompilation session report
<!-- Required. Run `tools/session_report.py --verify <verify.py result>.json --captures <case dirs>`
     on your branch and paste its whole output here (it is checked by CI). Put findings and open
     questions in your commit messages as "Finding: ..." / "Question: ..." lines first. -->

### What
<!-- Functions replaced or documented, by name and address. One function or cluster per PR. -->

### verify.py output
<!-- Paste the output for each function. "Not run" blocks status `verified`, not `replaced`. -->

### Mutation test
<!-- The deliberately wrong versions you tried and how each failed. -->

### Checklist
- [ ] Based on current `main`; no other open PR touches the same functions
- [ ] One function or cluster
- [ ] `symbols/functions.csv` and `game/hooks.csv` rows included; `tools/progress.py --update-readme` run
- [ ] No game files, assets, binaries, decompiler projects or raw decompiler dumps; no screenshots or clips of the game; no links to game files (CONTRIBUTING.md, "Your own copy")
- [ ] No `FUN_` / `DAT_` / `undefined8` / `local_` names left in the code
- [ ] Readable ([STYLE.md](../STYLE.md)): `tools/check_readable.py` passes; no transcription (addresses, raw offsets, magic numbers, `_mm_` outside `game/engine/`, recording code) goes to `main`; `tools/readable_allowlist.txt` only shrinks
- [ ] Independent review: <!-- who reviewed verification and readability (not the author), and the symbols/reviews.csv row; required before `verified` -->
- [ ] `runtime/` stays generic
- [ ] AI-assisted: <!-- yes / no. Every AI-assisted commit needs a Co-Authored-By trailer naming the actual tool; for Codex: Co-Authored-By: Codex <noreply@openai.com> -->
- [ ] Depends on: <!-- #PR, or none -->
