// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md): writes a message to the C runtime's standard error stream.
// The stream is the runtime's exported _Stderr object, reached through the GOT.

typedef struct FILE FILE;
extern FILE _Stderr;
int fputs(const char *text, FILE *stream);

int engine_write_stderr(const char *text)
{
    fputs(text, &_Stderr);
    return 0;
}
