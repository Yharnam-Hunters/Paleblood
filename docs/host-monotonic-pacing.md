# Host monotonic pacing

The headless AudioOut queue and VideoOut's vsync flips wait for absolute host
`CLOCK_MONOTONIC` deadlines through `rt_host_sleep_until_monotonic`. The helper
uses POSIX `nanosleep` so those consumers compile on macOS as well as Linux.
After every wake, including `EINTR`, it reads the monotonic clock again and
computes the remaining time against the original deadline. An expired deadline
returns immediately; an invalid deadline or a permanent clock/sleep error
returns a host error number. A wait may finish late due to host scheduling.

AudioOut retains its existing block schedule and NULL-buffer flush behavior.
VideoOut retains its existing next-tick calculation and hsync flips do not wait.
Both consumers stop by name on a host timing error. AudioOut releases its port
lock before reporting that error. These host errors are not mapped to guessed
PS4 result codes. Host audio playback and the flip-rate policy are unchanged.

The tests can be built independently of the rest of the runtime:

```sh
cmake -S test/host_time -B build-host-time -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-time
ctest --test-dir build-host-time --output-on-failure --no-tests=error
```

`host_time` uses real signals to interrupt a wait. `host_time_cpp` checks C++
linkage. `host_time_pacing` compiles the production helper, AudioOut, VideoOut and
syslib registry with test-only clock aliases. It checks deadline normalization,
repeated interruptions, expired deadlines, host errors, audio flushes, vsync
boundaries, and hsync bypass. Its display/event boundary is stubbed; it does not
establish game boot or display presentation on macOS.
