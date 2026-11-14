// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the engine's own C string helpers. They are classic C
// runtime routines on plain `char`, which is signed on this target: the comparisons return the
// difference of the first differing characters as signed bytes.
#include <stddef.h>

#define CASE_DISTANCE ('a' - 'A')

static inline char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? c + CASE_DISTANCE : c;
}

// Lower-cases one ASCII letter; any other character is returned as it is (sign-extended).
int engine_ascii_to_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        c += CASE_DISTANCE;
    return c;
}

// strlen, returning a 32-bit count.
unsigned engine_string_length(const char *text)
{
    const char *end = text;
    while (*end)
        ++end;
    return (unsigned)(end - text);
}

// strcmp on signed characters.
int engine_compare_signed_strings(const char *left, const char *right)
{
    char a, b;
    do {
        a = *left++;
        b = *right++;
        if (!a)
            break;
    } while (a == b);
    return a - b;
}

// strcat: appends `source` after the terminator of `destination`; returns `destination`.
char *engine_string_append(char *destination, const char *source)
{
    char *end = destination;
    while (*end)
        ++end;
    while ((*end++ = *source++) != 0) {
    }
    return destination;
}

// Upper-cases the ASCII letters of a string in place; returns the string.
char *engine_ascii_to_upper_in_place(char *text)
{
    for (char *p = text; *p; ++p)
        if (*p >= 'a' && *p <= 'z')
            *p -= CASE_DISTANCE;
    return text;
}

// stricmp for ASCII: compares after lower-casing both sides.
int engine_compare_ascii_ignoring_case(const char *left, const char *right)
{
    char a, b;
    do {
        a = ascii_lower(*left++);
        b = ascii_lower(*right++);
        if (!a)
            break;
    } while (a == b);
    return a - b;
}

// strncat-like: appends at most `count` characters of `source` and always terminates; a zero
// count leaves `destination` untouched.
char *engine_string_append_counted(char *destination, const char *source, unsigned count)
{
    if (count) {
        char *end = destination;
        while (*end++) {
        }
        --end;
        while ((*end++ = *source++) != 0) {
            if (--count == 0) {
                *end = 0;
                break;
            }
        }
    }
    return destination;
}
