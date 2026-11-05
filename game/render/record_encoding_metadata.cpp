// SPDX-License-Identifier: GPL-2.0-or-later
// Read metadata from the first encoding byte. Larger byte values return zero;
// neither the remaining encoding bytes nor an additional argument are read.
#include "record_encoding.h"

extern "C" uint32_t bb_render_record_encoding_metadata(const uint8_t *encoding)
{
    const uint32_t index = *encoding;
    if (index >= render::record_encoding_metadata_entries) return 0;
    return render::record_encoding_metadata.get()[index];
}
