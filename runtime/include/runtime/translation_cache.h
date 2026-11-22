/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The translation cache: GCN-to-SPIR-V translations kept on disk from one run to the next
   (docs/RENDERER.md, "Pipeline compiles").

   Translating the shaders a boot meets took 1.35 to 1.57 s of the renderer's thread in the clinic walk,
   in single frames of up to 33 ms, and the same shaders come back in every run. The executor looks each
   new shader up here by a key that holds everything its translation depends on (the program's code,
   the callee's, every layout value, the translator's version), and keeps the translation's outputs
   under it. A record is appended to the file as soon as it is made, so a run ended by a timeout or a
   stop keeps what it translated.

   The file starts with a header naming the translator's version; a file of another version, or
   larger than TRANSLATION_FILE_MAX, is started again empty. Each record carries a checksum: the
   records from the first one that does not check (a run killed while writing) on are dropped. Keys are
   compared byte for byte, never by hash alone. Lookups and additions may come from any thread. */
#ifndef RUNTIME_TRANSLATION_CACHE_H
#define RUNTIME_TRANSLATION_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TRANSLATION_FILE_MAX = 256 << 20 };

typedef struct translation_cache translation_cache;

/* A key being built: the bytes added to it, in order. failed is set when memory ran out (the key then
   finds nothing and adds nothing). */
typedef struct {
    unsigned char *bytes;
    size_t size, capacity;
    int failed;
} translation_key;

void translation_key_add(translation_key *key, const void *bytes, size_t size);
void translation_key_free(translation_key *key);

/* The cache in the file at path, made (with its directories) when there is none. NULL for an empty
   path or a file that cannot be opened: every lookup then misses and additions are dropped. */
translation_cache *translation_cache_open(const char *path, const char *version);
void translation_cache_close(translation_cache *cache);

/* The value kept under key: 1 with *value (the cache's own bytes, valid until it closes) and *size,
   else 0. */
int translation_cache_find(translation_cache *cache, const translation_key *key, const void **value, size_t *size);
/* Keeps value under key, in memory and at the end of the file. A key already kept keeps its value. */
void translation_cache_add(translation_cache *cache, const translation_key *key, const void *value, size_t size);

/* How many records the cache holds, those read from its file and those added since. */
size_t translation_cache_count(const translation_cache *cache);

#ifdef __cplusplus
}
#endif

#endif
