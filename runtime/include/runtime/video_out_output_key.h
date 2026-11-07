/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_VIDEO_OUT_OUTPUT_KEY_H
#define RUNTIME_VIDEO_OUT_OUTPUT_KEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The configuration prefix consumed by the output helper. The variant and
   parameter meanings are not established; cleanup supplies their FF sentinels. */
typedef struct {
    uint32_t size;
    uint8_t variant_04, variant_05, variant_06, variant_07;
    uint64_t parameter_08, parameter_10;
    uint8_t variant_18, required_ff[7];
} rt_video_out_output_configuration;

typedef int32_t (*rt_video_out_configure_output_fn)(uint32_t selector,
    const rt_video_out_output_configuration *configuration, const void *optional,
    int32_t level, uint32_t flags, uint32_t key, uint32_t companion);

/* The module owner supplies the actual words and output helper. The binding
   descriptor stays fixed during a call; the pointed words remain mutable.
   This interface does not install a native export or supply a provider. */
typedef struct {
    uint32_t *key_word;
    uint32_t *companion_word;
    rt_video_out_configure_output_fn configure_output;
} rt_video_out_output_key_bindings;

extern const rt_video_out_output_key_bindings *rt_video_out_output_key_imports;

/* A matching key requests the sentinel configuration, then clears the key even
   if that request returns an error. A nonmatching key leaves both words alone.
   The companion is captured before the call; cleanup itself never writes it. */
int32_t rt_video_out_clear_matching_output_key(uint32_t key);

#ifdef __cplusplus
}
#endif
#endif
