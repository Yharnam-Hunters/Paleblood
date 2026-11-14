// SPDX-License-Identifier: GPL-2.0-or-later
// Stopping the process by name from a replacement, for a path where the original hangs or
// crashes (a search with no not-found exit, a write through a null pointer). A replacement does
// not quietly do something else there: it stops and says which function reached the path.
//
// The game library cannot link the runtime's rt_fatal (runtime/include/runtime/process.h), so it
// looks it up in the host, as the render observer does (runtime/render_observer.c): the boot
// harness exports it and records the stop as the run's outcome. Under a host without it the
// message goes to stderr and the process aborts.
#pragma once

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

namespace engine {

[[noreturn]] inline void stop(const char *function, const char *message)
{
    using HostFatal = void (*)(const char *function, const char *message);
    if (auto host_fatal = reinterpret_cast<HostFatal>(dlsym(RTLD_DEFAULT, "rt_fatal")))
        host_fatal(function, message);
    std::fprintf(stderr, "game: %s: %s\n", function, message);
    std::fflush(stderr);
    std::abort();
}

}  // namespace engine
