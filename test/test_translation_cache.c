/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The translation cache (runtime/renderer/translation_cache.c): a value kept under a key is found again
   by the same key bytes, in the same cache and in the next one opened on its file; a key differing in
   one byte, of another length, or a file of another version finds nothing; a torn last record (a run
   killed while writing) is dropped and the records before it kept. */
#include "runtime/translation_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { KEYS = 3000 }; /* enough to grow the index several times */

static translation_key key_of(unsigned n, size_t length)
{
    translation_key key = {0};
    for (size_t i = 0; i < length; i++) {
        const unsigned char byte = (unsigned char)(n * 31 + i);
        translation_key_add(&key, &byte, 1);
    }
    translation_key_add(&key, &n, sizeof n);
    return key;
}

/* Whether the cache holds value n (n repeated n % 7 + 1 times) under key n. */
static int holds(translation_cache *cache, unsigned n)
{
    translation_key key = key_of(n, 40 + n % 50);
    const void *value = NULL;
    size_t size = 0;
    const int found = translation_cache_find(cache, &key, &value, &size);
    translation_key_free(&key);
    if (!found || size != (n % 7 + 1) * sizeof n) return 0;
    for (size_t i = 0; i < size / sizeof n; i++) {
        unsigned word;
        memcpy(&word, (const unsigned char *)value + i * sizeof n, sizeof word);
        if (word != n) return 0;
    }
    return 1;
}

static void add(translation_cache *cache, unsigned n)
{
    translation_key key = key_of(n, 40 + n % 50);
    unsigned words[8];
    for (unsigned i = 0; i < 8; i++) words[i] = n;
    translation_cache_add(cache, &key, words, (n % 7 + 1) * sizeof n);
    translation_key_free(&key);
}

/* translation_cache_each's visits: whether each key came as key_of(next) in turn. */
typedef struct {
    unsigned next;
    int in_order;
} visits;

static void visit(void *context, const void *key, size_t size)
{
    visits *v = context;
    translation_key expected = key_of(v->next, 40 + v->next % 50);
    v->in_order &= size == expected.size && !memcmp(key, expected.bytes, size);
    translation_key_free(&expected);
    v->next++;
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) ? -1 : (long)st.st_size;
}

int main(void)
{
    char directory[] = "/tmp/pb-translations-XXXXXX", path[96];
    if (!mkdtemp(directory)) return 1;
    snprintf(path, sizeof path, "%s/sub/translations.bin", directory); /* its directory is made */

    CHECK(translation_cache_open("", "v1") == NULL);
    CHECK(translation_cache_open(NULL, "v1") == NULL);

    translation_cache *cache = translation_cache_open(path, "v1");
    CHECK(cache != NULL);
    for (unsigned n = 0; n < KEYS; n++) add(cache, n);
    add(cache, 5); /* again: kept once */
    CHECK(translation_cache_count(cache) == KEYS);
    int all = 1;
    for (unsigned n = 0; n < KEYS; n++) all &= holds(cache, n);
    CHECK(all);
    CHECK(!holds(cache, KEYS));
    translation_key one_byte_off = key_of(7, 40 + 7 % 50), shorter = key_of(7, 40 + 7 % 50 - 1);
    one_byte_off.bytes[3] ^= 1;
    const void *value;
    size_t size;
    CHECK(!translation_cache_find(cache, &one_byte_off, &value, &size));
    CHECK(!translation_cache_find(cache, &shorter, &value, &size));
    translation_key_free(&one_byte_off), translation_key_free(&shorter);
    translation_cache_close(cache);

    /* the next run reads the file */
    cache = translation_cache_open(path, "v1");
    CHECK(translation_cache_count(cache) == KEYS);
    /* its keys in the order kept */
    visits seen = {0, 1};
    translation_cache_each(cache, visit, &seen);
    CHECK(seen.in_order && seen.next == KEYS);
    all = 1;
    for (unsigned n = 0; n < KEYS; n++) all &= holds(cache, n);
    CHECK(all);
    translation_cache_close(cache);

    /* a torn last record: the ones before it stay, and the file is cut back to them */
    const long whole = file_size(path);
    CHECK(truncate(path, whole - 3) == 0);
    cache = translation_cache_open(path, "v1");
    CHECK(translation_cache_count(cache) == KEYS - 1);
    CHECK(holds(cache, KEYS - 2) && !holds(cache, KEYS - 1));
    add(cache, KEYS - 1); /* appended after the cut */
    add(cache, 9); /* kept already: not visited again */
    seen = (visits){0, 1}; /* those read from the file, then the one added since */
    translation_cache_each(cache, visit, &seen);
    CHECK(seen.in_order && seen.next == KEYS);
    translation_cache_close(cache);
    cache = translation_cache_open(path, "v1");
    CHECK(translation_cache_count(cache) == KEYS && holds(cache, KEYS - 1));
    translation_cache_close(cache);

    /* a changed byte in a record fails its checksum: it and the records after it go */
    FILE *f = fopen(path, "r+b");
    CHECK(f != NULL);
    if (f) {
        fseek(f, whole / 2, SEEK_SET);
        const int byte = fgetc(f);
        fseek(f, whole / 2, SEEK_SET);
        fputc(byte ^ 0x40, f);
        fclose(f);
    }
    cache = translation_cache_open(path, "v1");
    CHECK(translation_cache_count(cache) > 0 && translation_cache_count(cache) < KEYS);
    CHECK(holds(cache, 0));
    translation_cache_close(cache);

    /* another version starts the file again */
    cache = translation_cache_open(path, "v2");
    CHECK(cache != NULL && translation_cache_count(cache) == 0 && !holds(cache, 0));
    translation_cache_close(cache);
    CHECK(file_size(path) < 64);

    unlink(path);
    char sub[96];
    snprintf(sub, sizeof sub, "%s/sub", directory);
    rmdir(sub);
    rmdir(directory);
    if (failures) fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
