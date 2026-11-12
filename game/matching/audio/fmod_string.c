// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the string copies linked in with the FMOD audio library:
// classic strcpy and strncpy-without-padding on plain `char`.

// strcpy: copies through the terminator; returns `destination`.
char *audio_fmod_string_copy(char *destination, const char *source)
{
    char *out = destination;
    while ((*out++ = *source++) != 0) {
    }
    return destination;
}

// Copies at most `count` characters, stopping after the terminator; unlike strncpy it does not
// pad the rest with zeros. Returns `destination`.
char *audio_fmod_string_copy_counted(char *destination, const char *source, unsigned count)
{
    char *out = destination;
    while (count-- && (*out++ = *source++) != 0) {
    }
    return destination;
}
