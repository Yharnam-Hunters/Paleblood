// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the next token of a wide-character path (structs in disk_file_device.h).
//
// loading_path_token_next_02a2aca0 (0x02a2aca0) takes a cursor (position, delimiter characters,
// characters left) and returns the next token's length and first character, moving the cursor to
// the delimiter after it. Delimiters before the token are skipped first. A null position or
// delimiter set, text that ends (a 0 character) before a token, or a cursor with nothing left
// gives no token: length 0 and a null pointer. Characters are tested with the C library's wcschr,
// so a 0 character inside the counted range also ends a token. Callers include the disk file
// device's mount lookups (0x0246cf50, 0x0246d180), which split "name:" off a path.
#include <cstdint>

#include "disk_file_device.h"

using namespace loading::disk_file_device;

namespace {

// The token as the original returns it: the length in rax, the first character in rdx.
constexpr int rdx_shift = 64;   // the high half of a 128-bit return is rdx

unsigned __int128 token_result(uint64_t length, const char16_t *text)
{
    return static_cast<unsigned __int128>(reinterpret_cast<uintptr_t>(text)) << rdx_shift | length;
}

bool is_delimiter(const TokenCursor *cursor, char16_t character)
{
    return c_wcschr(cursor->delimiters, character) != nullptr;
}

}  // namespace

extern "C" unsigned __int128 bb_loading_path_token_next_02a2aca0(TokenCursor *cursor)
{
    if (!cursor->delimiters || !cursor->position)
        return token_result(0, nullptr);

    while (cursor->remaining != 0 && is_delimiter(cursor, *cursor->position)) {
        cursor->position += 1;
        cursor->remaining -= 1;
    }
    if (*cursor->position == u'\0' || cursor->remaining == 0)
        return token_result(0, nullptr);

    const char16_t *text = cursor->position;
    uint64_t length = 0;
    do {
        if (is_delimiter(cursor, *cursor->position))
            break;
        cursor->position += 1;
        length += 1;
        cursor->remaining -= 1;
    } while (cursor->remaining != 0);
    return token_result(length, text);
}
