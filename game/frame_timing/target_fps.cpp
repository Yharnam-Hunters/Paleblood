// SPDX-License-Identifier: GPL-2.0-or-later
#include "target_fps.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace frame_timing {

Target target_fps()
{
    static const Target target = [] {
        const char *v = std::getenv("BB_TARGET_FPS");
        if (!v || !*v) return Target::original;
        if (!std::strcmp(v, "30")) return Target::fps30;
        if (!std::strcmp(v, "60")) return Target::fps60;
        if (!std::strcmp(v, "uncapped")) return Target::uncapped;
        std::fprintf(stderr, "frame timing: BB_TARGET_FPS=%s is not 30, 60 or uncapped; ignored\n", v);
        return Target::original;
    }();
    return target;
}

}  // namespace frame_timing
