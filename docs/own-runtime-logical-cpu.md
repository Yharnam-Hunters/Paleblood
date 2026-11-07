# Immutable logical CPU slots

This component stores a logical CPU count and its corresponding slot mask. It also provides a
read-only Darwin host-count query. The root build compiles the component and its slot tests;
the runtime and game do not call it yet. The PR stays a draft until a runtime consumer needs
this interface and the Linux host query is established.

`rt_logical_cpu_slots_init` accepts 1..64 and publishes a named count/reserved/mask value only on
success. Bits 0..N-1 identify Paleblood logical slots, not host processor IDs or an observed console
mask. Keep the initialized value immutable while readers can access it; there is no reconfiguration
API. Construct a different value in separate storage when needed. `read` validates the
count, zero reserved field and complete corresponding mask before copying exactly 8 bytes to the output.
Output must be valid/disjoint from the immutable value, and may be unaligned. Initialization
requires aligned valid value storage. NULL, invalid count or malformed state leaves outputs alone.
The 64-slot case uses UINT64_MAX, never a shift by 64. No allocation, destruction or live query is
needed to read an existing value.

`rt_logical_cpu_darwin_count` is distinct from native allowed-CPU queries. It reads `hw.logicalcpu`
through the public C sysctlbyname API with newp=NULL/newlen=0. Require successful status, exactly
one int32, positive value, and at most 64 before publishing a uint32. Host API failure returns
HOST_ERROR and the exact captured native errno; even native errno 0 remains 0. Invalid returned size
or nonpositive value is MALFORMED_HOST_DATA. A legitimate positive count beyond 64 returns
COUNT_OUT_OF_RANGE, not a clipped mask. Non-Darwin returns UNSUPPORTED without output. All component
APIs restore the caller's errno; native_error is zero except a HOST_ERROR diagnostic. Output
addresses are borrowed valid storage; this API does not promise to recover an invalid pointer.

Apple's [system-capabilities guide](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities)
defines the key as an enabled logical-core count. The pinned Apple XNU public header at commit
ac9718fb1af618d5ce8678d0dc6e8a58f252216f, libkern/libkern/sysctl.h (SHA256
1c83417ca7bad80971f39c971804a6932c21cf620ec4143ec055b342e4cd2839), identifies count parameters as 32-bit
and notes availability may change. This is not a per-thread eligibility set or CPU identity list.
The [public sysctl manual](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/sysctlbyname.3.html)
defines the read-only interface and returned byte count. Mach affinity tags are scheduling hints;
this unit neither uses them nor promises hard pinning. A count read has no association with an
already initialized value: any future integration must explicitly freeze its selected topology.

## Tests

The root CMake project builds the slot-mask test. The separate test project also exercises the
Darwin query on x86-64 Darwin, and the unsupported-query path on Linux. Its Darwin configuration
requires an explicit single architecture and rejects ARM64 or universal builds. The component
itself is portable C, with the host query conditional on Darwin.

```sh
cmake -S test/logical_cpu -B build-logical-cpu -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-logical-cpu --parallel 2
ctest --test-dir build-logical-cpu --output-on-failure
```

Tests also run with assertions disabled and cover 1/3/63/64, invalid counts/state, full output canaries and unaligned reads.
Darwin additionally builds a test-only sysctl alias against the exact component: read-only
arguments, errno preservation, missing errno, malformed sizes/counts, >64, successful reads and
independence of prior slot values. A separate test calls the real count API once;
unsupported/unrepresentable data fails rather than skipping or guessing. Linux's pure test
checks explicit unsupported-query behavior. Each CTest has a 10-second process bound; it is not an
intrinsic host sysctl duration guarantee. No test runs guest bytes, sets affinity, or installs a
scheduler policy.

These tests check the component in isolation. They do not establish a console affinity mask,
install a scheduler policy, or exercise executable startup. Validation results and compiler
identities belong in the PR report.
