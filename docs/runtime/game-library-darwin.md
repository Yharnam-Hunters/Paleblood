# Game library on Darwin

The x86-64 `bbgame` library builds with Mach-O capture/observation entries and the same hook
ABI as Linux. Probe stubs use the shared non-replacing mapper. Stack arguments are copied
through a fault-safe self-process read (Mach VM on Darwin, the existing full-width Linux
syscall convention on Linux); unreadable or incomplete words remain zero.

Observation only needs instruction relocation. The existing Capstone relocation algorithm
therefore builds separately from single-step tracing. With Capstone on Darwin, observation
hooks report the arguments then tail-call the relocated original; `rt_trace_available()` stays
false because the Linux signal/ucontext tracer is not ported. Without Capstone, both relocation
and tracing report unavailable, and original observation hooks are not installed. A successful
library build alone does not establish a rendered game frame.

Build the runtime as described in [Darwin startup](startup-darwin.md), adding a matching target
architecture Capstone installation through `-DPB_CAPSTONE_ROOT=/your/capstone-prefix` when
PkgConfig is disabled. The prefix must contain `include/capstone/capstone.h` and `lib/libcapstone`.
Normal native builds may instead discover Capstone through PkgConfig. CI stages the public
universal Capstone 5.0.9 wheel with a fixed SHA-256 and no system package installation.

```sh
cmake --build build-boot --parallel 2 --target bbgame test_observe_hooks test_host_hooks test_render_observer test_capture test_capture_stack test_capture_observe_entry test_probe_bridge
PB_REQUIRE_RELOCATION=1 ctest --test-dir build-boot --output-on-failure --no-tests=error --timeout 30 -R '^(observe_hooks|host_hooks|render_observer|capture|capture_stack|capture_observe_entry|probe_bridge|boot_game_library)$'
```

The fixtures execute the production assembly with six integer registers, eight vector registers,
two stack arguments, variadic AL, pass-through results, and integer/vector result registers.
Guard-page tests exercise partial and unreadable stacks. The real probe stubs increment counters
while preserving the original's return value; only their periodic output thread is controlled in
that fixture. The existing observation fixture calls its original through the real relocated
entry. Linux keeps the single-step capture/replay tests alongside these bridge tests.

The own runtime loads the library with `--game-lib` (normally `build-boot/game/libbbgame.dylib`).
For offscreen Metal execution add `-DPB_ENABLE_METAL_EXECUTOR=ON` when configuring, then run with
`--display headless`. Window presentation and matching-architecture movie decoding are separate
requirements. Genuine gameplay captures and decompiled-function verification are not supplied by
these synthetic fixtures.
