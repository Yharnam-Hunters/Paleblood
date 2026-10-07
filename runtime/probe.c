/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Call counting probes: which original functions run in a game run, without replacing them.

   BB_PROBES=<file>: one probe per line, "0xADDRESS LENGTH HEXBYTES" (tools/probe_list.py writes
   it): LENGTH (5..32) bytes of whole instructions at the function's entry that can run anywhere
   (no RIP-relative operands, no branches), and the bytes expected there. Each probe gets a stub
   within 2 GB of the image: "lock inc counter; the displaced instructions; jmp back", and the
   entry becomes "jmp stub" (5 bytes, the rest int3, never reached). A probe whose bytes differ
   (hooked, patched, wrong dump) is skipped with a message.
   BB_PROBE_OUT=<file> (default probes.txt): "0xADDRESS COUNT" per probe, rewritten every 2 s. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "runtime/guest.h"
#include "runtime/probe.h"

enum { MAX_PROBES = 4096, STUB_SIZE = 64, MAX_LEN = 32 };

static struct { uint32_t address; } probes[MAX_PROBES];
static uint64_t *counters;
static unsigned probe_count;
static const char *out_path;

static void *dump_loop(void *unused)
{
    (void)unused;
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", out_path);
    for (;;) {
        sleep(2);
        FILE *f = fopen(tmp, "w");
        if (!f)
            continue;
        for (unsigned i = 0; i < probe_count; i++)
            fprintf(f, "0x%08x %llu\n", probes[i].address,
                    (unsigned long long)__atomic_load_n(&counters[i], __ATOMIC_RELAXED));
        fclose(f);
        rename(tmp, out_path);
    }
    return NULL;
}

static int parse_hex(const char *s, unsigned char *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1)
            return -1;
        out[i] = (unsigned char)v;
    }
    return s[2 * n] == '\0' || s[2 * n] == '\n' ? 0 : -1;
}

static int within_2g(const unsigned char *from, const unsigned char *to)
{
    const int64_t d = (int64_t)(to - from);
    return d > -0x7fff0000LL && d < 0x7fff0000LL;
}

static void put_rel32(unsigned char *at, const unsigned char *next, const unsigned char *target)
{
    const int32_t d = (int32_t)(target - next);
    memcpy(at, &d, 4);
}

int rt_probes_install(unsigned char *image, uint64_t image_size)
{
    const char *path = getenv("BB_PROBES");
    if (!path || !*path)
        return 0;
    out_path = getenv("BB_PROBE_OUT");
    if (!out_path || !*out_path)
        out_path = "probes.txt";
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "probes: cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    /* Stubs and counters near the image (after it, else before it), so every rel32 reaches. */
    const size_t region = (size_t)MAX_PROBES * (STUB_SIZE + 8);
    unsigned char *mem = MAP_FAILED;
    unsigned char *end = image + ((image_size + 0xfffff) & ~(uint64_t)0xfffff);
    for (int step = 1; step <= 256 && mem == MAP_FAILED; step++) {
        const size_t off = (size_t)step * 0x1000000;   /* 16 MB steps, up to 4 GB away in all */
        unsigned char *candidates[2] = {end + off, image - off - region};
        for (int c = 0; c < 2 && mem == MAP_FAILED; c++) {
            unsigned char *hint = candidates[c];
            if (!within_2g(image, hint) || !within_2g(end, hint + region) || !within_2g(hint, image))
                continue;
            mem = mmap(hint, region, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        }
    }
    if (mem == MAP_FAILED) {
        fprintf(stderr, "probes: no memory within 2 GB of the image\n");
        fclose(f);
        return 1;
    }
    unsigned char *stubs = mem;
    counters = (uint64_t *)(mem + (size_t)MAX_PROBES * STUB_SIZE);
    char line[256];
    unsigned skipped = 0;
    while (fgets(line, sizeof line, f) && probe_count < MAX_PROBES) {
        unsigned address, len;
        char hexbytes[2 * MAX_LEN + 3];
        if (line[0] == '#' || sscanf(line, "%x %u %66s", &address, &len, hexbytes) != 3)
            continue;
        unsigned char expected[MAX_LEN];
        unsigned char *entry = image + (address - RT_EBOOT_BASE);
        if (len < 5 || len > MAX_LEN || address < RT_EBOOT_BASE || address - RT_EBOOT_BASE + len > image_size ||
            parse_hex(hexbytes, expected, len) || memcmp(entry, expected, len)) {
            fprintf(stderr, "probes: 0x%08x skipped (bytes differ or bad line)\n", address);
            skipped++;
            continue;
        }
        const unsigned i = probe_count++;
        unsigned char *s = stubs + (size_t)i * STUB_SIZE;
        unsigned char *counter = (unsigned char *)&counters[i];
        /* lock inc qword [rip+disp32] */
        s[0] = 0xf0; s[1] = 0x48; s[2] = 0xff; s[3] = 0x05;
        put_rel32(s + 4, s + 8, counter);
        memcpy(s + 8, expected, len);
        s[8 + len] = 0xe9;
        put_rel32(s + 9 + len, s + 13 + len, entry + len);
        probes[i].address = address;
        unsigned char patch[MAX_LEN];
        memset(patch, 0xcc, len);
        patch[0] = 0xe9;
        put_rel32(patch + 1, entry + 5, s);
        /* The loader calls bbgame_init before its final page protections: the image is still
           writable here, as for install_hook. */
        memcpy(entry, patch, len);
    }
    fclose(f);
    __builtin___clear_cache((char *)stubs, (char *)stubs + (size_t)probe_count * STUB_SIZE);
    pthread_t t;
    pthread_create(&t, NULL, dump_loop, NULL);
    pthread_detach(t);
    fprintf(stderr, "probes: %u installed, %u skipped, counts in %s\n", probe_count, skipped, out_path);
    return 0;
}
