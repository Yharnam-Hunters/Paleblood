# Owned Darwin x86-64 guest-pointer storage

`runtime/guest_tls_darwin.c` owns a public pthread key and stores a caller-owned live guest TCB
pointer on each participating host thread. Its C API is in `runtime/include/runtime/guest_tls.h`. It does not
allocate a host/guest TCB, change GS base, write GS:0, install signal handlers or execute code.
This foundation is not wired into `pbboot` and does not establish full guest TLS or Mac boot.

Create requires a NULL handle. Bind accepts a non-NULL pointer; read returns the calling
thread's public slot, including NULL when unbound. Clear is idempotent. Functions preserve
caller errno and report an exact pthread error separately on `RT_GUEST_TLS_HOST_ERROR`.
Invalid arguments and reported host failures retain outputs/ownership as documented in the
header. Output storage must be valid and disjoint from the handle and TCBs.

Keep each binding and its TCB alive until all direct/API reads using it finish. Different
threads may bind/read/clear concurrently. Before destroy, quiesce every other API call and raw
reader, including metadata readers. Remaining bindings return BUSY. Clear must succeed before
thread exit/cancellation and before freeing a TCB. There is no destructor: a thread that exits
bound conservatively leaves the handle BUSY. External setspecific/key_delete would violate
accounting; public reads are allowed while the owned key lives. This is not a signal-handler API.

## Candidate layout and qualification

`get_candidate` checks 64-bit pointer/key widths and the inspected desktop key range256..767,
then checks key*8 fits a positive signed32 displacement. It reports candidate metadata, not a
private-layout guarantee. Create may own a key outside that profile; metadata then fails
UNREPRESENTABLE and the still-owned unbound key can be destroyed normally.

Apple's [public key contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_key_create.3.html)
defines opaque process-wide keys with per-thread values. The numeric range and GS addressing
are implementation details in these pinned primary sources:

| Source | Revision and content SHA256 |
| --- | --- |
| [libpthread key allocation and public access](https://github.com/apple-oss-distributions/libpthread/blob/1f4f5265b319111142f1bf3a27d4484ef5a98314/src/pthread_tsd.c#L162-L257) | commit `1f4f5265b319111142f1bf3a27d4484ef5a98314`; SHA256 `3722b5cac4f5ec6d61b729d68e97682fe70bb0a25425f8728eec77a148d91d2f` |
| [libpthread desktop dynamic-key bounds](https://github.com/apple-oss-distributions/libpthread/blob/1f4f5265b319111142f1bf3a27d4484ef5a98314/src/types_internal.h#L129-L137) | same commit; SHA256 `8e48a8ad40e3ade70facc56d506634c0c8adaa66e9e61232ed56d75a20d099ee` |
| [XNU x86 TSD access](https://github.com/apple-oss-distributions/xnu/blob/ac9718fb1af618d5ce8678d0dc6e8a58f252216f/libsyscall/os/tsd.h#L110-L139) | commit `ac9718fb1af618d5ce8678d0dc6e8a58f252216f`; SHA256 `c4db9e6399fb21de1e7368c53b6b22efc5325eff9976f2b5574efae18aec1164` |

These OSS revisions are not identified as the shipped libraries on every Darwin/Rosetta
release. Before any patched image uses an offset, an explicit owned startup/test boundary must
establish public/direct agreement on every participating thread. Never assume a fixed key or
select an apparently unused reserved slot. The candidate offset is intended for the separately
qualified equal-size loader rewrite; this unit does not validate or perform that rewrite.

## Standalone owned tests

Use Apple Clang, a macOS SDK and an x86-64 process environment. Apple Silicon requires working
x86-64 translation. The isolated project rejects non-Darwin, ARM64 and universal builds; it
never changes the root Linux CMake build. From the repository root:

```sh
cmake -S test/darwin_guest_tls -B build-darwin-guest-tls \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build-darwin-guest-tls --config Release --parallel 2
ctest --test-dir build-darwin-guest-tls -C Release --output-on-failure
```

The project builds the static unit and two C11 executables with Release settings and
`-Wall -Wextra -Werror`. Each CTest has a 10-second bound. The real owned test also has an
8-second process alarm. Checks remain active with NDEBUG; unsupported behavior fails rather
than becoming a skipped/pass result.

The real test synchronizes main plus one worker under the same live owned key. Each holds two
successive distinct fullwidth object pointers, then NULL. API/public reads must equal the
candidate GS read. The owned code is a 9-byte MOV plus a 1-byte RET, 10 bytes total. It checks
GS:0/pthread_self, ordinary errno, a host _Thread_local canary, sentinel contents and flags
around MOV+RET. Readers finish before BUSY destruction checks; clear/join precede deletion and
unmapping. The fake-API executable checks allocator, pthread error, invalid metadata and
lifetime/accounting paths without raw GS or real key operations.

A passing owned test qualifies that tested process/host configuration only. It does not measure
every GP/FP register or host TLS field, cover all FS forms/module TLS, validate signal/stack or
CPU scheduling integration, establish future-version compatibility, or demonstrate guest boot,
Metal presentation or gameplay.
