/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The audio job manager's ATRAC9 decoder (runtime/syslib/audio_ajm.c): LibAtrac9 by default. */
#ifndef RUNTIME_AJM_H
#define RUNTIME_AJM_H

#include <stdint.h>

typedef struct rt_ajm_decoder {
    /* A decoder for a 4-byte ATRAC9 configuration word, and its channel count; NULL if invalid. */
    void *(*open)(const uint8_t config[4], uint32_t *channels);
    /* Decodes one frame into interleaved 16-bit PCM; *used is the bytes it read. 0, or an error. */
    int (*decode)(void *decoder, const uint8_t *in, int16_t *pcm, int *used);
    void (*release)(void *decoder);
} rt_ajm_decoder;

/* Replaces the decoder (for tests, which have no ATRAC9 data); NULL restores LibAtrac9. */
void rt_ajm_set_decoder(const rt_ajm_decoder *replacement);

#endif
