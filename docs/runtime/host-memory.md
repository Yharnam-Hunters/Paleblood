# Direct memory on Darwin

The runtime keeps its existing 5 GiB physical pool, allocation ranges, mapping ledger,
reserve/batch operations and guest protections. Linux retains its memfd backing and
128 GiB GPU base. Darwin uses an unlinked `tmpfile`, retains its `FILE` owner for the
process lifetime and sets `FD_CLOEXEC`. The descriptor is published only after sizing
succeeds; failed preparation closes the temporary owner.

`rt_map_at` extends the existing anonymous placement helper to shared file mappings.
It validates the extent and flags, uses `MAP_FIXED_NOREPLACE` where available, and
otherwise uses an ordinary hint. A result at another address is released before
returning `EADDRNOTAVAIL`. Only that newly returned allocation is released; a declined
hint does not establish why the host rejected it. Explicit replacement flags are
rejected. Batch replacement continues to require an owned reservation.

Darwin can already occupy the Linux GPU window. On first placement, the runtime
probes full pool spans at 128 GiB increments below the existing 40-bit map-window
limit, then releases the successful probe. The selected base keeps its original
128 GiB alignment; automatic CPU and flexible mappings start after the GPU span.
This is address selection, not a permanent reservation: each later mapping still
claims its range without replacement. If another mapping takes a GPU range first,
the runtime stops by name. This host policy is not a PS4 address-layout claim.

## Validation

The independent CPU tests need no guest image or graphics dependencies:

```sh
cmake -S test/host_memory -B build-host-memory -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-memory --parallel 2
ctest --test-dir build-host-memory --output-on-failure --no-tests=error
python3 test/test_memory_mapping.py -v
```

They run the existing reserve/batch and unmap suites, real shared aliases above a
4 GiB file offset, descriptor size/close-on-exec checks, occupied-range canaries,
and declined-hint cleanup. Darwin additionally injects constructor and window-probe
failures to check cleanup and delayed publication. The Darwin host harness observes
intentional fatal stops through `rt_fatal_observer`; a returning call fails the test.
Actual protection tests still fault in a child. Linux keeps its SIGABRT checks.

With a real Metal device, the normal root build can also use the actual runtime
pool as executor input:

```sh
cmake -S . -B build-memory-metal -DCMAKE_BUILD_TYPE=Release \
  -DPB_ENABLE_METAL_EXECUTOR=ON -DPB_ENABLE_METAL_SMOKE_TEST=ON
cmake --build build-memory-metal --target test_direct_memory_metal --parallel 2
PB_REQUIRE_METAL=1 ctest --test-dir build-memory-metal --output-on-failure \
  --no-tests=error -R '^direct_memory_metal$'
```

This test opens the full 5 GiB pool, dispatches owned shader code at physical offsets
above 4 GiB, changes CPU input between dispatches and checks CPU-visible output.
It fails if opening the device or dispatching fails; it has no device-unavailable
success path. Hosted CPU checks do not qualify Metal execution. These host tests do
not establish original-game behavior or successful guest startup on macOS.
