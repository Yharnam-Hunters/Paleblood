# LibAtrac9

An ATRAC9 audio decoder in C by Alex Barney, MIT licensed (`LICENSE`).

- Upstream: <https://github.com/Thealexbarney/LibAtrac9>, directory `C/src`
- Commit: `efca2e3af35562a09a9bb6deed90e45b4b824dc4` (2025-03-14)
- Unmodified: `src/` is that commit's `C/src`, file for file.

The runtime's audio job manager (`runtime/syslib/audio_ajm.c`) decodes the title's ATRAC9 audio
with it. It is built as the static library `atrac9` (`CMakeLists.txt`) with its own warnings off,
as code we do not maintain.

To update: replace `src/` with a newer upstream `C/src`, update the commit above, and run the
audio job manager tests (`test_syslib`).
