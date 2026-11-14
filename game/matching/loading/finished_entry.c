// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for loading's finished entry copy (game/loading/finished_entry.cpp)
// and the load queue's report of a loaded request.

#define VTABLE_ADDRESS_POINT 2   // the vtable's functions start after two words
#define REPORT_LOADED 1
#define REQUEST_WORD_COUNT 4

typedef struct ResourceInfo ResourceInfo;

typedef struct {
    const void *const *vtable;
    ResourceInfo *info;
} ResourceHandle;

// A result base (four request words, a code and a word), then a handle to the request's info.
typedef struct {
    const void *const *vtable;
    unsigned long request_words[REQUEST_WORD_COUNT];
    int code;
    unsigned long request_word_0x20;
    ResourceHandle handle;
} FinishedEntry;

typedef struct LoadEventQueue LoadEventQueue;
typedef struct {
    void (*report)(LoadEventQueue *queue, unsigned long *request_words, int code, unsigned long word);
} LoadEventQueueVtable;
struct LoadEventQueue {
    const LoadEventQueueVtable *vtable;
};

extern const void *const finished_entry_vtable[];
extern const void *const load_handle_vtable[];
void result_copy(FinishedEntry *entry, const FinishedEntry *source);
void resource_handle_assign(ResourceHandle *handle, const ResourceHandle *source);

// Copies the result base, starts an empty load handle and assigns the source's handle to it.
void loading_finished_entry_copy_construct_02aa4ab0(FinishedEntry *entry, const FinishedEntry *source)
{
    result_copy(entry, source);
    entry->vtable = &finished_entry_vtable[VTABLE_ADDRESS_POINT];
    entry->handle.info = 0;
    entry->handle.vtable = &load_handle_vtable[VTABLE_ADDRESS_POINT];
    resource_handle_assign(&entry->handle, &source->handle);
}

// Reports a request through the queue's first vtable slot with code 1.
void loading_load_queue_report_loaded_02a692d0(LoadEventQueue *queue, unsigned long *request_words,
                                               unsigned long request_word_0x20)
{
    queue->vtable->report(queue, request_words, REPORT_LOADED, request_word_0x20);
}
