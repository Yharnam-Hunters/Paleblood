// SPDX-License-Identifier: GPL-2.0-or-later
// The guest thread's pointer: the executable reads it from fs:[0]; the runtime moves those reads
// to gs:[0] (runtime/loader.h), so replacements read it there too.
#pragma once

namespace engine {

inline unsigned char *thread_pointer()
{
    unsigned char *p;
    asm volatile("movq %%gs:0, %0" : "=r"(p));
    return p;
}

}  // namespace engine
