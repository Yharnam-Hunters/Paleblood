/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A host without record exports can call every observer without a provider or side effects. */
#include "runtime/render_observer.h"
#include <stdio.h>
int main(void)
{
    if (rt_render_observer_available()) return 1;
    rt_observe_index_type(0, 0, 0xffffffffu);
    rt_observe_index_base(0, 0, 0xffffffffffffffffull);
    rt_observe_index_buffer_size(0, 0, 0xffffffffu);
    rt_observe_draw_indexed(0, 0, 0xffffffffu, 0xffffffffu, 0xffffffffffffffffull, 0xffffffffu, 0xffffffffu);
    rt_observe_draw_indexed_offset(0, 0, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
    puts("index observer: absent host provider is optional");
    return 0;
}
