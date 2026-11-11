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
inputs cannot be assumed to contain zero. This limit is separate from unsupported fetch-shader calls.
The recorded viewport clip bounds must be standard 0 and 1 (either signed zero is accepted);
other bounds stop explicitly. Position and enabled clip-distance vectors require float4 export
formats. Both clip vectors, including sparse selections of all eight distances, are supported;
miscellaneous and cull outputs are rejected. Shader interfaces may use up to 32 located float4
parameters. Declared vertex parameter and pixel interpolant capacities must cover the translated
interface, including on shader-cache hits; larger unused allocations are accepted.

Pixel exports support target 0 RGBA as FP16_ABGR or 32_ABGR, perspective-center inputs and no
depth export. Pixel-position X/Y inputs and whole-quad shader operations are outside this profile
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
this profile. Image and sampler bindings stay empty in this draw backend;
`image_load`, `image_load_mip` and `image_sample` return named translation errors, including
image descriptors loaded from memory. Fetch-shader callees are also unbound in this backend:
`s_swappc_b64` returns a named translation error. Triangle strips and depth-target registers
remain outside this profile. Rectangle lists need a geometry expansion path and are rejected; compute shader bindings and dispatch events are also rejected
explicitly by this draw-only backend.

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
canaries, format/VCC runtime-stop recovery, pixel-position/whole-quad guards, unbound pixel-image
and sampling operations, descriptors loaded from memory, unbound fetch calls, strip/depth-target
guards, required-versus-written user SGPRs with explicit zero and cached-reset checks, unsupported compute/rect
events, readback bounds and mapping lifetime.
It fails if no suitable GPU exists.
Hosted CI compiles this code for ARM64 and x86-64; it does not run these GPU checks. A successful
offscreen test establishes neither window presentation nor execution of a game on macOS.
Setting the Metal debug environment did not activate a debug device wrapper on the tested host;
these are real GPU results, without an established API-validation-layer pass.

On non-Apple builds the existing Linux build/tests are unchanged. Explicitly requesting
any Metal option on a non-macOS platform fails rather
than silently creating unsupported targets. The existing Linux verification suite is preserved
for the normal GCC and Clang builds.
