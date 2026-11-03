<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Audio

The audio system covers sound playback, mixing and music. The game uses the FMOD sound engine; the first of its functions are mapped.

**At a glance**

- **Scope:** Sound playback, mixing, music.
- **Verified:** `audio_fmod_thread_affinity_lookup` (`0x0111a8c0`) is edge-verified against constructed cases.
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

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x0111a8c0` | 290 | `audio_fmod_thread_affinity_lookup` | edge-verified | FMOD thread-name affinity masks; eight 64-bit slots; nonblocking uses a 23-byte prefix; unknown names leave output untouched |

## Structs and globals

None documented yet. Record each as: name, address, size, layout, who reads and writes it.

## Patch points

None documented yet. Record each as: address, what the original does there, what the
replacement or a mod would change, and the risk.

## Open questions

- Everything: this system has not been studied yet.
