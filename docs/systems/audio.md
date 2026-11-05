<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Audio

The audio system covers sound playback, mixing and music. The game uses the FMOD sound engine; the first of its functions are mapped.

**At a glance**

- **Scope:** Sound playback, mixing, music.
- **Verified:** Three FMOD functions are edge-verified against constructed cases: the thread affinity lookup (`0x0111a8c0`) and two output callbacks (`0x0111b060`, `0x0111b0b0`).
- **Open:** Playback, mixing and music themselves.

## Summary

### FMOD's threads

The game plays sound through FMOD, a commercial sound engine. FMOD runs its own threads (mixer,
streaming, loading and others), and before it starts one it asks the game which CPU cores that
thread may use. `audio_fmod_thread_affinity_lookup` (`0x0111a8c0`) answers: it compares the
thread's name against eight known FMOD thread names in a fixed order (the non-blocking one by
its first 23 characters only) and copies the matching slot's settings; for any other name it
changes nothing and returns 0.

<details>
<summary>How the lookup was verified</summary>

By [@evill33t](https://github.com/evill33t) in #106, brought up to date with `main` and merged
in [#272](https://github.com/Yharnam-Hunters/Paleblood/pull/272): its 21 constructed edge cases
pass against the original on current `main`, and an independent review approved it. Several
neighbouring helpers in the same address range (string comparisons, the recursive mutex and
semaphore wrappers) are FMOD's too; they are described on the [Engine](engine.md) and
[Kernel](kernel.md) pages.

</details>

### FMOD's output device

FMOD learns about the PS4's audio output through a table of callbacks the game installs (at
`0x0111aed0`). Two of them are mapped and replaced:

- `audio_fmod_output_driver_count` (`0x0111b060`) reports one output driver.
- `audio_fmod_output_defaults` (`0x0111b0b0`) fills in the output's defaults, each only when FMOD
  asked for it: a setting word, then what looks like a 48 kHz sample rate and FMOD's 5.1 speaker
  mode. Those two meanings are hypotheses from the values, not yet confirmed from FMOD's use of
  them.

<details>
<summary>How the callbacks were verified</summary>

By [@evill33t](https://github.com/evill33t) in [#114](https://github.com/Yharnam-Hunters/Paleblood/pull/114): both match the original on
constructed edge cases (every combination of present and missing outputs), and an independent
review approved it. They are edge-verified, not yet verified against recorded game runs.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a8c0` | 290 | `audio_fmod_thread_affinity_lookup` | edge-verified | FMOD thread-name affinity masks; eight 64-bit slots; nonblocking uses a 23-byte prefix; unknown names leave output untouched |
| `0x0111b060` | 14 | `audio_fmod_output_driver_count` | edge-verified | FMOD Orbis AudioOut descriptor callback installed by 0x0111aed0; optional output receives one driver; return zero |
| `0x0111b0b0` | 37 | `audio_fmod_output_defaults` | edge-verified | FMOD output descriptor callback writes 132 then 48000 then 6 to optional outputs; sample-rate and speaker-mode meanings remain hypotheses |
| `0x011c4810` | 34 | `audio_fmod_string_copy` | edge-verified | NUL-terminated string copy used by FMOD callers; includes terminator and returns destination; finite aliases covered |
| `0x011c4840` | 38 | `audio_fmod_string_copy_counted` | edge-verified | FMOD bounded string copy uses low32 count; stops at NUL or count without padding; zero count accesses neither pointer |

## Structs and globals

None documented yet. Record each as: name, address, size, layout, who reads and writes it.

## Patch points

None documented yet. Record each as: address, what the original does there, what the
replacement or a mod would change, and the risk.

## Open questions

- Everything: this system has not been studied yet.
