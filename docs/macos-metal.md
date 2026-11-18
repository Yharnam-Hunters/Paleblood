# macOS Metal platform unit

On macOS, the normal root CMake configuration adds `pbmetal` alongside the runtime, game and
test targets. It is a native static library with a C API and an Objective-C++ implementation
linked to Apple's Metal, Foundation and QuartzCore frameworks. `PB_METAL_ONLY` defaults to `OFF`; set it
to `ON` explicitly to build the Metal unit without the other targets. An existing build cache
that selected `ON` retains that setting until changed.

The normal macOS configuration retains the guest runtime targets, but their host portability
is still in progress. Compiling or testing this Metal unit does not establish a working full
runtime build or guest boot on macOS.

The base Metal unit creates a device, command queue and RGBA8Unorm offscreen texture, submits a
render-pass clear/store, then reads pixels after command-buffer completion. It also accepts
complete RGBA8 frames through a synchronous CPU upload and can present a completed frame
through a caller-owned `CAMetalLayer`. This shared-texture implementation requires a
unified-memory Metal device. Configuration or compilation does not prove that a suitable
device exists. The caller supplies the window, application event loop and layer lifetime.
The optional draw executor below adds shader translation and offscreen draws through the
renderer’s existing call record; its guest display remains separate from this primitive.

Use Apple Clang and an installed macOS SDK. The intended deployment target is macOS 13 or
newer. For a native Apple Silicon library build, from the repository root:

```sh
cmake -S . -B build-metal -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DPB_METAL_ONLY=ON
cmake --build build-metal --target pbmetal --parallel 2
```

`PB_ENABLE_METAL_SMOKE_TEST` defaults to `OFF`: ordinary builds do not register or execute
a GPU test. On a machine where a real unified-memory Metal device is available, opt in:

```sh
cmake -S . -B build-metal -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DPB_METAL_ONLY=ON -DPB_ENABLE_METAL_SMOKE_TEST=ON
cmake --build build-metal --target test_metal_offscreen test_metal_upload --parallel 2
ctest --test-dir build-metal -R '^metal_(offscreen|upload)$' --output-on-failure
```

Each test has a 30-second CTest timeout and fails if device creation is unavailable or any
check fails. An unavailable GPU is not a passing or skipped smoke. The offscreen test checks four actual
clear/store submissions, all RGBA channels, readback padding/canaries and API lifecycle/error
contracts. Both packed and padded multirow reads are checked. Packed rows are read directly
into caller memory; padded rows use staging to preserve padding. Invalid inputs and rejected
context states leave the destination untouched; a Metal exception can partially update a
packed destination and makes the context unusable. It reports the architecture selected by
the compiler; an x86-64 or universal build
is not evidence that the guest runtime works through Rosetta. Device loss, hardware timeout
and allocation exhaustion are not deliberately induced.

The upload test uses independently literal RGBA and BGRA pattern tables. It checks a
nonuniform 7x5 frame, arbitrary source row padding, exact final-pixel capacity at a protected
page, caller-memory lifetime, rejected-update preservation and alternation with GPU clears.
It also checks 1x1, 7x1 and 1x5 frames, including an unused single-row caller stride of
`SIZE_MAX`. CPU texture updates and completed GPU clears are reported separately.

`rt_metal_offscreen_upload` copies each source row before returning and retains no caller
pointer. It supplies zero Metal row stride for a single-row source, as documented by Apple’s
[`replaceRegion:mipmapLevel:withBytes:bytesPerRow:`](https://developer.apple.com/documentation/metal/mtltexture/replace(region:mipmaplevel:withbytes:bytesperrow:)).
Pixels remain `RGBA8Unorm`, without channel conversion, alpha premultiplication or vertical
flip. A successful upload makes the texture readable without a prior clear. The owner must
serialize upload, clear, read and presentation. An upload backend exception poisons the context;
invalid input preserves its prior frame and readiness.

The `macos-metal` CI workflow configures the normal root build and compiles the library and
owned test executables for ARM64 and x86-64. It does not run a GPU or window test on a hosted
runner. Real-device and real-window results remain explicit local checks.

## Presenting completed frames

The caller owns a valid `CAMetalLayer`, its view/window and the application event loop.
`rt_metal_offscreen_configure_layer` accepts an unretained bridge to that layer, assigns the
context’s device, `BGRA8Unorm`, its exact source extent, finite drawable acquisition and
non-transactional presentation. Bounds, backing scale, attachment, `framebufferOnly`, window
resize and close policy remain with the caller. Configuration and presentation require the
main thread. Serialize all operations on the context and all mutation of its borrowed layer.
Destroying the context does not detach the layer or restore its previous configuration.

After a successful upload or clear, `rt_metal_offscreen_present` rechecks the layer profile,
reads source texels through a fixed fullscreen Metal shader and schedules the completed
BGRA drawable. Source and drawable dimensions must match. The render pass performs no
scaling, vertical flip, channel swizzle or blending. Core Animation may scale to the layer’s
bounds and apply display color processing; this API does not establish physical screen
pixel or color identity.

An optional `rt_metal_bgra_readback` captures the actual drawable before presentation. It
requires `framebufferOnly=NO`, preserves caller padding and accepts capacity ending at the
last pixel. The implementation blits into owned shared storage before scheduling presentation,
then copies caller bytes after successful GPU completion. A null capture avoids readback
allocation/copy and allows framebuffer-only drawables. Invalid input or a backend error
leaves capture bytes untouched. Apple documents why drawable data must be captured
[before presentation](https://developer.apple.com/documentation/metal/reading-pixel-data-from-a-drawable-texture).

`RT_METAL_OK` proves command completion and presentation scheduling. A caller can separately
observe the drawable’s [presented handler](https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler(_:))
and [presented time](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime).
A zero time means that the drawable was not presented or its frame was dropped; scheduling
and completed pre-present pixels do not guarantee that each frame is displayed.
The 1..30000 ms timeout bounds the GPU wait after commit. Pipeline compilation and
[`nextDrawable` acquisition](https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable())
are outside that wait; acquisition has its own system timeout. `RT_METAL_NO_DRAWABLE` leaves
the context usable. GPU failure or a GPU-wait timeout poisons it. Timeout does not cancel
submitted work, which can present later. Command resources and strong callback references
retain the layer/drawable after a timeout; callbacks retain neither caller output pointers
nor the context.

The window fixture is separately opt-in and needs a graphical macOS session and real Metal
device. Ordinary builds and the smoke option do not register it:

```sh
cmake -S . -B build-metal-window -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DPB_METAL_ONLY=ON -DPB_ENABLE_METAL_SMOKE_TEST=ON -DPB_ENABLE_METAL_PRESENTATION_TEST=ON
cmake --build build-metal-window --target test_metal_offscreen test_metal_upload test_metal_presentation --parallel 2
ctest --test-dir build-metal-window -R '^metal_(offscreen|upload|presentation)$' --output-on-failure
```

The fixture owns its AppKit application/window and observes genuine drawables. Named records
separate actual API status, GPU completion, acquisition identity, callbacks/timestamps and
literal pre-present BGRA pixel/padding checks. It checks profile/size/format/thread/state
refusals before acquisition, framebuffer-only presentation, window-bound resize with a fixed
source extent, and 1x1, 7x1 and 1x5 drawable captures. Every valid case requires its own
callback and a finite nonnegative time; zero is reported as `DROP_OR_UNCONFIRMED`, while a
positive time is `DISPLAY_OBSERVED`. The predeclared `present/normal-sized-positive-anchor`
is a separate 320x180 literal capture and must have its own positive time. No other frame
can satisfy that requirement. A single fixed startup submission follows each newly
configured source or restored profile, including the normal-sized anchor; startup times
never satisfy the anchor. Main-thread display and window-occlusion samples immediately
before and after the anchor describe only those instants, not continuous visibility or the
cause of a dropped frame. Submissions use a fixed 10 Hz cadence while servicing events,
without retries until a frame displays. The CTest limit remains 30 seconds.

These tests do not capture the user’s screen, verify physical scanout, force `NO_DRAWABLE`,
device loss, allocation exhaustion or hardware timeout, or exercise guest execution. The
runtime’s `--display window`, host input and audio are not wired by this primitive.

## Draw executor

`PB_ENABLE_METAL_EXECUTOR=ON` adds `pbmetal_executor`, implementing the same C interface as
the Vulkan executor. It requires a unified-memory device and macOS 15 or later. It maps the
direct-memory pool's file separately, wraps those shared pages in an `MTLBuffer`, and adds the
distance between guest and GPU addresses to the common GCN translator's physical buffer accesses.
Closing the caller's file descriptor or mapping does not invalidate the executor's mapping.

The executor translates GFX7 vertex and pixel programs with `pbgcn_spirv`, converts those modules
to MSL 2.3 using the official [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) C++ library,
then compiles them with Metal's runtime `newLibraryWithSource` API in safe math mode. CMake
fetches immutable commit `7affe74d77f93a622bb5002789d5332d32e512ee` (Vulkan SDK 1.4.328.1), checks
the archive's SHA-256, and builds the dependency locally under the selected build directory.
The upstream archive includes its dual Apache-2.0/MIT licence. No global SPIRV-Cross installation
or offline Metal compiler is required.

```sh
cmake -S . -B build-metal-draw -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DPB_METAL_ONLY=ON -DPB_ENABLE_METAL_EXECUTOR=ON -DPB_ENABLE_METAL_SMOKE_TEST=ON
cmake --build build-metal-draw --target test_metal_executor --parallel 2
ctest --test-dir build-metal-draw -R '^metal_executor$' --output-on-failure
```

The optional Metal executor selects the headless display, no host audio, and the no-host-input
provider for boot wiring. These build targets do not qualify a full macOS boot or host controller input.

The executor processes recorded calls in submission order. Registers, targets, shaders, user data,
scissors and index state carry across submitted buffers until an explicit `rt_executor_reset`.
Reset forgets that state while keeping completed target pixels and compiled caches. A rejected
call does not roll back earlier accepted state changes; later submissions must repair or reset
unsupported state. Reset does not revive an executor poisoned by a device failure.
Live system-call shader bindings are placed by their command-buffer write address; the common
recorder joins them to the submitted buffer at their byte offsets. After a submission, the next
ordinary record or placed binding starts a new use of that buffer, even if its offset increases.
It supports viewport 0, vertex/pixel shaders and their separate user-data ranges, auto-index and
16/32-bit indexed triangle and rectangle lists, and one COLOR_8_8_8_8 render target (UNORM or sRGB, either
component order).
Index data above a 4GiB pool-relative offset uses a page-aligned, shared Metal buffer
window with a small native offset. The window aliases the pool's bytes without a CPU
copy, so GPU-produced indices remain current. The draw retains the pool mapping until
completion; allocation failure stops by name. Direct and offset indexed triangle lists
and strips use the same window. `metal_index_windows` checks low, boundary-crossing,
high and exact-pool-end spans with both index widths in a sparse 5GiB pool, including
repeated GPU-produced indices, exact pixels and bounded input/canary comparisons.

Targets are limited to 4096 pixels per axis; viewport X/Y scales and translations must be
finite and within ±4096, with a positive X scale and a nonzero Y scale.
Vertex input profiles 0 through 3 reuse the common translator. VGPR0 holds the vertex ID;
profiles 1 and 2 add the instance index divided by `VGT_INSTANCE_STEP_RATE_0` in VGPR1 and
by `VGT_INSTANCE_STEP_RATE_1` in VGPR2, and profile 3 adds the instance index in VGPR3.
The step-rate words and their written bits persist across call fragments and reset to unwritten.
A missing or zero step rate is unknown: reading its enabled input before overwriting it stops
translation by name. Undeclared inputs have no established hardware value. Primitive-ID inputs
and unmodeled stage state remain outside this profile.

`NUM_INSTANCES` is a full 32-bit count; zero means one instance, as documented for
`VGT_NUM_INSTANCES` in the [AMD CIK register reference, page 29](https://docs.amd.com/api/khub/documents/9fuBVmqajj07G~5~aeTUig/content).
The native auto and indexed
draws use that count with base instance zero (and base vertex zero for indexed draws), so the
index slice is reused for every instance. This follows Apple's
[instance-count draw contract](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/drawprimitives(type:vertexstart:vertexcount:instancecount:baseinstance:)).
No per-instance host allocation or extra DMA bound is inferred. Vertex shader cache keys include
the input profile, both full step-rate words and written bits; the instance count remains dynamic.
Fetch calls use the same starting-register and cache contract.
The recorded viewport clip bounds must be standard 0 and 1 (either signed zero is accepted);
other bounds stop explicitly. Position and enabled clip-distance vectors require float4 export
formats. Both clip vectors, including sparse selections of all eight distances, are supported;
miscellaneous and cull outputs are rejected. Shader interfaces may use up to 32 located float4
parameters. Declared vertex parameter and pixel interpolant capacities must cover the translated
interface, including on shader-cache hits; larger unused allocations are accepted.

Pixel exports support RGBA as FP16_ABGR or 32_ABGR at their original locations in the eight
color slots, perspective-center inputs and no depth export. Pixel-position X/Y inputs and whole-quad mask expansion are outside this profile
and are rejected explicitly. Other color layouts, output masks, barycentric modes and depth
controls stop with a reason. User SGPR counts must fit both the 16-word model and the declared shader allocation.
The translator's required user-SGPR read mask is cached with each shader. Every draw checks it
against slots explicitly written for that stage since reset; writing zero establishes a value,
while untouched backing-array zeros do not. Partial writes accumulate across buffers, and reset
forgets written slots while keeping the shader cache. This follows the public translator's
program-order report: a write encountered in one branch can suppress a later read in another;
it does not provide path-sensitive initialization analysis.

The input remains valid GFX7 code with consistent resource metadata: this backend does not
validate every instruction against its declared SGPR/VGPR allocation.

Readback is tightly packed BGRA, matching the display API. Sampled textures are separate
resources loaded from their descriptors; colour and depth/stencil attachments use canonical
guest memory.
Each active color slot supports RGBA8/BGRA8 UNORM or sRGB attachments with matching extents
and disjoint guest memory. Each has its own ADD blend state with ZERO, ONE, SRC_ALPHA or
ONE_MINUS_SRC_ALPHA factors, using the color factors for alpha as well. Other factors or
operations, separate alpha, ROP3 overrides, reserved fields, non-copy color operations and
partial color masks stop with a reason. A slot without an export or with a zero target mask
leaves its guest bytes intact. Shaders and depth/stencil operations still execute when color
writes are disabled. The shader and pipeline caches preserve the original enabled output
locations, and the pipeline includes each attachment's format and effective blend state.

`DB_DEPTH_BOUNDS_MIN` and `_MAX` retain independent raw 32-bit words, including NaNs,
infinities and signed zero, while depth-bounds testing is disabled. This register layout and
`DB_DEPTH_CONTROL.DEPTH_BOUNDS_ENABLE` bit 3 follow AMD's
[GFX7 register definitions](https://github.com/torvalds/linux/blob/ee83899302a0457a3ba47464a13e8682bd7ba27d/drivers/gpu/drm/amd/include/asic_reg/gca/gfx_7_2_d.h)
and [field masks](https://github.com/torvalds/linux/blob/ee83899302a0457a3ba47464a13e8682bd7ba27d/drivers/gpu/drm/amd/include/asic_reg/gca/gfx_7_2_sh_mask.h).
An enabled depth-bounds test is refused by name before drawing, including zero-count and
zero-colour-mask draws; active bounds testing is not implemented. Register writes and state
carry across call fragments and submissions; reset clears the values and control. Queue
selection changes compute state only. Exact inactive values have no public readback, so the
owned `metal_depth_bounds` test qualifies admission, refusal and fresh pixel/vertex writes,
while source review establishes the independent raw stores. No private state getter is introduced.

Depth/stencil draws support single-sample D16, D32 and S8, all eight comparisons, front/back
stencil references and masks, keep/zero/replace/invert, and clamped or wrapping steps of one.
Replacement with STENCILOPVAL stops if it conflicts with a distinct comparison or replacement
reference. Explicit zero formats disable their tests; Z_WRITE alone does not enable depth.
A null pixel shader requires an active depth/stencil attachment and no colour writes. Colour
mode CB_DISABLE also suppresses colour writes while the draw executes.

The shared GFX7 layout decoder uses direct DB geometry and independent slice strides. Macro
tile splits above 1024 bytes require an unmodeled guest DRAM-row profile and stop explicitly. Each
active aspect requires explicit geometry, extent, stride, one selected view slice and its read
base; writable aspects also require the same write base. Larger depth/stencil surfaces preserve
texels outside the colour extent. View read-only bits disable native writes and CPU publication.
Before every draw the executor loads canonical guest bytes into private Metal textures through
aspect-specific staging blits, then publishes completed writable texels back after GPU completion.
Selected slices retain their bank rotation; padding and other slices remain untouched. This first
path allocates attachment resources per draw rather than keeping a dirty depth-image cache.

HTILE and expanded clears stop when an active aspect depends on them. A stencil-only draw
with `DB_STENCIL_INFO.TILE_STENCIL_DISABLE` set uses the uncompressed S8 plane even when the
inactive Z format retains HTILE or expanded-clear flags; it requires no Z base and does not
read or modify Z metadata. Stencil expanded-clear flags still stop explicitly. Clear/copy/
resummarize operations, multisampling, layered views, distinct read/write bases, depth bounds,
overlapping attachments and unsupported register fields stop explicitly. The pipeline cache includes depth/stencil formats, and every draw rebinds its test
state and references. Default front-face orientation matches the existing Vulkan profile with
both viewport Y signs; additional raster face/cull modes remain outside the profile. D32/S8 uses
combined textures with separate plane blits. D16/S8 requires Apple GPU family 2 for separate
attachments. These paths have run on an M4 Max; other hardware remains unqualified.
Triangle strips (`VGT_PRIMITIVE_TYPE` 6) select Metal's native triangle-strip primitive for
auto draws and every admitted 16/32-bit direct or offset indexed route. Triangle lists retain
their existing mapping and pipeline cache; topology is selected at each draw. Geometry shaders,
rectangle expansion and other primitive types remain outside this backend's profile.

Metal's [indexed draw contract](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/drawindexedprimitives(type:indexcount:indextype:indexbuffer:indexbufferoffset:))
always treats an index equal to the maximum of its width as a strip restart. Guest restart
state is not modeled here, so an indexed strip containing that incompatible value is refused
by name before encoding; this is an explicit backend limit, not a claim that AMD rejects it.
The executor scans only the fetched range after the existing address, alignment and DMA-bound
checks, freshly on every draw, with no fetch for zero count. `0xffff` is ordinary data for a
32-bit index; its restart marker is `0xffffffff`. Restart-register writes retain their existing
unknown-register refusal. No guest reset/default or complete primitive-restart support is claimed.
The owned `metal_triangle_strips` fixture uses independent solid-square and gapped-column pixel
goldens, fresh vertex stores and full-pool canaries across geometry, cache, indexing and refusals.

Screen scissors use the existing register events for `PA_SC_SCREEN_SCISSOR_TL` and `_BR`.
The [AMD CIK register manual](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/CIK_3D_registers_v2.pdf)
defines their components as signed 16-bit values: negative coordinates clamp to zero, the top/left
edge is inclusive and the bottom/right edge exclusive, with no window offset. The executor
intersects that rectangle with its target for Metal. Coordinates outside AMD's documented
positive range, or only one of the initial TL/BR pair, are rejected. Empty intersections still
submit a genuine zero-area draw. This path has run on an M4 Max; other GPUs remain unqualified.

Indexed state uses the canonical `INDEX_BASE`, `INDEX_SIZE` and `INDEX_TYPE` events.
Logical `DRAW_INDEX_OFFSET` calls carry only first/count and check the retained `INDEX_SIZE`
when one was set. The same offset event has an explicit `has_packet_fields` tag for raw
`DRAW_INDEX_OFFSET_2` words; zero raw headers stay raw and are rejected. Addressed `DRAW_INDEX_2`
events retain their actual packet header, address, count, maximum and initiator. Predication,
other raw header flags, byte swapping, unsupported index types and initiator values stop with
named errors. Both forms require an explicit supported `INDEX_TYPE`. The offset form shifts
`INDEX_OFFSET` by the selected element width and adds `INDEX_BASE`, as the
[AMD SI programming guide, section 2.5.6](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/si_programming_guide_v2.pdf)
describes. Address addition, alignment and the fetched byte range are checked against the pool.
A zero-count raw packet still submits a pass, using no guest index address or memory.

A raw packet's `MAX_SIZE` programs its own DMA bound, independently of retained `INDEX_SIZE`.
The CIK register manual describes good fetches and zero filling beyond `VGT_DMA_MAX_SIZE`;
applying that bound at the effective shifted base is an inference from the two documents, not a
PS4 GPU measurement. The raw profile accepts `index_count <= MAX_SIZE`, independently of
the starting offset, and rejects draws requiring DMA zero filling. Public evidence has not
established whether a direct draw changes the CP's saved `INDEX_BASE`: a later nonempty offset
draw therefore requires a fresh explicit base event. The canonical call record retains the raw
index events. `INDEX_SIZE` is stored for logical offset calls; it does not add a first-plus-count
bound to a raw packet's independent `MAX_SIZE`.

Unsupported formats, shader instructions, shader inputs, layered/multisampled targets and
target reallocations return a named error. Blending and unrecorded state remain
outside this profile. Pixel image loads and fixed-LOD sampling use the bindings described below.

### Rectangle lists

Rectangle draws capture the original vertex shader outputs in a rasterization-disabled pass,
then a small vertex shader expands each triple into two triangles. The original program is
not rerun for the generated vertices. Native indexed invocation/cache behavior is retained;
source IDs are snapshotted before capture, and sparse IDs allocate through their maximum value.
Incomplete triples, expanded-count overflow and buffers exceeding the device limit stop explicitly.
Rectangle-list instance counts above one are refused before capture; ordinary triangles and strips
retain their native instanced paths.

The expansion follows the existing `rect_list.c` project contract: ordered clip-space XY
comparisons select corner 1, then 2, otherwise 0. It emits the strip's two triangles with
fourth output `(next + after) - corner` for the complete position float4, every parameter float4
and every enabled clip scalar. Viewport Y conversion occurs once after expansion. Capture
struct members are reflected, and MSL checks the allocation stride with `static_assert`.
This establishes agreement with the project's canonical expansion; it does not independently
establish PS4 hardware rules for varying W, competing predicates or non-rectangular inputs.

A completed capture is checked for shader runtime stops before rasterization. Earlier vertex
buffer writes survive such a stop; the target and depth/stencil planes are not published by
that raster pass. Allocation or encoder failure after capture also does not roll back those
vertex writes. Each pass waits for completion, so this first implementation adds a submission
and synchronization per rectangle draw. Device loss and hardware timeouts are not induced by
owned tests.

## Vertex fetch calls

A vertex program can call one straight-line fetch program through `s_swappc_b64` and return
through its saved SGPR pair with `s_setpc_b64`. The backend reuses `gcn_call_sgpr` and the
common vertex translator; it does not emulate calls separately. The target must come from
an untouched, fully written pair within the declared vertex user data. Its 48-bit address
must be dword aligned and inside the shared pool. The common bounded decoder includes the
complete return instruction. Branches, nested calls, images, a changed target before the
call, and writes to the saved return pair remain outside the common fetch contract.

The target is resolved again at each draw, before a cache lookup or program read. The cache
also compares complete live caller and fetch bytes, their addresses and the existing shader
layout. Changing a target or overwriting either program therefore cannot select a stale
translation. Descriptor-table pointers are read by the translated fetch program from the
vertex stage's data; the normal required-versus-written checks also apply on cache hits and
after reset. Auto-index and both supported indexed draw forms share this path.

## Pixel images and samplers

Pixel resources reuse the common image/sampler origin analysis and GFX7 descriptor decoder.
Descriptors can be in user SGPRs or loaded through a 48-bit pointer and proven byte offset.
Declared SGPR bounds and written-slot masks are checked before reading either form, including
on shader-cache hits. Texels and descriptor words are read again at each draw, so CPU, compute
and completed render-target writes remain visible through cached native views.

Fragment coordinate gradients use the common translator's numeric quad broadcasts. The Metal
adapter accepts constant subgroup sources 0 through 3 and scalar 32-bit integer or float values.
It gathers the requested producer's raw bits and presence in one native `quad_or` reduction;
it never permutes data from an inactive lane. The call is materialized before later selections
can short-circuit it. A missing producer discards that fragment and atomically records a fault
in a separate word from the common translator's runtime stop. After GPU completion, the executor
checks that word before publishing color or depth back to the guest pool. A fault poisons the
executor: reset cannot resume it. This does not roll back shader writes performed earlier in
the draw. See the [Metal language specification, sections 6.10.3 and 6.11.1](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf)
for active-lane reductions, helper invocations and discard behavior.

Admission currently requires one inlined, straight-line guest block. Quad reads inside per-lane
selections, prior invocation termination, guest loops and mutable or escaping dispatcher state
are rejected. Literal Boolean subgroup votes are width independent; dynamic wave votes and
whole-quad mask expansion still require a separate control-flow proof. Vertex quad operations
remain rejected. This does not broaden the sampler's relative-mip-zero profile below.

`metal_quad_values_cpu` checks owned SPIR-V metadata, control-flow refusals, naming collisions
and fresh-compiler reproducibility. `metal_quad_values` additionally executes full and one-pixel raster
coverage with dynamic raw-bit patterns, plus deliberately divergent native packet callers that
exercise the missing-source fault and discard. The latter callers are outside the adapter's
admitted control-flow profile. These tests establish the stated numeric-read profile on the
tested Metal device; they do not establish whole-quad execution or original-GPU equivalence.

The backend supports 1D/2D images and arrays, mip and layer view ranges, and the common tiling
modes through `rt_tiling_mip_chain`. SPIRV-Cross lowers 1D textures as 2D with height one. Native
views preserve the descriptor's base mip, base layer and component selectors; selectors for
unknown or absent components fail explicitly. Complete padded allocations must fit the pool.
Image dimensions are limited to 16384 and arrays to 2048 layers; actual native allocation and
BC/32-bit filtering capabilities are also checked.

Formats follow the common renderer's established table: R8/RG8/RGBA8 UNORM, R32 FLOAT/UINT/SINT,
RG32/RGBA32 FLOAT, RGBA8 UINT/sRGB, BC1/2/3/7 UNORM/sRGB, and BC4/5 UNORM/SNORM. BC6 and other
unestablished combinations fail by format name. Integer return bits stay integer bits; sRGB
reads use Metal's native conversion before filtering. Its precision is checked using the
encoded-byte rule in the [Metal language specification, section 8.7.7](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf).

Samplers support normalized wrap, mirror and clamp-to-edge addressing, with equal point or
bilinear min/mag filters. Linear filtering requires a filterable sampled image; integer images
used only by texel loads do not impose that restriction. The common translator supplies coordinate
gradients for `image_sample`; the native unmipmapped sampler selects the view's first level. Nondefault minimum LOD, bias or reserved
mip controls with a multi-level sampled view fail explicitly until their semantics are modeled;
single-level views retain the common profile's irrelevant-field behavior. Pixel image stores
remain unsupported.

`test_metal_images` uses one covered fragment to store raw results, independent of framebuffer
quantization. Owned LLVM-assembled programs exercise every mapped format, dimensions, tiling,
relative mips/arrays, all sixteen image/sampler slots (and rejection of a seventeenth), descriptor rereads, CPU/compute/draw publication,
point/bilinear/address modes, swizzles, native limits and rejected inputs. These device tests
require BC support and 32-bit float filtering; hosted CI only compiles them. The use of native
[texture views](https://developer.apple.com/documentation/metal/mtltexture) and capability queries
establishes this backend's support profile, not a real-game or original-GPU comparison.

## Render-target memory

Color targets use the shared guest pool as their backing store. Their full padded layout must
fit in that pool. The backend reuses `runtime/tiling.h` for linear modes 8/31, 1D display/thin
modes 9/13 and 2D display/thin modes 10/14. Unsupported tile modes and invalid pitches fail
before allocating or using a texture. A cached target's size, format and tile mode stay fixed.

Before each draw, encoded texels are detiled from the pool and loaded into the Metal texture;
the pass preserves them with a load action. After synchronous completion, the native encoded
texels are tiled back into the pool, preserving layout padding. Later CPU accesses, buffer
reads, compute calls and overlapping target views therefore observe the completed draw,
including when a later recorded call fails. Reset forgets stage state without erasing memory.
The public BGRA readback reads the current backing bytes, so CPU and compute updates remain
visible before another draw. UNORM/sRGB encoding and native channel order are preserved;
only public readback converts RGBA storage into BGRA output.

This implementation copies through reusable CPU rows at each draw. It establishes ordered
memory behavior without a GPU tiling-copy or batching optimization; no full-game performance
result is claimed.

## Compute

The executor also accepts compute bindings, compute user data and ordered dispatches.
It uses the common GFX7 compute translator and the same SPIRV-Cross/MSL compiler,
shared memory pool and runtime-stop channel as draws. Compute user data and its
written-slot mask are independent of vertex and pixel state. A reset forgets those
values while retaining compiled pipelines. The cache includes the complete program
bytes, local dimensions, user count and X workgroup-ID layout; changing code at an
existing address does not reuse its previous pipeline.

This is a lane-local compute profile. All three thread-ID inputs must be explicitly
enabled, and local XYZ dimensions must be positive, fit the device/pipeline limits
and contain at most one logical 64-lane GCN wave. The profile does not assume that
Metal has a native 64-lane SIMD group. Actual subgroup operations, including
wave-wide votes and quad swaps, are rejected before MSL compilation. The common
translator declares vote capabilities even for programs that never use them;
capability declarations alone do not cause rejection. Lane-local VCC half writes
retain the logical lane index from the common translator.

The optional X workgroup-ID SGPR follows the declared user SGPRs. Y/Z ID SGPRs,
packed workgroup-size input and compute samplers remain unsupported. Dispatches
accept 1..65535 X workgroups and one Y/Z workgroup. Compute copied-call user-data
records must contain 1..8 words within the modeled slots. Required user data must
have been explicitly written; zero is a valid written value.

Compute images use the common descriptor origins, typed translation and canonical tiling. The
backend binds up to sixteen single-level 1D/2D storage images or arrays. R8/RG8/RGBA8 UNORM,
R32 FLOAT/UINT/SINT, RG32/RGBA32 FLOAT and RGBA8 UINT support separate loads/stores; native
read/write access uses the device's supported [read/write texture tier](https://developer.apple.com/documentation/metal/mtldevice/readwritetexturesupport).
RG8 UNORM and RG32 FLOAT require separate accesses. Four-component storage images accept
bijective permutations of their four channel selectors. The common translator maps loads
forward and stores by the inverse mapping; native views retain physical channel order. The
compiled image-binding cache includes that mapping. One- and two-component stored channels
retain their identity-only profile; unused selectors do not affect storage. Nonzero mips, BC storage, sRGB reads, unsupported dimensions,
sampling, partial component stores and unsupported formats fail explicitly. Overlapping writable
bindings must share the same native view; general overlapping-view coherence remains open.

Descriptors and texels are resolved before every dispatch, including pipeline-cache hits. Cached
translations also match image dimensions, array/type/component layout and origin. Completed stores
publish exposed slices back to canonical guest bytes before reporting a later shader stop. Current
bytes in unexposed slices and layout padding survive publication. Read-only images do not publish
stale texels over buffer writes. Pixel and storage-image views have separate usage-aware cache
entries and exchange data through the pool. This provides ordered dispatch/draw/CPU visibility;
it does not establish arbitrary image/buffer alias coherence inside one dispatch.

Write-only RGBA8 sRGB descriptors in the 1D/2D and array profiles follow the Vulkan
backend's [explicit compatibility policy](https://github.com/Yharnam-Hunters/Paleblood/blob/a7729d573ae6c0f790447c041c50f2b0c8328ada/runtime/renderer/render_executor.c#L3257-L3265): the local storage format becomes RGBA8 UNORM before
native allocation and binding. Values are packed as UNORM without gamma encoding, including
alpha. The raw descriptor remains the texture-cache key; ordinary current-guest upload and
completed-store publication preserve untouched texels and padding. Reads, read/write access,
compressed sRGB storage and 3D descriptors remain refused by this compatibility profile.

This is a project compatibility choice, not GFX7 hardware equivalence. The [Sea Islands ISA's
format table](https://docs.amd.com/api/khub/documents/YO8RUh~LIQ5Cx3M_Kq9qkg/content#page=80) does not list sRGB image writes, and [AMD PAL deliberately converts a write view to
UNORM](https://github.com/GPUOpen-Drivers/pal/blob/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/hw/gfxip/rpm/rsrcProcMgr.cpp#L1125-L1131) after performing any needed gamma conversion in its shader. The native UNORM compatibility
write does not establish GFX7 behavior for an sRGB descriptor. `test_metal_srgb_storage` uses
interior RGB and independent alpha to distinguish this raw-UNORM policy from automatic gamma encoding.

Each compute submission completes before a later dispatch or draw consumes its
shared buffer and image writes. Completed draw pixels are already published into that pool,
and the next draw reloads it. These checks do not establish a working game boot
or character-creation rendering on macOS.

The full-four-component mapping follows the common translator's storage contract. AMD's
[Kaveri image channel table](https://github.com/ROCm/ROCR-Runtime/blob/d614ea8bbd73a7832c265725117274d13041ff06/src/image/image_lut_kv.cpp)
programs BGRA and the non-involutive ARGB mapping in the image descriptor. AMD's device-library
and [LLVM store lowering](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/llvm/lib/Target/AMDGPU/SIISelLowering.cpp#L8173)
pass the original four-component value and descriptor through without a software channel reorder. This supports inverse stores as a driver/compiler inference;
it is not a captured console truth table. Constant, reserved and repeated selectors remain
refused in this four-component profile. Per-slice CPU transfers use zero `bytesPerImage` for
these non-3D textures, following [Metal's transfer contract](https://developer.apple.com/documentation/metal/mtltexture/replace(region:mipmaplevel:slice:withbytes:bytesperrow:bytesperimage:)). Partial stores and arbitrary overlapping-view coherence
remain outside the existing storage profile.

`test_metal_permutations` checks all 24 full-four-component permutations in RGBA8 UNORM,
RGBA8 UINT and RGBA32 FLOAT, across the existing 1D/2D and array shapes. Independent literal
forward and inverse tables check logical loads, raw canonical stores and untouched padding.
Dedicated same-program/address transitions include a non-involutive four-cycle and its inverse;
CPU changes and later raw GPU buffer observations test cached reloads and publication. Every
owned image-store instruction explicitly sets the ISA's required `unorm` flag.

`test_metal_storage` exercises owned LLVM-assembled programs across all nine uncompressed
formats, four dimension/array types and six tiling modes. It checks typed results, preserved raw
backing bytes and padding, view slices, read/write limits, live descriptor/cache changes, loaded
48-bit pointers, complete descriptor/pool ends, all sixteen image slots, rejection of a seventeenth, and runtime-stop publication.
Image tests also consume completed image stores as pixel input and completed draws as compute
image input. These device tests use synthetic data and do not establish original-game equivalence.

`test_metal_compute` runs owned buffer programs on the actual device. It checks
partial and 64-invocation groups, multidimensional local IDs, X workgroups, VCC
halves, stage isolation, partial user writes, cached resets, live code changes,
metadata/admission guards, runtime-stop recovery, two ordered dispatches and a draw
reading computed colors. It also dispatches after a completed draw and after reset. No original game shader or capture is used.

Descriptor-format accesses use the common translator's runtime stop channel: the executor owns
a shared GPU-addressable stop word, declares its residency for both stages, and checks it after
completion. Unsupported runtime formats or reserved swizzle selectors return a named load/store
error rather than a successful draw. Supported loads use the descriptor's component swizzles
and zero/one selectors; packed stores use the inverse component mapping. The unsupported access
itself leaves its destination untouched; other shader accesses and attachment writes may already
have run. Scalar VCC half writes that leave a draw-stage lane mask unknown also return a named
runtime stop. Each subsequent draw resets the word. Whole 32-bit uint, sint and float formats,
packed UNORM8 loads and supported packed stores retain the common translator's semantics. A whole-component store
checks the selectors for the format's real components; selectors beyond them do not affect it.

A GPU timeout or device failure makes the executor unusable; completion retains
the pool and stop word until in-flight GPU work finishes.

The separate `metal_vertex_instancing` fixture uses owned literal tile geometry and disjoint
(instance, vertex) records to check both rate quotients and actual instance IDs. Its full-pool
oracles cover every direct draw route, cached rate changes, explicit zero, reset and named
unknown-input refusals; its small instance counts do not qualify extreme GPU workloads.
The two additional strip groups use the same 31 route/quotient cases with four-corner strips
and a repeated final-corner tail. Their literal full-tile pixel oracles distinguish a valid
six-vertex Triangle fallback, including with reversed indices; default triangle-list inputs
and the 76-case inventory are unchanged.

The real-device test uses only owned synthetic GFX7 programs, descriptors and data. It checks
interpolated triangles, two-parameter interfaces and their capacity guards, all eight clip
distances, FP16 and float32 color exports, clip-bound and resource-header guards, buffer loads
through user SGPRs, independent pixel user data, channel
order, rectangular targets, both viewport Y directions, submission boundaries, cross-buffer masked indexed draws and explicit state resets,
shared-memory updates, signed/exclusive and empty scissors, nontrivial 16/32-bit indices in both
address forms, pool-end index reads, zero-count draws, index updates, placed-shader ordering and
recorder buffer isolation, recorder submission resets, null pixel shaders, 40-bit descriptor bases, descriptor
formats and swizzles in both stages, packed UNORM8 buffer loads, canonical logical index-size
bounds and raw-header tagging, pool-end scalar loads with constant selectors, skipped-store
canaries, format/VCC runtime-stop recovery, pixel-position/whole-quad guards, missing image/sampler descriptor guards, descriptors loaded from memory, fetch rendering through live target and
program changes, partial target writes, last-pair and pool-end guards, straight-line call refusals, strip guards, depth/stencil admission, required-versus-written user SGPRs with explicit zero and cached-reset checks, compute input/profile guards and incomplete-rectangle refusals, readback bounds and mapping lifetime.
`test_metal_depth` additionally checks depth/stencil comparisons, operations and masks, both face
orientations and viewport signs, native read-only behavior through overlapping triangles,
scissored writes, padded selected slices, mixed attachment extents, null-pixel draws, retained
state, required-register presence and canonical-memory updates. Stencil-only metadata cases
use literal S8/BGRA results and complete pool comparisons to preserve the inactive Z plane,
metadata bytes, padding and other slices; active metadata-dependent aspects retain prework
refusals.
`test_metal_alpha_blend` checks literal factor-pair results, RGB and alpha bytes, pipeline-cache
transitions and fresh destination memory in both channel orders. Its sRGB results use the
[IEC transfer curve](https://registry.color.org/rgb-registry/srgb); RGB is encoded while
[alpha remains linear](https://developer.apple.com/documentation/metal/mtlpixelformat).
Owned vertex stores and full-pool guards also cover inactive blend state and unsupported-state recovery.
`test_metal_vertex_only` checks null-pixel draws with no attachments through vertex stores and
complete pool comparisons. The same vertex code alternates with color and depth/stencil draws;
sparse indices, instance-input tuples, zero counts and single-execution rectangle sources have
independent byte goldens. Nonzero or unprogrammed pixel stages and existing rectangle guards
remain refusals. Vertex-only execution disables native rasterization and keeps shader memory
writes; it does not skip the vertex stage or admit attachmentless fragment execution.
`test_metal_rectangles` compares explicit ordinary-triangle references with rectangle draws,
including permutations, both viewport signs, varying W, clip outputs, two parameters, sparse
indices, offset draws, multiple rectangles, vertex counters and capture-stop publication.
`test_metal_rectangle_values` separately checks the compiled expansion helper with labelled
corner/tie/fallback cases, ordered NaNs, signed zero, arithmetic order, sparse parameter location
31 and all eight negative fourth-clip cases. These helper checks do not execute a source vertex
shader or establish rasterization behavior.
The tests fail if no suitable GPU exists.
Hosted CI compiles this code for ARM64 and x86-64; it does not run these GPU checks. A successful
offscreen test establishes neither window presentation nor execution of a game on macOS.
Setting the Metal debug environment did not activate a debug device wrapper on the tested host;
these are real GPU results, without an established API-validation-layer pass.

On non-Apple builds the existing Linux build/tests are unchanged. Explicitly requesting
any Metal option on a non-macOS platform fails rather
than silently creating unsupported targets. The existing Linux verification suite is preserved
for the normal GCC and Clang builds.

`test_metal_coherence` exercises all supported target tile modes and both channel orders
with UNORM and sRGB storage. It checks initial masked loads, complete raw buffer copies,
compute writes, CPU patches, cached reloads, padding and neighboring canaries, overlapping
targets, ordered errors, a draw reading another target through buffer loads, exact pool-end
admission, padded bounds and tile-mode cache identity. It uses owned programs and descriptors.
