// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for small engine string helpers.

enum {
    ASCII_UPPER_TO_LOWER = 'a' - 'A',
};

// Lowercases the ASCII letters A-Z of a string in place and returns the string.
char *engine_ascii_to_lower_in_place(char *text)
{
    char *cursor;
    for (cursor = text; *cursor; cursor++)
        if (*cursor >= 'A' && *cursor <= 'Z')
            *cursor += ASCII_UPPER_TO_LOWER;
    return text;
}
