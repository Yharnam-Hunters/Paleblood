// SPDX-License-Identifier: GPL-2.0-or-later
// The imported GOT slot holds the exported _Stderr object's address. Forward that
// single loaded value directly to fputs; no stream layout or second load is needed.
#include <cstdint>

#include "runtime/original.h"

namespace
{

struct StdioStream;
RT_GLOBAL(0x057e40c0, imported_stderr_object, StdioStream *);
RT_ORIGINAL(0x02fbf1e8, write_string, int32_t(const char *text, StdioStream *stream));

} // namespace

extern "C" int bb_engine_write_stderr(const char *text)
{
    (void)write_string(text, imported_stderr_object.get());
    return 0;
}
