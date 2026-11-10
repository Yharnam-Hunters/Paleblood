/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/host_time.h"
#include <cerrno>
#include <cstdio>
int main()
{
    const timespec expired{};
    if (rt_host_sleep_until_monotonic(nullptr) != EINVAL || rt_host_sleep_until_monotonic(&expired) != 0) return 1;
    std::puts("host deadline C++: 2 checks passed");
    return 0;
}
