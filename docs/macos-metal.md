# macOS Metal platform unit

On macOS, the normal root CMake configuration adds `pbmetal` alongside the runtime, game and
test targets. It is a native static library with a C API and an Objective-C++ implementation
linked to Apple's Metal and Foundation frameworks. `PB_METAL_ONLY` defaults to `OFF`; set it
to `ON` explicitly to build the Metal unit without the other targets. An existing build cache
that selected `ON` retains that setting until changed.

The normal macOS configuration retains the guest runtime targets, but their host portability
is still in progress. Compiling or testing this Metal unit does not establish a working full
runtime build or guest boot on macOS.

The Metal unit creates a device, command queue and RGBA8Unorm offscreen texture, submits a
render-pass clear/store, then reads pixels after command-buffer completion. This first
shared-texture implementation requires a unified-memory Metal device. Successful CMake
configuration or compilation does not prove that a suitable device exists. There is no
window, presentation, game shader translation or guest execution in this unit.

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

On non-Apple builds the existing Linux build/tests are unchanged. Explicitly requesting
either Metal option on a non-macOS platform fails rather
than silently creating unsupported targets. The existing Linux verification suite is preserved
for the normal GCC and Clang builds.
