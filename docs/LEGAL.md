# Legal

- **No game files, ever.** This repository contains no code, assets, binaries, decompiler output
  or extracted data from the game, and never will. What it holds is newly written source, tools
  and documentation. `tools/check_no_game_data.py` runs in the pre-commit hook and in CI and
  rejects anything that looks like game data.
- **Your own console, your own copy.** To build, run or verify anything you need Bloodborne
  (CUSA03173, EU) with the 1.09 update, dumped from your own PS4. The project does not provide
  or point to game files, pkgs, firmware, keys or decryption tools, and requests for or links to
  them are removed ([CONTRIBUTING.md](../CONTRIBUTING.md), "Your own copy").
- **Images.** No extracted game images or assets. The images here are our own work: recordings of
  our tools, and frames drawn by Paleblood's own renderer, at full resolution, added by the
  maintainer with attribution (such as the Iosefka's Clinic frame in the README), and listed in
  [`docs/assets/ALLOWLIST`](assets/ALLOWLIST). The checks refuse anything else.
- **Not affiliated.** This is a fan project. It is not affiliated with, endorsed by or sponsored
  by Sony Interactive Entertainment or FromSoftware. Bloodborne and related names are trademarks
  of their respective owners; the frames shown here are theirs, shown for technical commentary.

## License

GPL-2.0-or-later ([LICENSE](../LICENSE)). LibAtrac9 is MIT; the external run scaffold has its own
GPL-2.0-or-later license and attribution notice in `third_party/README.md`. Neither grants any
right in the game or its code.
