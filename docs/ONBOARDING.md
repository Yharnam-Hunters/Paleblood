# Onboarding: from your dump to your first pull request

Target: under an hour on a recent desktop. Every command below was run on Linux by the
maintainer; measured times are from that machine. You need your own legally obtained
Bloodborne pkgs: the base game and the 1.09 update for CUSA03173 (EU). Nobody in the
project will send you game files, and nothing from your dump ever goes into a commit, an
issue or a pull request.

## 0. Before you start (once)

- Access: the maintainer adds you to the organization (see "Joining" in CONTRIBUTING.md).
- Tools: `git`, `gh` (logged in), `python3`, `rsync`, `g++`, `curl`, `unzip`, a JDK 21 or newer,
  and Rust's `cargo`.
- About 70 GB of free disk for the extracted game, the converted executables and Ghidra.

```
cargo install orbis-pkg-util --version 0.1.0 --locked     # 12 s
export PATH=$HOME/.cargo/bin:$PATH
```

## 1. Fork and clone (2 minutes)

Fork `Yharnam-Hunters/Paleblood` on GitHub (your fork stays private), then:

```
gh repo clone <you>/Paleblood
cd Paleblood
gh repo set-default Yharnam-Hunters/Paleblood
tools/install_hooks.sh
```

The hooks refuse game data, `CLAUDE.md` and stale progress numbers before they reach a commit.

## 2. Prepare your dump (10 minutes)

Pick a directory **outside** the repository (the script refuses one inside it):

```
tools/prepare_dump.sh /path/to/CUSA03173_base.pkg /path/to/CUSA03173_update_1.09.pkg ~/bb   # 570 s
```

It extracts both pkgs (they are only read), copies the update over the base, builds
SelfUtil-Patched at a pinned commit (it has no license, so it is built locally and never
shipped), converts `eboot.bin` and the seven `sce_module` PRX files to ELF, and checks them:

```
ok       eboot.elf
...
ok       eboot.elf (loaded image)
```

Every line must say `ok`. A mismatch means a different region, version or dump: stop, it will
not line up with anyone else's addresses. `tools/check_target.py ~/bb/elf` repeats the check.

## 3. Ghidra (15 minutes, most of it the download)

Ghidra 12.0.3 and the GhidraOrbis loader built for it (details in [GHIDRA.md](GHIDRA.md)):

```
mkdir -p ~/bb/tools && cd ~/bb/tools
curl -fLO https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.0.3_build/ghidra_12.0.3_PUBLIC_20260210.zip
echo "90d3fffb20b00030dcef8d2a24dd0f422d3a61e432b3ad43f77233ac6d667981  ghidra_12.0.3_PUBLIC_20260210.zip" | sha256sum -c
unzip -q ghidra_12.0.3_PUBLIC_20260210.zip
curl -fLo GhidraOrbis.zip https://github.com/astrelsky/GhidraOrbis/releases/download/1.0144/ghidra_12.0.3_PUBLIC_20260605_GhidraOrbis.zip
echo "86bc09ecffe94dc731edf96abfc7211e8950ba2664f45883c12e83d89f850d46  GhidraOrbis.zip" | sha256sum -c
unzip -q GhidraOrbis.zip -d ghidra_12.0.3_PUBLIC/Ghidra/Extensions/
sed -i 's/^MAXMEM=2G/MAXMEM=14G/' ghidra_12.0.3_PUBLIC/support/analyzeHeadless   # tested value; less may do
```

Import your eboot **without** analysis and create the shared functions and names from the
repository. This takes minutes instead of the half hour of a full analysis, and gives you
exactly the function starts everyone else has:

```
G=~/bb/tools/ghidra_12.0.3_PUBLIC
REPO=/path/to/Paleblood
mkdir -p ~/bb/ghidra
$G/support/analyzeHeadless ~/bb/ghidra bb -import ~/bb/elf/eboot.elf -noanalysis \
    -loader-imageBase 0x400000 \
    -scriptPath $REPO/tools/ghidra -postScript ImportNames.java $REPO --create      # 155 s
```

It prints `CREATE created=... failed=0` and `NAMES named=...`. The image base must be
`0x400000`: every address in the project is a PS4 virtual address (QUIRKS.md).

Then open the project in the Ghidra window (`$G/ghidraRun`, File, Open Project, `~/bb/ghidra/bb`).
The decompiler works per function on demand. If you want the full analysis as well, run
Analysis, Auto Analyze; it changes nothing that others depend on.

Whenever `symbols/functions.csv` changes upstream, pull and run `ImportNames.java` again
(Script Manager in the window, or the headless line above without `-import ... -noanalysis
-loader-imageBase 0x400000` and with `-process eboot.elf`). Your own names are kept unless the
repository names the same function differently; the old name goes into the plate comment.

## 4. Your first pull request (20 minutes)

Good first issues are small, self-contained functions to identify and name, and system pages
to document. Replacing code comes later, once the bbport-based test harness exists.

1. Pick an issue labelled `good first issue` and comment that you take it.
2. Look at the function in Ghidra. Work out what it does from its callers, callees and the
   library calls it makes. Rename it in Ghidra to `<system>_<what_it_does>`, snake_case, for
   example `input_read_pad_state` (naming rules in CONTRIBUTING.md).
3. Turn your names into rows and merge them:

   ```
   $G/support/analyzeHeadless ~/bb/ghidra bb -process eboot.elf -noanalysis \
       -scriptPath tools/ghidra -postScript ExportNames.java "$PWD" /tmp/rows.csv
   python3 tools/merge_functions.py /tmp/rows.csv
   ```

   Close the project in the Ghidra window first (a project open in the window is locked for
   headless runs), or run `ExportNames.java` from the Script Manager instead.
   `ExportNames.java` writes only address, size and name. `merge_functions.py` refuses a name
   that conflicts with the repository and validates the result.
4. Add a one-line note to your row in `symbols/functions.csv` if it helps (200 characters at
   most), and describe the function in the issue or the pull request: what it does, how you
   know (callers, library calls), what is still unknown. No decompiler output, no bytes.
5. Commit and open the pull request:

   ```
   git switch -c name-<address>
   git add symbols/functions.csv
   git commit -m "input: name 0x0145dcb0 input_read_pad_state" -m "Closes #N"
   git push -u origin HEAD
   gh pr create --fill
   ```

The pull request template asks for the rest. CI checks the CSV, the progress numbers and that
no game data is included. If you used an AI assistant, say so in the template.

## What was not tested by the maintainer

- The Ghidra window (`ghidraRun`) and running the scripts from the Script Manager: the
  commands above were run headless. The scripts ask for their arguments when started from the
  window.
- Forking and opening a pull request from a fork: the maintainer owns the organization.
- macOS and Windows. The tools are bash and Python; on Windows use WSL.
