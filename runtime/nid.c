/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/nid.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[5];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} sha1;

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(sha1 *s, const unsigned char *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5a827999;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ed9eba1;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
        else f = b ^ c ^ d, k = 0xca62c1d6;
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

static void sha1_update(sha1 *s, const void *data, size_t n)
{
    const unsigned char *p = data;
    s->length += n;
    while (n) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->block + s->used, p, take);
        s->used += take, p += take, n -= take;
        if (s->used == 64) sha1_block(s, s->block), s->used = 0;
    }
}

static void sha1_digest(sha1 *s, unsigned char out[20])
{
    const uint64_t bits = s->length * 8;
    const unsigned char one = 0x80, zero = 0;
    sha1_update(s, &one, 1);
    while (s->used != 56) sha1_update(s, &zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha1_update(s, len, 8);
    for (int i = 0; i < 20; i++) out[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

void rt_nid(const char *symbol, char out[RT_NID_SIZE])
{
    static const unsigned char suffix[16] = {0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde, 0xd8, 0xc1,
                                             0xe6, 0xb0, 0x39, 0xb1, 0xc3, 0xe5, 0x52, 0x30};
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    sha1 s = {{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0}, 0, {0}, 0};
    unsigned char digest[20], v[9] = {0};
    sha1_update(&s, symbol, strlen(symbol));
    sha1_update(&s, suffix, sizeof suffix);
    sha1_digest(&s, digest);
    for (int i = 0; i < 8; i++) v[i] = digest[7 - i];
    /* 64 bits as 11 base64 digits: ten full 6-bit groups, then the last 4 bits shifted up by 2 */
    for (int i = 0; i < 11; i++) {
        const int bit = 6 * i;
        unsigned int chunk = (unsigned int)v[bit / 8] << 8 | v[bit / 8 + 1];
        out[i] = alphabet[(chunk >> (10 - bit % 8)) & 0x3f];
    }
    out[11] = 0;
}
