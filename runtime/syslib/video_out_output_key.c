/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Release the output configuration associated with a matching key. The lower
   helper owns the request result; cleanup always clears its key on return. */
#include "runtime/video_out_output_key.h"
#include "runtime/process.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(rt_video_out_output_configuration) == 32, "configuration size");
_Static_assert(offsetof(rt_video_out_output_configuration, size) == 0, "size offset");
_Static_assert(offsetof(rt_video_out_output_configuration, variant_04) == 4, "variant04 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, variant_05) == 5, "variant05 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, variant_06) == 6, "variant06 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, variant_07) == 7, "variant07 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, parameter_08) == 8, "parameter08 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, parameter_10) == 16, "parameter10 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, variant_18) == 24, "variant18 offset");
_Static_assert(offsetof(rt_video_out_output_configuration, required_ff) == 25, "sentinel offset");

const rt_video_out_output_key_bindings *rt_video_out_output_key_imports;

int32_t rt_video_out_clear_matching_output_key(uint32_t key)
{
    const rt_video_out_output_key_bindings *imports = rt_video_out_output_key_imports;
    if (!imports || !imports->key_word)
        rt_fatal("VideoOut output key cleanup", "the actual key word is unbound");
    if (*imports->key_word != key)
        return 0;
    if (!imports->companion_word || !imports->configure_output)
        rt_fatal("VideoOut output key cleanup", "the actual companion word or output helper is unbound");

    const uint32_t companion = *imports->companion_word;
    _Alignas(32) rt_video_out_output_configuration configuration;
    memset(&configuration, UINT8_MAX, sizeof configuration);
    configuration.size = sizeof configuration;
    (void)imports->configure_output(0, &configuration, NULL, 0, 0, key, companion);
    /* Do not retest the key or the result: the helper may have changed either. */
    *imports->key_word = 0;
    return 0;
}
