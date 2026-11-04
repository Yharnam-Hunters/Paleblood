# Immutable logical CPU slots

This independent component is based on upstream 45f6f962dd10b94d4105ae969f60b480aa1cf638.
It adds a value object and a read-only Darwin count API. It does not change the root build,
`rt_cpu_query_native`, native host-ID types, boot, guest attributes, scheduling or thread creation.
The separate startup integration remains a later unit.

`rt_logical_cpu_slots_init` accepts 1..64 and publishes a named count/reserved/mask value only on
success. Bits 0..N-1 identify Paleblood logical slots, not host processor IDs or an observed console
mask. Treat the initialized value as immutable while published; no mutation/reconfiguration API
is provided. Construct a different value in separate storage when needed. `read` validates the
count, zero reserved field and complete corresponding mask before exactly 8-byte memcpy output.
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

## Isolated owned tests

The ordinary root CMake project is unchanged. The separate test project accepts whole x86-64
Darwin or Linux. Darwin requires an explicit single architecture, and rejects ARM64/universal.
The component itself is plain portable C, with a conditional Darwin query; the restricted test
proposal matches the current owned-runtime execution architecture.

```sh
cmake -S test/logical_cpu -B build-logical-cpu -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-logical-cpu --parallel 2
ctest --test-dir build-logical-cpu --output-on-failure
```

Release-active tests cover 1/3/63/64, invalid counts/state, full output canaries and unaligned reads.
Darwin additionally builds a test-only sysctl alias against the exact component: read-only
arguments, errno preservation, missing errno, malformed sizes/counts, >64, successful reads and
independence of prior slot values. A separate ordinary owned test calls the real count API once;
unsupported/unrepresentable data fails rather than skipping or guessing. Linux's pure test
checks explicit unsupported-query behavior. Each CTest has a 10-second process bound; it is not an
intrinsic host sysctl duration guarantee. No test runs guest bytes, sets affinity, or installs a
scheduler policy.

The reviewed component passed isolated normal Release builds under Linux GCC 14 and Clang 19
(one CTest each) and whole-x86-64 macOS AppleClang (three CTests), with zero skips. The real
Darwin query returned 16 enabled logical CPUs on that validation host. This is a host count
observation, not a guest affinity measurement. All 236 source files remained unchanged during
validation. Root-project integration, original-executable startup and gameplay are outside this
component validation.
