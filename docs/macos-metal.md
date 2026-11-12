# macOS Metal platform unit

On macOS, the normal root CMake configuration adds `pbmetal` alongside the runtime, game and
test targets. It is a native static library with a C API and an Objective-C++ implementation
linked to Apple's Metal and Foundation frameworks. `PB_METAL_ONLY` defaults to `OFF`; set it
to `ON` explicitly to build the Metal unit without the other targets. An existing build cache
that selected `ON` retains that setting until changed.

The normal macOS configuration retains the guest runtime targets, but their host portability
is still in progress. Compiling or testing this Metal unit does not establish a working full
runtime build or guest boot on macOS.

The base Metal unit creates a device, command queue and RGBA8Unorm offscreen texture, submits a
render-pass clear/store, then reads pixels after command-buffer completion. This first
shared-texture implementation requires a unified-memory Metal device. Successful CMake
configuration or compilation does not prove that a suitable device exists. There is no
window, presentation or guest execution in this unit. The optional draw executor below adds
shader translation and offscreen draws through the renderer's existing call record.

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
cmake --build build-metal --target test_metal_offscreen --parallel 2
ctest --test-dir build-metal -R '^metal_offscreen$' --output-on-failure
```

The test has a 30-second CTest timeout. It fails if device creation is unavailable or any check
fails; an unavailable GPU is not reported as a passing or skipped smoke. It checks four actual
clear/store submissions, all RGBA channels, readback padding/canaries and API lifecycle/error
contracts. Both packed and padded multirow reads are checked. Packed rows are read directly
into caller memory; padded rows use staging to preserve padding. Invalid inputs and rejected
context states leave the destination untouched; a Metal exception can partially update a
packed destination and makes the context unusable. It reports the architecture selected by
the compiler; an x86-64 or universal build
is not evidence that the guest runtime works through Rosetta. Device loss, hardware timeout
and allocation exhaustion are not deliberately induced.

The `macos-metal` CI workflow configures the normal root build and compiles the library and
smoke executable for ARM64 and x86-64. It does not run a GPU test on a hosted runner. The
real-device test remains an explicit local check, and its result must be reported separately.

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
16/32-bit indexed triangle lists, and one COLOR_8_8_8_8 render target (UNORM or sRGB, either
component order).
Targets are limited to 4096 pixels per axis; viewport X/Y scales and translations must be
finite and within ±4096, with a positive X scale and a nonzero Y scale.
Only vertex input profile 0, with vertex ID in VGPR0, is modeled by the common translator.
The other three `VGPR_COMP_CNT` encodings are rejected explicitly; their additional hardware
inputs cannot be assumed to contain zero. Fetch calls use the same vertex-ID input profile.
The recorded viewport clip bounds must be standard 0 and 1 (either signed zero is accepted);
other bounds stop explicitly. Position and enabled clip-distance vectors require float4 export
formats. Both clip vectors, including sparse selections of all eight distances, are supported;
miscellaneous and cull outputs are rejected. Shader interfaces may use up to 32 located float4
parameters. Declared vertex parameter and pixel interpolant capacities must cover the translated
interface, including on shader-cache hits; larger unused allocations are accepted.

Pixel exports support target 0 RGBA as FP16_ABGR or 32_ABGR, perspective-center inputs and no
depth export. Pixel-position X/Y inputs and whole-quad mask expansion are outside this profile
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

Readback is tightly packed BGRA, matching the display API. Images are separate GPU resources:
they are not copied back into the guest pool or interpreted as tiled console surfaces.
The generic register events validate this backend's supported opaque-triangle profile: blending,
depth, non-copy colour operations, partial colour masks and multiple instances stop with a reason.
A zero colour mask disables attachment writes while still executing the draw. The Metal test
observes an owned vertex shader's buffer stores while the attachment remains unchanged. The pipeline cache includes
the write mask, and restoring it enables colour writes again.

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
target reallocations return a named error. Blending, depth and unrecorded state remain outside
this profile. Pixel image loads and fixed-LOD sampling use the bindings described below.
Triangle strips and depth-target registers
remain outside this profile. Rectangle lists need a geometry expansion path and are rejected.

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

Fragment sampling accepts the common translator's quad broadcasts for coordinate gradients;
SPIRV-Cross lowers them to native 2x2 fragment-quad operations. Whole-quad mask expansion remains
rejected because its wave-wide SCC is not established by a native 32-lane SIMD group. Vertex quad
operations remain rejected. Native sampler selection is still limited to relative mip zero below;
accepting the gradient instructions does not broaden that LOD profile.

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
program changes, partial target writes, last-pair and pool-end guards, straight-line call refusals, strip/depth-target
guards, required-versus-written user SGPRs with explicit zero and cached-reset checks, compute input/profile guards and rectangle-list refusals, readback bounds and mapping lifetime.
It fails if no suitable GPU exists.
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
