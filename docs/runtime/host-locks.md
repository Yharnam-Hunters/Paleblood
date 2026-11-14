# Host timed locks

The guest mutex/rwlock providers retain their existing handles, recursive-depth bookkeeping,
FreeBSD error values and relative microsecond timeouts. Linux uses the existing monotonic
pthread clock-lock functions and monotonic condition attributes.

Darwin does not provide those clock-lock or condition-clock-attribute APIs. Its condition
wait uses `pthread_cond_timedwait_relative_np`, calculating the remaining interval from the
existing monotonic deadline while holding the caller's mutex. The native wait releases and
reacquires that mutex, including expired deadlines; the guest provider still restores every
recursive level. Long intervals are capped to one hour per native wait to avoid overflow
in Darwin's relative nanosecond conversion; a chunk timeout rechecks the original monotonic
deadline, while notifications and other errors return normally. The POSIX absolute-wall-clock entry keeps the existing conversion to one
monotonic deadline. Wall-clock changes during an active wait are not followed.

Mutex and rwlock timed acquisition use native try operations, with sleeps of up to one
millisecond capped by the original monotonic deadline. The first attempt is immediate, even
for a zero timeout. Busy results retry; other host errors propagate through the existing
guest error mapping. The common sleep helper rechecks the deadline after interrupted sleeps.
This is a polling host adapter: it does not promise native timed-lock wake latency, fairness,
or an original-console scheduling match. The requested sleep quantum is not a bound on host
scheduler delays. Thread cancellation remains outside these checks.

The API basis is Apple's public [mutex try-lock contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_mutex_trylock.3.html),
[rwlock contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/pthread_rwlock_wrlock.3.html)
and the declaration of `pthread_cond_timedwait_relative_np` in the installed SDK. Apple's
[libpthread source](https://github.com/apple-oss-distributions/libpthread/blob/main/src/pthread_cond.c)
corroborates that the extension selects relative waiting; no implementation code is copied.

`syslib_locks` calls the production providers through the actual system-library registry.
It runs the existing timed-lock suite plus condition timeout, notification, recursive release/
reacquisition, ownership, zero-timeout and absolute-wall-clock cases. `host_lock_wait` checks
busy retries, deadline capping, second rollover, representable endpoints and host errors with
controlled clock/attempt/sleep inputs to the same helper consumed by Darwin.

On Darwin, `host_cond_wait` also compiles the production condition provider with test-only
clock/wait aliases to check interval bounds, repeated chunk timeouts, notifications, original
deadlines and host errors. Real native waits are covered separately by `syslib_locks`.

These tests are registered in the normal root build and in an isolated project usable while the
remaining Darwin boot prerequisites are being ported:

```sh
cmake -S test/host_locks -B build-host-locks -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-locks --parallel 2
ctest --test-dir build-host-locks --output-on-failure
```

Select `-DCMAKE_OSX_ARCHITECTURES=x86_64` for an Intel/Rosetta process or `arm64` for an
Apple Silicon host. These tests establish host synchronization behavior, not guest boot,
Metal presentation, game performance or original-PS4 differential verification.
