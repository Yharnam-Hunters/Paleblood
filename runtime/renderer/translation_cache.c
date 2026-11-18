/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The translation cache (runtime/translation_cache.h). */
#include "runtime/translation_cache.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* The file: FILE_MAGIC, the version's length (32 bits) and bytes, then records, each a record_header
   followed by its key and value. */
static const char FILE_MAGIC[8] = {'P', 'B', 'X', 'L', 'A', 'T', 'E', '1'};
enum { VERSION_MAX = 256, FIRST_SLOTS = 1024 };

typedef struct {
    uint32_t key_size, value_size;
    uint64_t checksum; /* FNV-1a over the key's bytes, then the value's */
} record_header;

typedef struct {
    uint64_t hash; /* the key's FNV-1a; 0 marks a free slot (a key hashing to 0 is kept as 1) */
    const unsigned char *key, *value;
    uint32_t key_size, value_size;
} slot;

struct translation_cache {
    pthread_mutex_t lock;
    int fd;             /* the file, opened for appending; -1 once it can take no more */
    size_t file_size;
    unsigned char *read_bytes; /* the file as read when the cache opened: the records found there point into it */
    unsigned char **added;     /* the records added since, one allocation each */
    size_t added_count, added_capacity;
    slot *slots;
    size_t slot_count, used;
};

static uint64_t fnv1a(uint64_t hash, const unsigned char *bytes, size_t size)
{
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    return hash;
}

static const uint64_t FNV_OFFSET = 0xcbf29ce484222325ull;

static uint64_t key_hash(const unsigned char *key, size_t size)
{
    const uint64_t hash = fnv1a(FNV_OFFSET, key, size);
    return hash ? hash : 1;
}

void translation_key_add(translation_key *key, const void *bytes, size_t size)
{
    if (key->failed || !size) return;
    if (key->size + size > key->capacity) {
        size_t capacity = key->capacity ? key->capacity : 256;
        while (capacity < key->size + size) capacity *= 2;
        unsigned char *grown = realloc(key->bytes, capacity);
        if (!grown) {
            key->failed = 1;
            return;
        }
        key->bytes = grown, key->capacity = capacity;
    }
    memcpy(key->bytes + key->size, bytes, size);
    key->size += size;
}

void translation_key_free(translation_key *key)
{
    free(key->bytes);
    *key = (translation_key){0};
}

/* The slot holding key, or the free slot it would take. */
static slot *slot_for(const translation_cache *cache, uint64_t hash, const unsigned char *key, size_t size)
{
    for (size_t i = hash & (cache->slot_count - 1);; i = (i + 1) & (cache->slot_count - 1)) {
        slot *s = &cache->slots[i];
        if (!s->hash || (s->hash == hash && s->key_size == size && !memcmp(s->key, key, size))) return s;
    }
}

/* Puts a record in the index unless its key is there already; 0, or -1 without memory. */
static int index_record(translation_cache *cache, const unsigned char *key, uint32_t key_size, const unsigned char *value, uint32_t value_size)
{
    if (2 * (cache->used + 1) > cache->slot_count) { /* at most half full, so a probe ends soon */
        const size_t count = cache->slot_count ? 2 * cache->slot_count : FIRST_SLOTS;
        slot *old = cache->slots;
        const size_t old_count = cache->slot_count;
        cache->slots = calloc(count, sizeof *cache->slots);
        if (!cache->slots) {
            cache->slots = old;
            return -1;
        }
        cache->slot_count = count;
        for (size_t i = 0; i < old_count; i++)
            if (old[i].hash) *slot_for(cache, old[i].hash, old[i].key, old[i].key_size) = old[i];
        free(old);
    }
    const uint64_t hash = key_hash(key, key_size);
    slot *s = slot_for(cache, hash, key, key_size);
    if (s->hash) return 0;
    *s = (slot){.hash = hash, .key = key, .value = value, .key_size = key_size, .value_size = value_size};
    cache->used++;
    return 0;
}

static int write_all(int fd, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    while (size) {
        const ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n, size -= (size_t)n;
    }
    return 0;
}

static size_t header_size(const char *version) { return sizeof FILE_MAGIC + sizeof(uint32_t) + strlen(version); }

/* Whether bytes begin with the header for version. */
static int header_matches(const unsigned char *bytes, size_t size, const char *version)
{
    const uint32_t length = (uint32_t)strlen(version);
    uint32_t stored;
    if (size < header_size(version) || memcmp(bytes, FILE_MAGIC, sizeof FILE_MAGIC)) return 0;
    memcpy(&stored, bytes + sizeof FILE_MAGIC, sizeof stored);
    return stored == length && !memcmp(bytes + sizeof FILE_MAGIC + sizeof stored, version, length);
}

/* Indexes the records after the header; the file's length up to the last whole record that checks. */
static size_t read_records(translation_cache *cache, const unsigned char *bytes, size_t size, size_t at)
{
    while (size - at >= sizeof(record_header)) {
        record_header h;
        memcpy(&h, bytes + at, sizeof h);
        const size_t body = (size_t)h.key_size + h.value_size;
        if (!h.key_size || body > size - at - sizeof h) break;
        const unsigned char *key = bytes + at + sizeof h, *value = key + h.key_size;
        if (fnv1a(fnv1a(FNV_OFFSET, key, h.key_size), value, h.value_size) != h.checksum) break;
        if (index_record(cache, key, h.key_size, value, h.value_size)) break;
        at += sizeof h + body;
    }
    return at;
}

/* Makes the directories above path (as mkdir -p would), ignoring those already there. */
static void make_parent_directories(const char *path)
{
    char *parent = strdup(path);
    if (!parent) return;
    for (char *slash = strchr(parent + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        mkdir(parent, 0755);
        *slash = '/';
    }
    free(parent);
}

/* Reads the file (under its lock, so another run's appends are whole), indexing its records, or starts
   it again with the header when it is of another version, too large, or unreadable. 0, or -1. */
static int load_file(translation_cache *cache, const char *version)
{
    struct stat st;
    if (fstat(cache->fd, &st)) return -1;
    size_t size = (size_t)st.st_size, good = 0;
    if (st.st_size > 0 && st.st_size <= TRANSLATION_FILE_MAX && (cache->read_bytes = malloc(size))) {
        size_t got = 0;
        while (got < size) {
            const ssize_t n = pread(cache->fd, cache->read_bytes + got, size - got, (off_t)got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (got == size && header_matches(cache->read_bytes, size, version))
            good = read_records(cache, cache->read_bytes, size, header_size(version));
    }
    if (good && good == size) {
        cache->file_size = size;
        return 0;
    }
    if (good) { /* a torn last record: cut it off */
        cache->file_size = good;
        return ftruncate(cache->fd, (off_t)good);
    }
    if (ftruncate(cache->fd, 0)) return -1;
    const uint32_t length = (uint32_t)strlen(version);
    if (write_all(cache->fd, FILE_MAGIC, sizeof FILE_MAGIC) || write_all(cache->fd, &length, sizeof length) ||
        write_all(cache->fd, version, length))
        return -1;
    cache->file_size = header_size(version);
    return 0;
}

translation_cache *translation_cache_open(const char *path, const char *version)
{
    if (!path || !*path || !version || strlen(version) > VERSION_MAX) return NULL;
    translation_cache *cache = calloc(1, sizeof *cache);
    if (!cache) return NULL;
    pthread_mutex_init(&cache->lock, NULL);
    make_parent_directories(path);
    cache->fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (cache->fd < 0) {
        translation_cache_close(cache);
        return NULL;
    }
    flock(cache->fd, LOCK_EX);
    const int loaded = load_file(cache, version);
    flock(cache->fd, LOCK_UN);
    if (loaded) {
        translation_cache_close(cache);
        return NULL;
    }
    return cache;
}

void translation_cache_close(translation_cache *cache)
{
    if (!cache) return;
    if (cache->fd >= 0) close(cache->fd);
    for (size_t i = 0; i < cache->added_count; i++) free(cache->added[i]);
    free(cache->added);
    free(cache->slots);
    free(cache->read_bytes);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

int translation_cache_find(translation_cache *cache, const translation_key *key, const void **value, size_t *size)
{
    if (!cache || key->failed || !key->size) return 0;
    pthread_mutex_lock(&cache->lock);
    const slot *s = cache->slots ? slot_for(cache, key_hash(key->bytes, key->size), key->bytes, key->size) : NULL;
    const int found = s && s->hash;
    if (found) *value = s->value, *size = s->value_size;
    pthread_mutex_unlock(&cache->lock);
    return found;
}

/* A record for the file: its header, key and value in one allocation, or NULL without memory. */
static unsigned char *make_record(const translation_key *key, const void *value, size_t size)
{
    unsigned char *record = malloc(sizeof(record_header) + key->size + size);
    if (!record) return NULL;
    unsigned char *const key_bytes = record + sizeof(record_header), *const value_bytes = key_bytes + key->size;
    memcpy(key_bytes, key->bytes, key->size);
    memcpy(value_bytes, value, size);
    const record_header h = {.key_size = (uint32_t)key->size, .value_size = (uint32_t)size,
                             .checksum = fnv1a(fnv1a(FNV_OFFSET, key_bytes, key->size), value_bytes, size)};
    memcpy(record, &h, sizeof h);
    return record;
}

/* Appends a record to the file, whole between another run's appends (the file's lock). A failed write
   leaves the file closed: a half-written record fails its checksum when the file is next read. */
static void append_record(translation_cache *cache, const unsigned char *record, size_t record_size)
{
    if (cache->fd < 0 || cache->file_size + record_size > TRANSLATION_FILE_MAX) return;
    flock(cache->fd, LOCK_EX);
    const int failed = write_all(cache->fd, record, record_size);
    flock(cache->fd, LOCK_UN);
    if (failed) {
        close(cache->fd);
        cache->fd = -1;
    } else {
        cache->file_size += record_size;
    }
}

/* Keeps a new record in memory; 0, or -1 without memory. */
static int keep_record(translation_cache *cache, unsigned char *record)
{
    if (cache->added_count == cache->added_capacity) {
        const size_t capacity = cache->added_capacity ? 2 * cache->added_capacity : 256;
        unsigned char **grown = realloc(cache->added, capacity * sizeof *grown);
        if (!grown) return -1;
        cache->added = grown, cache->added_capacity = capacity;
    }
    record_header h;
    memcpy(&h, record, sizeof h);
    const unsigned char *key = record + sizeof h;
    if (index_record(cache, key, h.key_size, key + h.key_size, h.value_size)) return -1;
    cache->added[cache->added_count++] = record;
    return 0;
}

void translation_cache_add(translation_cache *cache, const translation_key *key, const void *value, size_t size)
{
    if (!cache || key->failed || !key->size || key->size > UINT32_MAX || size > UINT32_MAX) return;
    pthread_mutex_lock(&cache->lock);
    const int kept = cache->slots && slot_for(cache, key_hash(key->bytes, key->size), key->bytes, key->size)->hash;
    unsigned char *record = kept ? NULL : make_record(key, value, size);
    if (record && keep_record(cache, record) == 0)
        append_record(cache, record, sizeof(record_header) + key->size + size);
    else
        free(record);
    pthread_mutex_unlock(&cache->lock);
}

size_t translation_cache_count(const translation_cache *cache) { return cache ? cache->used : 0; }
