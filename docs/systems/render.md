<!-- Generated from the project documentation. Do not edit here: open an issue or a pull request. -->

# Render

The render system draws each frame and presents it to the screen. This page covers the per-frame render update, the pacing of each present, one helper from the YEBIS post-processing middleware, and the GPU helper library compiled into the game: its command writers, state setters, swap chain and shader uploads, and how they now report to Paleblood's renderer.

**At a glance**

- **Scope:** `render_frame_update_02377bd0`, `render_swap_chain_present`, `render_yebis_get_recursive_sample_parameters`, and the GPU helper library's command writers, state setters, swap chain and shader uploads.
- **Verified:** All three: the reference-counted object list, the vblank pacing of the present, and the blur sample counts.
- **Open:** What the listed objects and the two children are; the engine's own drawing code above the GPU library, materials and post-processing. Many GPU library functions are mapped and edge-verified; they now tell Paleblood's renderer what they set.

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

Scope still to map: the engine's draw submission above the GPU library, materials and
post-processing.

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
- **Building present records.** `render_build_present_source_record` (`0x0256e1d0`) builds the
  record for a present, by mode, keeping partial output, unsigned divisions and the packed
  auxiliary requirements as the original does; `render_choose_record_constructor_selector`
  (`0x02ab7a20`), `render_combine_record_parameters` (`0x02ab7ab0`) and
  `render_reduce_record_parameter` (`0x02ab7e40`, a four-entry scale table, zero past its end)
  pick and combine its constructor choices ([#473](https://github.com/Yharnam-Hunters/Paleblood/pull/473), from #268; edge-verified on synthetic cases).
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
    rest of the word [#324](https://github.com/Yharnam-Hunters/Paleblood/pull/324), from #236;
  - **construction:** `render_record_constructor` (`0x01485f90`) clears a record's 44-byte prefix
    and fills in the record and its auxiliary outputs, returning a packed 64-bit result;
    `render_record_constructor_pack_pair` (`0x01471600`) packs a selector pair into an encoding,
    and two optional steps (`0x0149e2d0`, `0x0149e400`) align paired components and work out the
    geometry, writing their outputs even when a helper reports an error [#478](https://github.com/Yharnam-Hunters/Paleblood/pull/478), from #246 (edge-verified on synthetic cases);
  - **normalisation:** `render_record_normalize_mode2`, `…mode3` and `…mode4` (`0x025710a0`,
    `0x025711d0`, `0x02571340`) bring a record's input into one normalised form, copying the words
    they do not interpret unchanged and keeping each helper's writes and failure codes in order, and
    `render_record_translate_selector` (`0x02ab7310`) maps a record selector to one output word,
    answering status 12 for a selector it does not support [#249](https://github.com/Yharnam-Hunters/Paleblood/pull/249) (edge-verified on synthetic cases).

### Setting GPU state and drawing

The GPU helper library keeps no state of its own: each setter writes its value straight into the
command buffer as a register packet, and the GPU reads it from there. A large group of setters and
draw writers was mapped and replaced by [@evill33t](https://github.com/evill33t), each verified to
write exactly the original packets:

- **Blending and colour output** [#411](https://github.com/Yharnam-Hunters/Paleblood/pull/411) (from #233): `render_set_cb_blend_control`
  (`0x01473090`, one colour buffer's blend equation, `CB_BLENDn_CONTROL`),
  `render_set_cb_color_control` (`0x01473320`, the colour output mode and raster operation,
  `CB_COLOR_CONTROL`) and `render_set_cb_target_mask` (`0x01472fd0`, which colour components each
  target writes, `CB_TARGET_MASK`).
- **Depth and stencil** [#411](https://github.com/Yharnam-Hunters/Paleblood/pull/411): `render_set_db_depth_control` (`0x014733a0`, the depth and
  stencil tests and writes) and `render_set_db_stencil_control` (`0x01473400`, the front- and
  back-face stencil operations).
- **What is drawn** [#411](https://github.com/Yharnam-Hunters/Paleblood/pull/411): `render_set_vgt_primitive_type` (`0x01473fe0`, triangles,
  strips, rectangles and so on), `render_emit_num_instances` (`0x01475370`, how many copies a draw
  makes), `render_emit_draw_index_auto` (`0x014741b0`, a draw of consecutive vertices) and
  `render_emit_event_write` (`0x01475da0`, a GPU event such as a cache flush), with two record
  helpers (`0x01485390`, `0x014853f0`) and the one call that asks the system whether the GPU's
  performance-analysis mode is on (`0x01489c40`).
- **Where on screen** [#364](https://github.com/Yharnam-Hunters/Paleblood/pull/364) (from #232): `render_configure_viewport` (`0x014765e0`, the
  viewport's scale, offset and depth range), `render_configure_screen_scissor` (`0x01476790`),
  `render_set_screen_offset` (`0x01473650`), the guard-band clip and discard adjustments
  (`0x014736d0`, `0x01473750`) and the viewport-transform controls (`0x01476980`, `0x01485220`).
- **A present's context section** [#365](https://github.com/Yharnam-Hunters/Paleblood/pull/365) (from #231): `render_emit_present_context_section`
  (`0x01472870`) reserves room and writes the context registers a present needs.

### Command buffers, swap chain and shader uploads

- **Command buffers** [#360](https://github.com/Yharnam-Hunters/Paleblood/pull/360) (from #256): `render_command_buffer_init` (`0x01471270`) sets up
  a command buffer's pointers and its "need more room" callback. `render_command_submit_batch`
  (`0x02ab0ce0`), `render_command_submit_snapshot` (`0x02ab0e30`) and `render_command_submit_present`
  (`0x02aafb30`) hand finished regions to the system's submit call, with the waits the GPU needs.
  The room check that every packet writer makes before writing is now shared code [#291](https://github.com/Yharnam-Hunters/Paleblood/pull/291).
- **The swap chain** [#361](https://github.com/Yharnam-Hunters/Paleblood/pull/361) (from #254): `render_setup_swap_chain` (`0x02ad5e90`) allocates
  and registers the swap chain's frame buffers with video out and sets up its cached resource data
  and tone curves; two cleanups release the curve and the cached record (`0x02571c60`,
  `0x02572290`, [#359](https://github.com/Yharnam-Hunters/Paleblood/pull/359), from #244).
- **The last present record** [#354](https://github.com/Yharnam-Hunters/Paleblood/pull/354) (from #260): `render_present_last_record_consume`
  (`0x02ab6810`) consumes the final record of a present, or only updates its tracking when the
  commands do not fit.
- **Shaders** [#263](https://github.com/Yharnam-Hunters/Paleblood/pull/263), [#363](https://github.com/Yharnam-Hunters/Paleblood/pull/363) (from #251): `render_flush_shader_preparation`
  (`0x02ab9850`) flushes a pending barrier before a shader event, and `render_allocate_shader_upload`
  (`0x02aaddc0`) finds room of at most 64 KiB for a shader's code, reclaiming once if none is free.
- **Resource descriptors** [#357](https://github.com/Yharnam-Hunters/Paleblood/pull/357) (from #255), [#358](https://github.com/Yharnam-Hunters/Paleblood/pull/358) (from #222): the records that
  describe a texture's layout, its mip levels and their offsets (`0x014a1e80`, `0x014a2c40`), its
  encoding (`0x014712d0`, `0x01471770`, `0x01489910`-`0x01489960`) and its tiling
  (`0x0149cc60`, which converts a tile-mode table index), and how its parameters are derived
  (`0x02abaf10`, `0x0149cb30`).

### Render targets, descriptors and the present path (10 November)

Forty-five more of the library's functions were replaced on 10 November, all
[@evill33t](https://github.com/evill33t)'s work, brought up to date with `main` and merged as
[#482](https://github.com/Yharnam-Hunters/Paleblood/pull/482), [#485](https://github.com/Yharnam-Hunters/Paleblood/pull/485), [#486](https://github.com/Yharnam-Hunters/Paleblood/pull/486), [#487](https://github.com/Yharnam-Hunters/Paleblood/pull/487) and the batch [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489):

- **Render targets** [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489): `render_set_render_target` (`0x014738b0`) writes a
  colour target's register block, or switches the target off when there is no descriptor;
  `render_set_depth_render_target` (`0x014739b0`) does the same for the depth target.
  `render_restore_color_target_from_bank` and `render_restore_depth_target_from_bank`
  (`0x02ad1e30`, `0x02ad1e70`) put a saved target back from one of two banks, and
  `render_present_slot_emit` (`0x01473840`) writes a colour target's two clear words.
- **Rasterizer state** [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489): `render_configure_viewport_and_guard_band` (`0x01496f60`) sets the
  viewport and the guard band, the region outside the screen where triangles are kept rather than
  clipped; `render_set_pa_sc_mode_cntl_0` (`0x01477150`) switches multisampling and the viewport
  scissor.
- **Linking shaders** [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489): `render_build_shader_link_words` (`0x014873a0`) matches a vertex
  shader's outputs to a pixel shader's inputs and builds the words that connect them;
  `render_emit_shader_link_words` (`0x01475000`) writes them. `render_configure_record_shader`,
  its secondary path and `render_finish_record_shader` (`0x02ace1e0`, `0x02ace5e0`, `0x02acfc20`)
  set up and finish a recorded shader's state.
- **Descriptor geometry** [#482](https://github.com/Yharnam-Hunters/Paleblood/pull/482), [#485](https://github.com/Yharnam-Hunters/Paleblood/pull/485), [#486](https://github.com/Yharnam-Hunters/Paleblood/pull/486), [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489) (from #237, #247, #238): building a
  resource descriptor and the requirements it adds up (`render_prepare_resource_descriptor`,
  `0x02fa6af0`), the two- and three-part record descriptors and their size
  (`0x01487ee0`, `0x01488590`, `0x01488370`, `0x014a27c0`, `0x01471a40`), converting a constructor's
  record (`0x01488d90`), and `render_record_geometry` (`0x0149ce10`) with its helpers
  (`0x0149c4e0`-`0x0149ed30`), which work out an image's dimensions and alignment for the record
  constructor.
- **The swap chain's resources** [#487](https://github.com/Yharnam-Hunters/Paleblood/pull/487), [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489) (from #245): `render_initialize_swap_chain_resource`
  (`0x02565f90`) checks a frame buffer's parameters, prepares its storage and wraps it;
  `render_prepare_swap_chain_resource_info` (`0x02571d60`) allocates that storage and publishes its
  descriptor.
- **Finishing a present** [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489): the chain that closes a frame's command lists and hands them on.
  `render_finish_present_commands` (`0x02aaf700`) finishes pending lists and submits the pending
  snapshot when there is room (`render_submit_present_finish_snapshot`, `0x02ab0b00`;
  `render_managed_present_capacity`, `0x02aaf030`); a pool of present buffers is claimed and
  released (`0x02ad3b80`, `0x02ad3c80`, `0x02ad3d40`, `0x02aaa5a0`); the release packets that tell
  the GPU a buffer may be reused are written (`0x014778f0`, `0x02abb6d0`); and the present's
  sources are prepared and their state flushed (`0x02ad1eb0`, `0x02ad1f80`), with the encoding
  helpers `0x02564ed0` and `0x02565130` and the context finisher `0x02568530`.
- **Resetting a present's context** [#557](https://github.com/Yharnam-Hunters/Paleblood/pull/557) (from #257, 11 November): five finishers put a
  present's context back to its starting state. `render_reset_present_context_preparation`
  (`0x02ad2d20`) resets both nine-entry banks of preparation records and the state around them;
  `render_prepare_present_context_scratch` (`0x02ad3070`) prepares the live bank's changed records
  before the present finishes, reloading the bank on every pass;
  `render_reset_present_context_shader_state` (`0x02acd190`) clears the conditional-command flag
  and refills the shader state, keeping the invalid-value markers between fills; and two small
  resets (`0x02abba10`, `0x02ad3300`) set one embedded state block to zero and that marker, leaving
  padding and a shared scalar's bits as they were. What each bank's records stand for is still open.

### Wind on models

`render_update_model_wind` (`0x02713870`) gives each model its wind, which the grass
and tree shaders read as `WindForceAndTime` and `PointWindForce`. Every model draw calls it
(`0x0216f9d0`, `0x02124660`). For the model's wind receiver it:

1. ages the influences it keeps: each fades out over its last 0.75 s while its wave time advances
   by the wind step;
2. copies the live influences, up to four in each of two lists, to the output;
3. samples every attached wind source: a base speed, a gust from fractal noise, and a direction
   turned by noisy angles;
4. adds or refreshes that source's influence, ramping it in over 0.1 s. Kinds 0 and 3 go to the
   first list, which keeps the strongest four once full.

It was the last function on the list of frame-rate-patched functions in issue [#32](https://github.com/Yharnam-Hunters/Paleblood/issues/32), now closed
([#568](https://github.com/Yharnam-Hunters/Paleblood/pull/568), 11 November). The patches point its three 1/30 s step loads at their own frame-time cell
and rewrite one to use the measured frame time; `BB_TARGET_FPS` does the same in code
(see The frame limiter). Three quirks of the original are kept:
three stale stack bytes copied into every influence, a failed lookup keeping the previous source's
jitter, gust and cycle values, and a hold flag that zeroes the step for every later source. What
each source kind stands for is still open.

<details>
<summary>How the wind update was verified</summary>

Edge-verified: 120 constructed cases pass against the original stock and with the pinned
community patch file for `30 FPS++`, `60 FPS++` and `Uncap FPS++` (each run with the matching
`BB_TARGET_FPS`); 109 of the original's 125 branches were seen both ways, and every
non-equivalent one of 70 deliberate mutants fails in at least one mode. A separate review
session that did not write the code requested changes, then approved the fixes. It becomes
verified once recordings from the game exist.

</details>

### How the renderer learns what the game draws

Paleblood does not replay the finished GPU commands. Instead, the GPU library's replacements, with
their packets unchanged, also tell Paleblood's renderer what they set, and the renderer draws that
on your GPU (see Runtime: the renderer):

- **Observer calls in replacements** [#414](https://github.com/Yharnam-Hunters/Paleblood/pull/414): the blend, colour, depth, stencil,
  primitive-type, instance-count, viewport and draw replacements above each report the same value
  they write, with the place in the command buffer where they wrote it. Under the verification
  harness the calls do nothing. The depth and stencil clear values and `DB_RENDER_CONTROL`
  replacements report too [#435](https://github.com/Yharnam-Hunters/Paleblood/pull/435).
- **Observed functions without replacements yet** [#416](https://github.com/Yharnam-Hunters/Paleblood/pull/416), [#418](https://github.com/Yharnam-Hunters/Paleblood/pull/418), [#435](https://github.com/Yharnam-Hunters/Paleblood/pull/435),
  [#441](https://github.com/Yharnam-Hunters/Paleblood/pull/441): where a library function has no replacement, the runtime watches its arguments at
  its entry and lets the original run. These were `setRenderTarget` (`0x014738b0`) and the depth
  target (`0x014739b0`), now replaced (above), and still are the stencil references (`0x01473190`, `0x014731f0`), the shaders' user-data writers
  (`0x014761a0`-`0x014764a0`), the indexed-draw setters and draw (`0x01475270`-`0x01475320`,
  `0x01474420`) and two compute dispatch writers (`0x01474c50`, `0x0148a350`).
- **The compute command buffer's user data** [#510](https://github.com/Yharnam-Hunters/Paleblood/pull/510): that buffer's six user-data writers
  (`0x0148aad0`-`0x0148ada0`) take no stage argument and were not observed, so a compute pass ran
  with whatever user data the record held from before; one colour lookup-table pass read another
  pass's table. They now report through the same observers as the stage writers.

This needs the game library built from the decompilation (`tools/boot.py --game-lib`). In the
maintainer's local runs it was enough to draw the studio logos and the title screen [#441](https://github.com/Yharnam-Hunters/Paleblood/pull/441).

<details>
<summary>How the library functions were verified</summary>

Each replacement above was checked against the original instructions on constructed edge cases,
which also caught deliberately broken variants; none was checked on recorded game frames. All
are edge-verified. They are [@evill33t](https://github.com/evill33t)'s work, brought up to date
with `main` and merged from the PRs named in brackets; #362 adds cases for the shader
preparation's reset modes (from #350) and #352 derives the packet tests' addresses from the
harness. The observer calls were added afterwards and leave the packets unchanged. The
functions of 10 November ([#482](https://github.com/Yharnam-Hunters/Paleblood/pull/482), [#485](https://github.com/Yharnam-Hunters/Paleblood/pull/485)-[#487](https://github.com/Yharnam-Hunters/Paleblood/pull/487), [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489)) were checked the same way and are
edge-verified; [#489](https://github.com/Yharnam-Hunters/Paleblood/pull/489) lists each one's cases in its session report.
The five context finishers of [#557](https://github.com/Yharnam-Hunters/Paleblood/pull/557) pass 3, 6, 3, 34 and 3 cases; an independent re-review
on the merged tree caught four of five planted mistakes, and the claim the fifth would have
tested (the order of one scalar load) was taken out of its note.

</details>

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
| `0x01471270` | 57 | `render_command_buffer_init` | edge-verified | Initialize command pointers and callback, rounding capacity down to complete DWORDs while preserving auxiliary fields. |
| `0x014712d0` | 51 | `render_compose_record_encoding_fields` | edge-verified | Pack one byte and five masked fields into an encoding DWORD; no memory access. |
| `0x01471310` | 544 | `render_pack_record_mapping_word` | edge-verified | Pack numeric mapping fields with priority masks and full-width mode handling; return a zero-extended DWORD. |
| `0x01471600` | 190 | `render_record_constructor_pack_pair` | edge-verified | Pack a full DWORD selector pair into an encoding; the first selector controls flag handling. |
| `0x01471730` | 21 | `render_record_encoding_metadata` | edge-verified | Bounded first-byte metadata lookup; preserve original table entries and zero fallback. |
| `0x01471770` | 27 | `render_record_encoding_byte_flags` | edge-verified | Map encoding byte 4 to 3, byte 2 to 1, and all other byte values to zero. |
| `0x014717a0` | 505 | `render_record_encoding_components` | edge-verified | Select ordered packed component arrangement; optional output only on success and Boolean AL contract. |
| `0x014719a0` | 117 | `render_record_encoding_category` | edge-verified | Decode accepted category nibbles; optional DWORD output untouched on rejection. |
| `0x01471a40` | 73 | `render_read_record_descriptor_component` | edge-verified | Read one encoding DWORD and extract one of four 3-bit components; invalid selectors return 2 without reading input. |
| `0x014721d0` | 31 | `render_set_present_last_record_indices` | edge-verified | Replace two packed indices while preserving the remaining bits. Return the old preserved bits. |
| `0x01472870` | 90 | `render_emit_present_context_section` | edge-verified | Keep the low-DWORD capacity decision separate from the full remaining-word count sent to the original import. Its effects may change the live cursor. |
| `0x01472fd0` | 86 | `render_set_cb_target_mask` | edge-verified | Write the complete CB_TARGET_MASK value, preserving all supplied component-enable bits. |
| `0x01473090` | 109 | `render_set_cb_blend_control` | edge-verified | Write masked CB_BLENDn_CONTROL for the selected color-buffer bank. |
| `0x01473320` | 117 | `render_set_cb_color_control` | edge-verified | Write CB_COLOR_CONTROL.MODE and ROP3 from their masked input fields. |
| `0x014733a0` | 93 | `render_set_db_depth_control` | edge-verified | Write the observed mask of DB_DEPTH_CONTROL depth/stencil test and write-control fields. |
| `0x01473400` | 93 | `render_set_db_stencil_control` | edge-verified | Write the low 24 bits of DB_STENCIL_CONTROL, including front- and back-face stencil operations. |
| `0x01473590` | 95 | `render_emit_present_preparation_scalar` | edge-verified | Write the captured float bits to DB_DEPTH_CLEAR. Admission tests the callback low byte; rejection returns its full 64-bit result. |
| `0x014735f0` | 89 | `render_emit_present_preparation_byte` | edge-verified | Write the captured input low byte to DB_STENCIL_CLEAR. Admission tests the callback low byte; rejection returns its full 64-bit result. |
| `0x01473650` | 116 | `render_set_screen_offset` | edge-verified | Write the observed masked X/Y fields of PA_SU_HARDWARE_SCREEN_OFFSET. |
| `0x014736d0` | 124 | `render_set_guard_band_clip_adjust` | edge-verified | Write vertical then horizontal guard-band clip adjustments as unchanged float bit patterns. Capture both float representations before the capacity callback and publish from the captured cursor. |
| `0x01473750` | 124 | `render_set_guard_band_discard_adjust` | edge-verified | Write vertical then horizontal guard-band discard adjustments as unchanged float bit patterns. Capture both float representations before the capacity callback and publish from the captured cursor. |
| `0x01473840` | 104 | `render_present_slot_emit` | edge-verified | Write the selected color target's two clear-word registers. Prefix stores precede an aliased input read; the final increment uses the live stored cursor. |
| `0x014738b0` | 246 | `render_set_render_target` | edge-verified | Emit color-buffer registers, or clear CB_COLORn_INFO for an absent descriptor, then append its NOP payload. Recheck validity after admission and reload the cursor before overlapping trailing writes. |
| `0x014739b0` | 344 | `render_set_depth_render_target` | edge-verified | Emit depth/stencil, view and HTILE registers, or clear DB_Z_INFO and DB_STENCIL_INFO for an absent descriptor. Preserve repeated validity tests and delayed reads when output overlaps descriptor state. |
| `0x01473fe0` | 90 | `render_set_vgt_primitive_type` | edge-verified | Write the low six primitive-type bits to VGT_PRIMITIVE_TYPE. |
| `0x014741b0` | 156 | `render_emit_draw_index_auto` | edge-verified | Emit DRAW_INDEX_AUTO and a query-selected THREAD_TRACE_MARKER event or NOP. Reload flags and cursor after overlapping packet stores before selecting and publishing the trailing packet. |
| `0x01475000` | 397 | `render_emit_shader_link_words` | edge-verified | Write SPI_PS_INPUT_CNTL entries from last word to first to preserve overlapping reads; retain the unmasked count shift for zero and oversized counts. |
| `0x01475370` | 76 | `render_emit_num_instances` | edge-verified | Emit NUM_INSTANCES with the complete supplied instance count. |
| `0x01475da0` | 101 | `render_emit_event_write` | edge-verified | Emit EVENT_WRITE with a masked event type and the original table-selected event index. |
| `0x014765e0` | 238 | `render_configure_viewport` | edge-verified | Write viewport ZMIN/ZMAX, then X/Y/Z scale and offset values for the selected viewport. Capture all six borrowed source words before output; retain the live and captured cursor publications. |
| `0x01476790` | 135 | `render_configure_screen_scissor` | edge-verified | Write PA_SC_SCREEN_SCISSOR_TL/BR using the low 16 bits of each supplied coordinate. Packet stores may overlap the cursor; its final increment reads the stored value after those writes. |
| `0x01476980` | 93 | `render_emit_pa_cl_vte_cntl` | edge-verified | Write PA_CL_VTE_CNTL viewport-transform enable/format bits after capacity admission. |
| `0x014769e0` | 93 | `render_emit_present_last_record_packet` | edge-verified | Write DB_RENDER_CONTROL for the last-record path. Publish and return the captured cursor plus twelve bytes; return the full callback result on rejection. |
| `0x01477150` | 109 | `render_set_pa_sc_mode_cntl_0` | edge-verified | Write MSAA_ENABLE and VPORT_SCISSOR_ENABLE in PA_SC_MODE_CNTL_0 after reserving three packet words. |
| `0x014778f0` | 204 | `render_emit_present_release_packet` | edge-verified | Emit EVENT_WRITE_EOP with by-value selectors, address and data; preserve the complete callback rejection result and final live cursor increment. |
| `0x01477b30` | 172 | `render_command_emit_counter_operation` | edge-verified | Emit the seven-word counter packet; callback admission uses its low byte, and the label range selects the masked encoding. |
| `0x014784a0` | 82 | `render_command_prepare_submission` | edge-verified | Reserve 64 command words; callback admission uses its low byte before two marker stores and the cursor advance. |
| `0x01485070` | 16 | `render_set_present_last_record_depth_compress_disable` | edge-verified | Replace DEPTH_COMPRESS_DISABLE in the saved DB_RENDER_CONTROL word using the input low bit. Return the old bits with that flag cleared. |
| `0x01485080` | 16 | `render_set_present_last_record_stencil_compress_disable` | edge-verified | Replace STENCIL_COMPRESS_DISABLE in the saved DB_RENDER_CONTROL word using the input low bit. Return the old bits with that flag cleared. |
| `0x01485220` | 81 | `render_set_pa_cl_vte_cntl` | edge-verified | Expand the toggled input byte into overlapping register fields. The original result is the shifted byte, independent of the preserved old bits or the final stored word. |
| `0x01485390` | 24 | `render_set_record_register_range` | edge-verified | Pack one bit from the first input and three from the last, preserving all other bits. |
| `0x014853f0` | 16 | `render_set_record_register_enabled` | edge-verified | Preserve the original byte-wide input: doubling it can contribute bits beyond bit one. |
| `0x01485f90` | 1410 | `render_record_constructor` | edge-verified | Clear the 44-byte record prefix and publish partial record and auxiliary outputs; return the packed 64-bit result. |
| `0x01486520` | 31 | `render_record_set_word_0c_fields` | edge-verified | Store two masked fields; return masked old word. No constructor add/subtract. |
| `0x01486540` | 305 | `render_record_set_encoding` | edge-verified | Pack accepted encoding into record DWORD10 preserving other bits; invalid input has undefined native extractor locals. |
| `0x014866e0` | 33 | `render_record_set_word_04_repeated_field` | edge-verified | Duplicate supplied low 11 bits; return preserved old word. |
| `0x01486710` | 20 | `render_record_set_word_08_low22` | edge-verified | Replace this word's low 22 bits and return the preserved old upper bits. |
| `0x01486760` | 20 | `render_record_set_word_14_second_low5_field` | edge-verified | Replace the second five-bit field and return the preserved old word bits. |
| `0x014867c0` | 21 | `render_record_set_first_auxiliary_flag` | edge-verified | Clear only bit 13 then OR full input byte shifted13; return preserved old word. |
| `0x014867e0` | 21 | `render_record_set_second_auxiliary_flag` | edge-verified | Clear only bit 14 then OR full input byte shifted14; return preserved old word. |
| `0x01486800` | 20 | `render_record_set_word_28_low22` | edge-verified | Replace this word's low 22 bits and return the preserved old upper bits. |
| `0x01486cf0` | 38 | `render_read_record_mapping_word` | edge-verified | Read one unaligned mapping DWORD and delegate three extracted fields to the original encoder. Full return is zero-extended. |
| `0x014873a0` | 163 | `render_build_shader_link_words` | edge-verified | Match byte-pair keys, then publish each packed word in the original four-store order. |
| `0x01487ee0` | 1090 | `render_initialize_pair_record_descriptor` | edge-verified | Clear 32 bytes; admit counts after float rounding, test flag bytes independently, and return packed requirements. |
| `0x01488370` | 17 | `render_record_descriptor_set_high_field` | edge-verified | Set the descriptor top nibble and return the preserved old low 28 bits. |
| `0x01488590` | 969 | `render_initialize_triple_record_descriptor` | edge-verified | Clear 32 bytes in order, delegate flags, retain partial writes and return packed requirements. |
| `0x01488d90` | 813 | `render_convert_constructor_record` | edge-verified | Convert a constructor record into a GCN image descriptor. Preserve overlapping reads, ordered component calls and independent low/middle-field tests when choosing its type. |
| `0x01489910` | 24 | `render_record_descriptor_level_count` | edge-verified | Read bits 16..19 below the upper descriptor partition; otherwise return zero. |
| `0x01489940` | 24 | `render_record_descriptor_count_exponent` | edge-verified | Read bits 16..19 in the upper descriptor partition; otherwise return zero. |
| `0x01489960` | 68 | `render_record_descriptor_encoding` | edge-verified | Read packed words at offsets 0x04 then 0x0c; pass six extracted fields to the encoding packer. |
| `0x01489c40` | 16 | `render_record_step_user_pa_enabled` | edge-verified | Query the actual import once; no cached global, fallback or service value is invented. |
| `0x01496f60` | 525 | `render_configure_viewport_and_guard_band` | edge-verified | Configure viewport transforms, scissor extent and guard-band pairs using ordered float arithmetic, floor rounding and the original upper clamp. |
| `0x0149c4e0` | 114 | `render_initialize_record_geometry_flags` | edge-verified | Clear the output before selector admission; selectors 7-12 copy the parameter flag only when the full 32-bit parameter equals 1. |
| `0x0149c660` | 486 | `render_record_geometry_resolve_parameter_index` | edge-verified | Resolve the packed parameter-table index with the original validation order and wrapping scale operations. |
| `0x0149c850` | 672 | `render_record_apply_parameter_tuple` | edge-verified | Select one output code from normalization tuple; ordered overrides and rejection preserve output. |
| `0x0149cb30` | 204 | `render_record_apply_mapping` | edge-verified | Map derived normalization code to original layout-helper tuple; unsupported code preserves output. |
| `0x0149cc60` | 350 | `render_convert_record_descriptor_classification` | edge-verified | Convert a tile-mode table index for the requested array mode; preserve output on error and bypass already matching modes. |
| `0x0149ce10` | 5124 | `render_record_geometry` | edge-verified | Calculate record dimensions and alignment, preserving staged output on failure and live reads when descriptor and result overlap. |
| `0x0149e2d0` | 295 | `render_record_constructor_optional_first` | edge-verified | Align paired components; retain the pre-increment product and 55-bit quantity, then publish optional outputs in order. |
| `0x0149e400` | 358 | `render_record_constructor_optional_second` | edge-verified | Calculate geometry and publish zeroed or partial auxiliary outputs even when a helper reports an error. |
| `0x0149e570` | 279 | `render_calculate_second_auxiliary` | edge-verified | Calculate the second auxiliary layout through two ordered unit admissions and wrapped alignment. Publish its four optional size and alignment outputs in 8/4/4/4-byte order. |
| `0x0149e8d0` | 40 | `render_record_geometry_round_up_power_of_two` | edge-verified | Round upward in the unsigned 32-bit domain; zero and an overflowing next power wrap to zero. |
| `0x0149e900` | 16 | `render_record_tuple_parameter_supported` | edge-verified | Nonzero power-of-two test with Boolean AL interface. |
| `0x0149e920` | 18 | `render_record_geometry_index_logarithm` | edge-verified | Extract the exponent after converting the positive integer to float, including float rounding near powers of two. |
| `0x0149e960` | 129 | `render_record_tuple_transform_classification` | edge-verified | Transform admitted classifications after wrapping 32-bit scaling and unsigned thresholds. |
| `0x0149ea20` | 28 | `render_record_tuple_classification_group` | edge-verified | Bounded classification-to-group lookup with fallback one and original shared table. |
| `0x0149ea70` | 25 | `render_record_tuple_alternate_classification` | edge-verified | Recognize bounded alternate-classification subset with zero fallback. |
| `0x0149ea90` | 40 | `render_record_geometry_category` | edge-verified | Read the three-bit category of a bounded record word; rejection leaves output untouched. |
| `0x0149eac0` | 40 | `render_record_geometry_classification` | edge-verified | Read the four-bit classification of a bounded record word before publishing to possibly aliased output. |
| `0x0149eb20` | 40 | `render_record_geometry_first_exponent` | edge-verified | Extract the first three-bit exponent used by the record geometry arithmetic. |
| `0x0149eb50` | 40 | `render_record_geometry_second_exponent` | edge-verified | Extract the second two-bit exponent; reject invalid indices or null output before reading the table. |
| `0x0149ed30` | 174 | `render_record_geometry_parameters` | edge-verified | Split one captured parameter word into four optional two-bit outputs, preserving write order when outputs alias. |
| `0x014a1e80` | 803 | `render_initialize_record_level_descriptor` | edge-verified | Publish a partial level descriptor in order, reread the source between helper calls, then convert its tile mode. |
| `0x014a27c0` | 627 | `render_get_record_descriptor_size` | edge-verified | Initialize mandatory outputs after the decoder call; accumulate live values and retain partial outputs on error. |
| `0x014a2c40` | 595 | `render_calculate_record_descriptor_level_offset` | edge-verified | Accumulate levels inclusively using partial helper totals even after errors; publish the two QWORD outputs in order. |
| `0x02377bd0` | 399 | `render_frame_update_02377bd0` | verified | per-frame: ages a list of ref-counted objects, updates RendMan-side child, steps another child by 1/30 s (Uncap: measured frame time) |
| `0x0247eb80` | 16 | `render_read_present_pool_counter` | edge-verified | full-QWORD locked read/writeback retry, XOR before each attempt; caller adds one. Real AMD64 write cycles retained; non-AMD64 bridge unresolved Synthetic edges pass; no recordings. |
| `0x02564ed0` | 25 | `render_present_encoding_units` | edge-verified | Return the full 64-bit entry for wrapped encoding minus one when its index is below 99; otherwise return zero. |
| `0x02565130` | 30 | `render_present_record_bit_width` | edge-verified | Return the bit width of the OR of three 32-bit inputs. All-zero input returns zero; the presentation fallback passes one as the third input. |
| `0x02565f90` | 855 | `render_initialize_swap_chain_resource` | edge-verified | Validate resource parameters, choose independent allocator and owner defaults, prepare resource storage and publish its wrapper. Allocation failure clears the output and releases prepared storage. |
| `0x02568530` | 190 | `render_finish_present_context` | edge-verified | Run the presentation context's finishing calls in order with a zeroed snapshot. |
| `0x0256e1d0` | 6280 | `render_build_present_source_record` | edge-verified | Build a mode-specific presentation record, preserving partial output, unsigned division and packed auxiliary requirements. Constructor selectors are 32-bit values even in stack slots. |
| `0x025710a0` | 301 | `render_record_normalize_mode2` | edge-verified | Normalize mode-2 input; preserve ordered 36-byte copy, supplied parameter bypass and helper error/status ordering. |
| `0x025711d0` | 361 | `render_record_normalize_mode3` | edge-verified | Normalize mode 3 input through an ordered 48-byte copy; cache mapping across conditional parameter helpers and preserve statuses and padding. |
| `0x02571340` | 303 | `render_record_normalize_mode4` | edge-verified | Normalize mode-4 input; preserve ordered 40-byte copy, optional parameter path and status/reload ordering. |
| `0x02571c60` | 47 | `render_clear_swap_chain_curve` | edge-verified | Release the swap-chain curve resource through its owner; clear only the resource after the callback. |
| `0x02571d60` | 1324 | `render_prepare_swap_chain_resource_info` | edge-verified | Allocate resource storage and publish its descriptor before copying initial data. Re-read entry counts after processing/copy callbacks; copy failures preserve partial output. |
| `0x02572290` | 47 | `render_clear_swap_chain_cached_record` | edge-verified | Release the swap-chain cached-record resource through its owner; clear only the resource after the callback. |
| `0x025b2fb0` | 768 | `render_swap_chain_present` | verified | GXSwapChainCtrl present: waits for the present interval in 60 Hz vblanks, maps it to a flip mode, presents (Uncap: mode 0) |
| `0x02713870` | 11646 | `render_update_model_wind` | edge-verified | Ages a model wind receiver's kept influences, lists the live ones and adds or refreshes one per attached wind source; applies selected frame-rate steps |
| `0x02aaa5a0` | 124 | `render_release_present_finish_node` | edge-verified | Release the finish node through the manager helper, retaining its head-match decision and reloading commands across retries. |
| `0x02aaddc0` | 393 | `render_allocate_shader_upload` | edge-verified | Allocate one shader-upload slice of at most 64 KiB. If no suitable block exists, acquire once, reclaim, then retry once. Preserve zero-size requests and delegate block policy to the original manager. |
| `0x02aaf030` | 508 | `render_managed_present_capacity` | edge-verified | Acquire managed command space, preserve partial block publication and copy the saved command snapshot in reverse order. |
| `0x02aaf700` | 750 | `render_finish_present_commands` | edge-verified | Finish pending command lists with live block and counter reads, then conditionally submit the cursor snapshot. |
| `0x02aafb30` | 509 | `render_command_submit_present` | edge-verified | Prepare and delegate a present submission, preserving capacity-failure returns and resetting begin markers to the live cursor. |
| `0x02ab0b00` | 326 | `render_submit_present_finish_snapshot` | edge-verified | Emit required counter waits and submit nonempty command regions in order; construct an empty second region when optional commands are absent. |
| `0x02ab0ce0` | 326 | `render_command_submit_batch` | edge-verified | Emit counter waits and submit nonempty regions with the packed video/index handle, or delegate the snapshot when no optional context exists. |
| `0x02ab0e30` | 213 | `render_command_submit_snapshot` | edge-verified | Initialize the auxiliary command buffer and submit nonempty regions using only the video handle. |
| `0x02ab5a70` | 1235 | `render_present_record_consume` | edge-verified | Track a resource range or emit record commands; preserve cached tokens, index wrap and final state resets. |
| `0x02ab6810` | 1173 | `render_present_last_record_consume` | edge-verified | Consume the final record range, or update its tracking bank when commands are not admitted. |
| `0x02ab7310` | 629 | `render_record_translate_selector` | edge-verified | Map supported selectors to one output DWORD; unsupported selectors return status 12 and zero the output. |
| `0x02ab7a20` | 66 | `render_choose_record_constructor_selector` | edge-verified | Choose a constructor selector from supported input values; only the low flag byte affects the extended case. |
| `0x02ab7ab0` | 124 | `render_combine_record_parameters` | edge-verified | Combine constructor choices, using only the low flag byte and bounding the four-entry original table before access. |
| `0x02ab7e40` | 22 | `render_reduce_record_parameter` | edge-verified | Return the original four-entry scale table value, or zero for an out-of-range unsigned index. |
| `0x02ab9850` | 374 | `render_flush_shader_preparation` | edge-verified | Flush the pending barrier/range before the event. Mode 0 keeps the range fields, and a suppressed state returns without touching either group of fields. |
| `0x02abaf10` | 78 | `render_record_derive_parameter` | edge-verified | Derive numeric normalization code; flag priority and mode-dependent base. |
| `0x02abb4f0` | 31 | `render_record_auxiliary_predicate` | edge-verified | Return the auxiliary-processing predicate: selectors 0x13/0x14 enable it, 0x15/0x16 use flag bit 6, and all other selectors disable it. |
| `0x02abb6d0` | 48 | `render_emit_present_release_delegate` | edge-verified | Forward address and data to the release writer with fixed flags and mode and a zero final selector. |
| `0x02abba10` | 22 | `render_reset_present_context_state_b518` | edge-verified | Reset the first 64-bit field to zero and the second to the packed invalid-value sentinel. |
| `0x02acd190` | 601 | `render_reset_present_context_shader_state` | edge-verified | Clear the conditional command flag, then preserve the sentinel stores around each distinct state-fill call. |
| `0x02ace1e0` | 1016 | `render_configure_record_shader` | edge-verified | Populate sparse primary and optional shader bindings, update special-slot caches, bind VS/PS programs, and emit up to 32 PS link words. |
| `0x02ace5e0` | 5473 | `render_configure_record_shader_secondary` | edge-verified | Consume two sparse shader binding banks, emit descriptor and address packets, copy selected upload spans, and update shared upload caches. |
| `0x02acfc20` | 207 | `render_finish_record_shader` | edge-verified | Finalize two dirty-state passes using cached event flags, clear the source dirty flags, then read the current command mode and apply a pending mode change. |
| `0x02ad1e30` | 55 | `render_restore_color_target_from_bank` | edge-verified | Restore one color target from the bank selected by flag bit 0. Keep the preparation cache unchanged while delegating the register writes. |
| `0x02ad1e70` | 61 | `render_restore_depth_target_from_bank` | edge-verified | Restore the depth target from the bank selected by flag bit 1, independently of the color-target bank in flag bit 0. |
| `0x02ad1eb0` | 194 | `render_present_source_prepare` | edge-verified | Flush source state and select an explicit or derived record count. Stop at the first builder error; reload the source parameter before consuming each successful record. |
| `0x02ad1f80` | 385 | `render_present_source_flush_state` | edge-verified | Emit dirty non-null slots from the selected bank. Keep count and flags cached on skips; reload the pending value, flags and slot count after each emission. |
| `0x02ad2c30` | 120 | `render_reset_present_preparation_dirty` | edge-verified | Emit changed depth and stencil clear values, then cache their post-call values. Preserve the second writer's upper return bits while replacing its low word. |
| `0x02ad2d20` | 517 | `render_reset_present_context_preparation` | edge-verified | Reset both nine-entry banks and surrounding state; interleave the first eight entries before loading the live scalar. |
| `0x02ad3070` | 351 | `render_prepare_present_context_scratch` | edge-verified | Prepare changed live-bank records before finishing; retain eligible pointers across refresh calls and reload the bank each iteration. |
| `0x02ad3300` | 76 | `render_reset_present_context_state_b528` | edge-verified | Reset the embedded state without changing padding; copy the shared scalar bits unchanged. |
| `0x02ad3b80` | 252 | `render_pop_present_buffer_pool` | edge-verified | Scan pool entries using the original counter and compare/exchange calls; publish the counter before successful output. |
| `0x02ad3c80` | 42 | `render_process_present_finish_item` | edge-verified | Mark the final pool word as processed; pass its address and a fixed selector to the release delegate, forwarding the packet result. |
| `0x02ad3d40` | 35 | `render_capture_present_buffer_pool_record` | edge-verified | Return zero for a zero begin address; otherwise locate the containing pool record using wrapped subtraction, arithmetic shift and unsigned division remainder. |
| `0x02ad5dd0` | 27 | `render_swap_chain_present_mode` | edge-verified | Validates low DWORD mode; retains object, loads full source pointer at +0x33d0, forwards mode to lower submit Synthetic GCC/Clang edges pass; no recordings. |
| `0x02ad5e90` | 3005 | `render_setup_swap_chain` | edge-verified | Allocate and register swap-chain buffers, initialize cached resource data and tone curves, and retain partial publications on the corresponding failure paths. |
| `0x02ad6e50` | 499 | `render_swap_chain_submit` | edge-verified | Owned-thread flip polling, source binding and submission; every exit reloads the global guard pointer. Native GPU helper effects remain original Synthetic GCC/Clang edges pass; no recordings. |
| `0x02fa6af0` | 2214 | `render_prepare_resource_descriptor` | edge-verified | Build the resource descriptor and requirements through ordered original helpers; preserve partial writes on errors and live parameter reads between calls. |

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
- The engine's own drawing code above the GPU library, its materials and post-processing have not
  been studied yet.
- Which GCN descriptor kinds the user-data writers' NOP markers (`0x68750004`, `0x68750005`,
  `0x68750006`) name.
- What the fifth argument of the dispatch writers' relatives (`0x01474d10`, `0x0148a410`) selects.
