// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for small pieces of the menu's Scaleform code
// (game/menu/scaleform_glue.cpp).

#define LOADED 4
#define FILE_COUNT 3
#define EXTRA_FILE 2         // the third file goes to the build as its extra
#define NAME_INLINE_CAPACITY 16

typedef struct {
    char unknown_0x00[0x78];
    volatile unsigned char state;   // read as volatile: the loading side sets it
    char unknown_0x79[0xf];
    const void *data;
    unsigned long size;
} LoadingFile;

typedef struct {
    LoadingFile *file;
    unsigned long size;
} FileSlot;

typedef struct {
    char unknown_0x00[0x8];
    union {
        char inline_text[NAME_INLINE_CAPACITY];
        char *heap;
    };
    unsigned long size;
    unsigned long capacity;
    char unknown_0x28[0x10];
} NarrowString;

typedef struct {
    char unknown_0x00[0x60];
    unsigned char step_done;
    char unknown_0x61[0xbf];
    NarrowString name;
    char unknown_0x158[0x3];
    unsigned char build_failed;
    char unknown_0x15c[0xc];
    FileSlot files[FILE_COUNT];
    char unknown_0x198[0x2];
    unsigned char build_declined;
} FileSetLoader;

void file_set_loader_finish(FileSetLoader *loader);
int file_set_build(const void *data, unsigned long size, const char *name, int flag, const void *extra,
                   int extra_size);

void menu_file_set_loader_wait_02387050(FileSetLoader *loader)
{
    _Bool all_loaded = 1;
    int i;
    for (i = 0; i < FILE_COUNT; ++i) {
        FileSlot *slot = &loader->files[i];
        if (!slot->file)
            continue;
        if (slot->file->state != LOADED) {
            all_loaded = 0;
            continue;
        }
        slot->size = slot->file->size;
    }
    if (all_loaded) {
        file_set_loader_finish(loader);
        loader->step_done = 1;
    }
}

void menu_file_set_loader_build_02387650(FileSetLoader *loader)
{
    unsigned long size = 0;
    const void *data = 0, *extra = 0;
    int extra_size = 0, result;
    if (loader->files[0].file) {
        data = loader->files[0].file->data;
        size = loader->files[0].file->size;
    }
    if (loader->files[EXTRA_FILE].file) {
        extra = loader->files[EXTRA_FILE].file->data;
        extra_size = (int)loader->files[EXTRA_FILE].file->size;
    }
    if (data && size) {
        result = file_set_build(data, size, loader->name.capacity >= NAME_INLINE_CAPACITY ? loader->name.heap : loader->name.inline_text,
                                1, extra, extra_size);
        if (result == 1)
            goto done;
        if (result == 0) {
            loader->build_declined = 1;
            goto done;
        }
    }
    loader->build_failed = 1;
done:
    file_set_loader_finish(loader);
    loader->step_done = 1;
}
