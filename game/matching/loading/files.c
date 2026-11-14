// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for loading's file helpers over the kernel and stdio.

#define READ_FAILED (-1)
#define STREAM_COUNT 3

typedef struct FILE FILE;

typedef struct {
    int descriptor;
} KernelFile;

typedef struct {
    FILE *files[STREAM_COUNT];
} StdioFiles;

long sceKernelRead(int descriptor, void *buffer, unsigned long size);
int fclose(FILE *file);

// Reads from the file; -1 on a kernel error, else 0 and the byte count when asked for it.
int loading_kernel_file_read(const KernelFile *file, void *buffer, unsigned long size,
                             long *bytes_read)
{
    long count = sceKernelRead(file->descriptor, buffer, size);
    if (count < 0)
        return READ_FAILED;
    if (bytes_read)
        *bytes_read = count;
    return 0;
}

// Closes the open streams, last first, and forgets them.
void loading_stdio_close_files(StdioFiles *stdio)
{
    int index;
    for (index = STREAM_COUNT - 1; index >= 0; index--)
        if (stdio->files[index]) {
            fclose(stdio->files[index]);
            stdio->files[index] = 0;
        }
}
