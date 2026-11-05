<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Render

The render system draws each frame and presents it to the screen. This page covers three mapped pieces: the per-frame render update, the pacing of each present, and one helper from the YEBIS post-processing middleware.

**At a glance**

- **Scope:** `render_frame_update_02377bd0`, `render_swap_chain_present` and `render_yebis_get_recursive_sample_parameters`.
- **Verified:** All three: the reference-counted object list, the vblank pacing of the present, and the blur sample counts.
- **Open:** What the listed objects and the two children are; most of draw submission, shaders and post-processing. The first command-buffer, present-record and swap-chain functions are mapped (edge-verified).

## Summary

### The per-frame update

`render_frame_update_02377bd0` (`0x02377bd0`) runs once a frame:

1. It walks a list of reference-counted objects and updates each one.
2. An object whose update says it is finished loses its reference (destroyed at the last one; an
   underflow is reported as "DLReferenceCountObject: Invalid Unref() call"), and its node is
   unlinked and freed.
3. It updates a `RendMan`-side child (`RendMan`: singleton `0x05940298`).
4. It steps another child by a frame time of 1/30 s, or 0 while that child is held.

### Pacing the present

`render_swap_chain_present` (`0x025b2fb0`, the graphics framework's `GXSwapChainCtrl`) paces
each present:

1. It waits until the requested number of 60 Hz vblanks has passed since the last present. It
   sleeps until 17 ms before that time, then polls the clock.
2. It maps the interval to a flip mode.
3. It presents.

In Iosefka's Clinic the game presents with interval 1 and does not wait here; the frame limiter
sets the pace.

### Blur sampling (YEBIS)

`render_yebis_get_recursive_sample_parameters` (`0x00fbc3e0`) belongs to YEBIS, the
post-processing middleware. Its `GPUTexUtil_GetRecursiveSampleParameters` works out:

- how many recursive passes a blur needs;
- how many samples the last one takes;
- how far apart the first one samples.

It carries the middleware's own assertions.

Scope still to map: draw submission, GNM command buffers, shaders, post-processing.

The game's shaders ship as compiled AMD GPU code in its shader bundles. A census tool in the code
repository decodes the shaders in your own copy and counts the instructions they use, for
Paleblood's shader translator (Paleblood #316). It prints counts only; no shader code leaves your
machine.

### Writing GPU commands and presenting

The engine builds the GPU's command buffers itself (see Runtime syscalls),
as AMD "PM4" packets: each packet is a header word naming the operation, followed by its
arguments. The first of the engine's own packet writers are mapped, all by [@evill33t](https://github.com/evill33t):

- **The command buffer.** One shared structure describes it: where it begins and ends, the
  cursor where the next word goes, and a callback the engine calls to get more room when it runs
  out. `render_command_prepare_submission` (`0x014784a0`) reserves 64 words before a submission,
  and `render_command_emit_counter_operation` (`0x01477b30`) writes a seven-word packet that makes
  the GPU wait on a value in memory (PM4 `WAIT_REG_MEM`) ([#208](https://github.com/Yharnam-Hunters/Paleblood/pull/208)).
- **The pool counter.** `render_read_present_pool_counter` (`0x0247eb80`) reads a 64-bit counter
  atomically, with a locked compare-and-exchange loop ([#279](https://github.com/Yharnam-Hunters/Paleblood/pull/279)).
- **Present records.** `render_present_record_consume` (`0x02ab5a70`) turns a pending present
  record into commands: it classifies the record, clamps its range, copies its payload and
  writes the final fields ([#286](https://github.com/Yharnam-Hunters/Paleblood/pull/286)).
- **A record test.** `render_record_auxiliary_predicate` (`0x02abb4f0`) answers yes or no for a
  record's selector: always yes for two selector values, a flag bit for the next two, no otherwise
  ([#293](https://github.com/Yharnam-Hunters/Paleblood/pull/293), from #266).
- **Presenting a swap-chain image.** `render_swap_chain_present_mode` (`0x02ad5dd0`) waits until
  the display has flipped far enough and picks the flip mode, and `render_swap_chain_submit`
  (`0x02ad6e50`) submits the frame, advances the frame counter and checks its owner again
  ([#287](https://github.com/Yharnam-Hunters/Paleblood/pull/287)). They use the public layouts of the video-out flip status and kernel events.
- **Depth and stencil clears.** `render_emit_present_preparation_scalar` (`0x01473590`) and
  `render_emit_present_preparation_byte` (`0x014735f0`) write the depth and stencil clear values
  (AMD's `DB_DEPTH_CLEAR` and `DB_STENCIL_CLEAR` registers) [#325](https://github.com/Yharnam-Hunters/Paleblood/pull/325), from #210.
- **The last record's depth settings.** `render_emit_present_last_record_packet` (`0x014769e0`)
  writes `DB_RENDER_CONTROL`. Two setters turn its depth and stencil compression off or on
  (`0x01485070`, `0x01485080`), one replaces two packed indices (`0x014721d0`), and
  `render_reset_present_preparation_dirty` (`0x02ad2c30`) writes only the clear values that changed
  and remembers them [#326](https://github.com/Yharnam-Hunters/Paleblood/pull/326), from #211.
- **Resource records.** A record describes a GPU resource's layout. The reviews tie its fields to
  AMD's public tiling and format fields, though the full meaning is still open. The mapped parts:
  - **mapping:** `render_pack_record_mapping_word` (`0x01471310`) packs its mapping word and
    `render_read_record_mapping_word` (`0x01486cf0`) reads one back [#321](https://github.com/Yharnam-Hunters/Paleblood/pull/321), from #212;
  - **encoding:** three lookups decode the encoding byte's metadata, component arrangement and
    category (`0x01471730`, `0x014717a0`, `0x014719a0`), and `render_record_set_encoding`
    (`0x01486540`) stores it [#327](https://github.com/Yharnam-Hunters/Paleblood/pull/327), from #261;
  - **parameters:** `render_record_apply_parameter_tuple` (`0x0149c850`) picks a record parameter
    from a normalisation table, with its classification helpers (`0x0149e900`–`0x0149ea70`) [#322](https://github.com/Yharnam-Hunters/Paleblood/pull/322), from #223;
  - **sizes:** `render_calculate_second_auxiliary` (`0x0149e570`) works out the size and alignment
    of a second auxiliary area [#323](https://github.com/Yharnam-Hunters/Paleblood/pull/323), from #228;
  - **fields:** seven setters (`0x01486520`–`0x01486800`) replace one field each, keeping the
    rest of the word [#324](https://github.com/Yharnam-Hunters/Paleblood/pull/324), from #236.

<details>
<summary>How these were verified</summary>

Each was checked against the original instructions on constructed edge cases by an independent
review, which also caught deliberately broken variants; none was checked on recorded game
frames. #279, #286 and #287 were brought up to date with `main` from #209, #259 and #267, and
#321 to #327 from #212, #223, #228, #236, #210, #211 and #261.

</details>

## Key functions

| Address | Size | Name | Status | Notes |
|---|---:|---|---|---|
| `0x00fbc3e0` | 670 | `render_yebis_get_recursive_sample_parameters` | verified | YEBIS GPUTexUtil_GetRecursiveSampleParameters: blur pass count, last-pass samples and scale; the FPS patches drop its first assertion |
| `0x01471310` | 544 | `render_pack_record_mapping_word` | edge-verified | Pack numeric mapping fields with priority masks and full-width mode handling; return a zero-extended DWORD. |
| `0x01471730` | 21 | `render_record_encoding_metadata` | edge-verified | Bounded first-byte metadata lookup; preserve original table entries and zero fallback. |
| `0x014717a0` | 505 | `render_record_encoding_components` | edge-verified | Select ordered packed component arrangement; optional output only on success and Boolean AL contract. |
| `0x014719a0` | 117 | `render_record_encoding_category` | edge-verified | Decode accepted category nibbles; optional DWORD output untouched on rejection. |
| `0x014721d0` | 31 | `render_set_present_last_record_indices` | edge-verified | Replace two packed indices while preserving the remaining bits. Return the old preserved bits. |
| `0x01473590` | 95 | `render_emit_present_preparation_scalar` | edge-verified | Write the captured float bits to DB_DEPTH_CLEAR. Admission tests the callback low byte; rejection returns its full 64-bit result. |
| `0x014735f0` | 89 | `render_emit_present_preparation_byte` | edge-verified | Write the captured input low byte to DB_STENCIL_CLEAR. Admission tests the callback low byte; rejection returns its full 64-bit result. |
| `0x014769e0` | 93 | `render_emit_present_last_record_packet` | edge-verified | Write DB_RENDER_CONTROL for the last-record path. Publish and return the captured cursor plus twelve bytes; return the full callback result on rejection. |
| `0x01477b30` | 172 | `render_command_emit_counter_operation` | edge-verified | Seven-word counter packet; callback low byte gates writes; label range selects masked encoding Synthetic edges pass; no recordings. |
| `0x014784a0` | 82 | `render_command_prepare_submission` | edge-verified | Reserve 64 command words; callback low byte gates two marker stores and cursor advance Synthetic edges pass; no recordings. |
| `0x01485070` | 16 | `render_set_present_last_record_depth_compress_disable` | edge-verified | Replace DEPTH_COMPRESS_DISABLE in the saved DB_RENDER_CONTROL word using the input low bit. Return the old bits with that flag cleared. |
| `0x01485080` | 16 | `render_set_present_last_record_stencil_compress_disable` | edge-verified | Replace STENCIL_COMPRESS_DISABLE in the saved DB_RENDER_CONTROL word using the input low bit. Return the old bits with that flag cleared. |
| `0x01486520` | 31 | `render_record_set_word_0c_fields` | edge-verified | Store two masked fields; return masked old word. No constructor add/subtract. |
| `0x01486540` | 305 | `render_record_set_encoding` | edge-verified | Pack accepted encoding into record DWORD10 preserving other bits; invalid input has undefined native extractor locals. |
| `0x014866e0` | 33 | `render_record_set_word_04_repeated_field` | edge-verified | Duplicate supplied low 11 bits; return preserved old word. |
| `0x01486710` | 20 | `render_record_set_word_08_low22` | edge-verified | Replace this word's low 22 bits and return the preserved old upper bits. |
| `0x01486760` | 20 | `render_record_set_word_14_second_low5_field` | edge-verified | Replace the second five-bit field and return the preserved old word bits. |
| `0x014867c0` | 21 | `render_record_set_first_auxiliary_flag` | edge-verified | Clear only bit 13 then OR full input byte shifted13; return preserved old word. |
| `0x014867e0` | 21 | `render_record_set_second_auxiliary_flag` | edge-verified | Clear only bit 14 then OR full input byte shifted14; return preserved old word. |
| `0x01486800` | 20 | `render_record_set_word_28_low22` | edge-verified | Replace this word's low 22 bits and return the preserved old upper bits. |
| `0x01486cf0` | 38 | `render_read_record_mapping_word` | edge-verified | Read one unaligned mapping DWORD and delegate three extracted fields to the original encoder. Full return is zero-extended. |
| `0x0149c850` | 672 | `render_record_apply_parameter_tuple` | edge-verified | Select one output code from normalization tuple; ordered overrides and rejection preserve output. |
| `0x0149e570` | 279 | `render_calculate_second_auxiliary` | edge-verified | Calculate the second auxiliary layout through two ordered unit admissions and wrapped alignment. Publish its four optional size and alignment outputs in 8/4/4/4-byte order. |
| `0x0149e900` | 16 | `render_record_tuple_parameter_supported` | edge-verified | Nonzero power-of-two test with Boolean AL interface. |
| `0x0149e960` | 129 | `render_record_tuple_transform_classification` | edge-verified | Transform admitted classifications after wrapping 32-bit scaling and unsigned thresholds. |
| `0x0149ea20` | 28 | `render_record_tuple_classification_group` | edge-verified | Bounded classification-to-group lookup with fallback one and original shared table. |
| `0x0149ea70` | 25 | `render_record_tuple_alternate_classification` | edge-verified | Recognize bounded alternate-classification subset with zero fallback. |
| `0x02377bd0` | 399 | `render_frame_update_02377bd0` | verified | per-frame: ages a list of ref-counted objects, updates RendMan-side child, steps another child by 1/30 s (Uncap: measured frame time) |
| `0x0247eb80` | 16 | `render_read_present_pool_counter` | edge-verified | full-QWORD locked read/writeback retry, XOR before each attempt; caller adds one. Real AMD64 write cycles retained; non-AMD64 bridge unresolved Synthetic edges pass; no recordings. |
| `0x025b2fb0` | 768 | `render_swap_chain_present` | verified | GXSwapChainCtrl present: waits for the present interval in 60 Hz vblanks, maps it to a flip mode, presents (Uncap: mode 0) |
| `0x02ab5a70` | 1235 | `render_present_record_consume` | edge-verified | Track resource range or emit record commands; preserve cached tokens, index wrap and final state resets. Static reconstruction Synthetic GCC/Clang edges pass; no recordings. |
| `0x02abb4f0` | 31 | `render_record_auxiliary_predicate` | edge-verified | Return the auxiliary-processing predicate: selectors 0x13/0x14 enable it, 0x15/0x16 use flag bit 6, and all other selectors disable it. |
| `0x02ad2c30` | 120 | `render_reset_present_preparation_dirty` | edge-verified | Emit changed depth and stencil clear values, then cache their post-call values. Preserve the second writer's upper return bits while replacing its low word. |
| `0x02ad5dd0` | 27 | `render_swap_chain_present_mode` | edge-verified | Validates low DWORD mode; retains object, loads full source pointer at +0x33d0, forwards mode to lower submit Synthetic GCC/Clang edges pass; no recordings. |
| `0x02ad6e50` | 499 | `render_swap_chain_submit` | edge-verified | Owned-thread flip polling, source binding and submission; every exit reloads the global guard pointer. Native GPU helper effects remain original Synthetic GCC/Clang edges pass; no recordings. |

## Structs and globals

**The update's owner**

| Offset | Field |
|---|---|
| `+0x10` | list sentinel (next `+0x0`, prev `+0x8`, object `+0x10`) |
| `+0x18` | count |
| `+0x20` | allocator (frees nodes through vtable slot `+0x70`) |
| `+0x70` | child, stepped through vtable slot `+0x40` |
| `+0x80` | child |
| `+0x8c` | the held flag |
| `+0x98` | a byte copied into the `+0x70` child each frame |

**Reference-counted objects**: vtable slot 0 destroys, count at `+0x8` (atomic). The engine shares this
reference counting across systems since [#185](https://github.com/Yharnam-Hunters/Paleblood/pull/185);
the frame update was re-verified unchanged on all its recorded and edge cases.

## Patch points

The frame-rate patches are described in [Frame timing](frame_timing.md).

- `Uncap FPS++` makes the `+0x70` child's step the flipper's measured frame time (`+0x264`)
  whatever the held flag says, through a small code cave inside the function, and drops the
  `RendMan` check. The 1/30 s it reads otherwise is the shared constant at `0x04d29170`, which
  the 60, 90 and uncapped patches rewrite in the data segment.
- `Uncap FPS++` presents with flip mode 0 (no vblank sync) at the end of `0x025b2fb0`.
- Every frame-rate patch removes the first assertion of `0x00fbc3e0` (at least 3 base samples):
  at higher frame rates the blur gets fewer samples.

## Open questions

- What the reference-counted objects in the per-frame list are, and what the two children are.
- Draw submission, GNM command buffers, shaders and post-processing have not been studied yet.
