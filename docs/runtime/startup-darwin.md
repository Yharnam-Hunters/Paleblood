# Darwin guest startup

The `pbboot` CLI runs AMD64 guest code on an x86-64 Mac process. On Apple Silicon, build it
with `-DCMAKE_OSX_ARCHITECTURES=x86_64` and run through Rosetta. Native ARM host-library tests
do not establish guest execution. An Apple SDK and Rosetta are required for that configuration.

```sh
cmake -S . -B build-boot -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
cmake --build build-boot --parallel 2 --target pbboot test_boot_scope_darwin test_boot_arena test_boot_arena_failures
ctest --test-dir build-boot --output-on-failure --no-tests=error -R '^(darwin_boot_scope|boot_owned_startup|boot_game_library|module_tls_boot|boot_arena|boot_arena_failures)$'
```

The example disables optional PkgConfig dependencies so an ARM Homebrew installation does not
get linked into an x86-64 executable. To decode movies or use other optional libraries, supply
matching x86-64 dependencies. The offscreen Metal executor is selected separately with
`-DPB_ENABLE_METAL_EXECUTOR=ON`; see [the Metal unit](../macos-metal.md). This startup change
does not add window presentation or establish that the game reaches a menu.

Before import addresses are bound, startup reserves one inaccessible, non-replacing image
arena below the runtime's 40-bit mapping limit. It tries the previous image address first,
then higher aligned candidates. Only pages inside that owned reservation become accessible.
Module hook stubs use the same reservation. Import listing returns before reserving or running
code. The CLI retains the arena until process exit because guest workers can outlive entry.

The main thread allocates variant-II guest TLS and qualifies its own guest pointer through the
real stack-switch entry inside native signal recovery. Only that successful qualification
allows rewriting the executable and bundled modules to the owned GS-slot displacement.
Module and optional game-library hooks are installed after the rewrite, and code caches are
invalidated before guest execution. Guest workers use the same qualified layout and their own
thread pointer, as described in [guest workers](guest-workers-darwin.md).

The recovery frame is active only during a guest call. A stopping worker claims the result,
publishes its complete payload, then signals the owner. Only the owner may jump to its active
frame. A stop between module phases skips the next call; the terminal entry-return claim also
waits for an already winning worker to finish publication. After disarming the timer, a queued
SIGALRM remains a timeout result. The previous signal mask is restored after recovery. Signal
handlers and guest resources remain process-owned while workers can still refer to them.

Tests use only owned AMD64 programs. They cover entry arguments, callbacks, main/worker TLS,
worker return and explicit exit, faults, deadlines, unresolved imports, module hooks and optional
library failures. Native controller tests force stop-publication and phase-boundary races.
Arena tests exercise real non-replacing reservations and host failure injection. These tests
establish host startup behavior, not differential equivalence to a PS4 kernel.
