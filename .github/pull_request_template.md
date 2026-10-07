### Session report
<!-- Required. Run `tools/session_report.py --verify <verify.py result>.json --captures <case dirs>`
     on your branch and paste its whole output here (it is checked by CI). Put findings and open
     questions in your commit messages as "Finding: ..." / "Question: ..." lines first. -->

### What
<!-- Functions replaced or documented, by name and address. One function or cluster per PR. -->

### verify.py output
<!-- Paste the output for each function. "Not run" blocks status `verified`, not `replaced`. -->

### In-game test
<!-- Steps: where in the game, what you did, what you saw. A reviewer must be able to repeat it. -->

### Checklist
- [ ] Based on current `main`; no other open PR touches the same functions
- [ ] One function or cluster
- [ ] `symbols/functions.csv` and `game/hooks.csv` rows included; `tools/progress.py --update-readme` run
- [ ] No game files, assets, binaries, decompiler projects or raw decompiler dumps; no screenshots or clips of the game; no links to game files (CONTRIBUTING.md, "Your own copy")
- [ ] No `FUN_` / `DAT_` / `undefined8` / `local_` names left in the code
- [ ] Readable ([STYLE.md](../STYLE.md)): `tools/check_readable.py` passes; no transcription (addresses, raw offsets, magic numbers, `_mm_` outside `game/engine/`, recording code) goes to `main`; `tools/readable_allowlist.txt` only shrinks
- [ ] Independent review: <!-- who reviewed verification and readability (not the author), and the symbols/reviews.csv row; required before `verified` -->
- [ ] `runtime/` stays generic
- [ ] AI-assisted: <!-- yes / no. If yes: Co-Authored-By trailer on the commits or a note here, verified with verify.py and the in-game test -->
- [ ] Depends on: <!-- #PR, or none -->
