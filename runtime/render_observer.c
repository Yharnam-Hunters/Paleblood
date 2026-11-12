/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The game library's way to the renderer's record (runtime/render_observer.h): the record's
   functions are looked up by name in the host process once, and each observer call forwards to its
   function, or does nothing when the host has none. */
#include "runtime/render_observer.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*fatal_fn)(const char *function, const char *message);

static struct {
    __typeof__(rt_render_record_color_target) *color_target;
    __typeof__(rt_render_record_viewport) *viewport;
    __typeof__(rt_render_record_draw_auto) *draw_auto;
    __typeof__(rt_render_record_vertex_shader) *vertex_shader;
    __typeof__(rt_render_record_pixel_shader) *pixel_shader;
    __typeof__(rt_render_record_user_data) *user_data;
    __typeof__(rt_render_record_register) *reg;
    __typeof__(rt_render_record_instances) *instances;
    __typeof__(rt_render_record_dispatch) *dispatch;
    __typeof__(rt_render_record_indirect_base) *indirect_base;
    __typeof__(rt_render_record_dispatch_indirect) *dispatch_indirect;
    __typeof__(rt_render_record_index_base) *index_base;
    __typeof__(rt_render_record_index_size) *index_size;
    __typeof__(rt_render_record_index_type) *index_type;
    __typeof__(rt_render_record_draw_index_offset) *draw_index_offset;
    fatal_fn fatal;
} record;

static pthread_once_t looked_up = PTHREAD_ONCE_INIT;

static void *host_function(const char *name)
{
    return dlsym(RTLD_DEFAULT, name);
}

static void look_up(void)
{
    record.color_target = (__typeof__(record.color_target))host_function("rt_render_record_color_target");
    record.viewport = (__typeof__(record.viewport))host_function("rt_render_record_viewport");
    record.draw_auto = (__typeof__(record.draw_auto))host_function("rt_render_record_draw_auto");
    record.vertex_shader = (__typeof__(record.vertex_shader))host_function("rt_render_record_vertex_shader");
    record.pixel_shader = (__typeof__(record.pixel_shader))host_function("rt_render_record_pixel_shader");
    record.user_data = (__typeof__(record.user_data))host_function("rt_render_record_user_data");
    record.reg = (__typeof__(record.reg))host_function("rt_render_record_register");
    record.instances = (__typeof__(record.instances))host_function("rt_render_record_instances");
    record.dispatch = (__typeof__(record.dispatch))host_function("rt_render_record_dispatch");
    record.indirect_base = (__typeof__(record.indirect_base))host_function("rt_render_record_indirect_base");
    record.dispatch_indirect = (__typeof__(record.dispatch_indirect))host_function("rt_render_record_dispatch_indirect");
    record.index_base = (__typeof__(record.index_base))host_function("rt_render_record_index_base");
    record.index_size = (__typeof__(record.index_size))host_function("rt_render_record_index_size");
    record.index_type = (__typeof__(record.index_type))host_function("rt_render_record_index_type");
    record.draw_index_offset = (__typeof__(record.draw_index_offset))host_function("rt_render_record_draw_index_offset");
    record.fatal = (fatal_fn)host_function("rt_fatal");
}

int rt_render_observer_available(void)
{
    pthread_once(&looked_up, look_up);
    return record.draw_auto != NULL;
}

/* The record refused a call: the replacement handed it something it does not take. */
static void refused(const char *call, const char *reason)
{
    if (record.fatal) record.fatal(call, reason);
    fprintf(stderr, "runtime: %s: %s\n", call, reason);
    abort();
}

#define FORWARD(name, call, ...)                                                                   \
    do {                                                                                           \
        pthread_once(&looked_up, look_up);                                                         \
        if (!record.name) return;                                                                  \
        char error[256] = "";                                                                      \
        if (record.name(__VA_ARGS__, error, sizeof error)) refused(call, error);                   \
    } while (0)

void rt_observe_color_target(uint64_t buffer, uint32_t offset, unsigned slot, const uint32_t words[RT_COLOR_TARGET_WORDS])
{
    FORWARD(color_target, "render observer: colour target", buffer, offset, slot, words);
}

void rt_observe_viewport(uint64_t buffer, uint32_t offset, unsigned index, float zmin, float zmax, const float scale[3],
                         const float translate[3])
{
    FORWARD(viewport, "render observer: viewport", buffer, offset, index, zmin, zmax, scale, translate);
}

void rt_observe_draw_auto(uint64_t buffer, uint32_t offset, uint32_t index_count)
{
    FORWARD(draw_auto, "render observer: draw", buffer, offset, index_count);
}

void rt_observe_vertex_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_VERTEX_SHADER_WORDS])
{
    FORWARD(vertex_shader, "render observer: vertex shader", buffer, offset, words);
}

void rt_observe_pixel_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_PIXEL_SHADER_WORDS])
{
    FORWARD(pixel_shader, "render observer: pixel shader", buffer, offset, words);
}

void rt_observe_user_data(uint64_t buffer, uint32_t offset, rt_shader_stage stage, unsigned slot, const uint32_t *words,
                          unsigned count)
{
    FORWARD(user_data, "render observer: user data", buffer, offset, stage, slot, words, count);
}

void rt_observe_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value)
{
    FORWARD(reg, "render observer: register", buffer, offset, address, value);
}

void rt_observe_instances(uint64_t buffer, uint32_t offset, uint32_t count)
{
    FORWARD(instances, "render observer: instances", buffer, offset, count);
}

void rt_observe_dispatch(uint64_t buffer, uint32_t offset, const uint32_t groups[3])
{
    FORWARD(dispatch, "render observer: dispatch", buffer, offset, groups);
}

void rt_observe_indirect_base(uint64_t buffer, uint32_t offset, uint64_t address)
{
    FORWARD(indirect_base, "render observer: indirect base", buffer, offset, address);
}

void rt_observe_dispatch_indirect(uint64_t buffer, uint32_t offset, uint32_t data_offset)
{
    FORWARD(dispatch_indirect, "render observer: indirect dispatch", buffer, offset, data_offset);
}

void rt_observe_index_base(uint64_t buffer, uint32_t offset, uint64_t address)
{
    FORWARD(index_base, "render observer: index base", buffer, offset, address);
}

void rt_observe_index_size(uint64_t buffer, uint32_t offset, uint32_t count)
{
    FORWARD(index_size, "render observer: index buffer size", buffer, offset, count);
}

void rt_observe_index_type(uint64_t buffer, uint32_t offset, uint32_t type)
{
    FORWARD(index_type, "render observer: index type", buffer, offset, type);
}

void rt_observe_draw_index_offset(uint64_t buffer, uint32_t offset, uint32_t first, uint32_t count)
{
    FORWARD(draw_index_offset, "render observer: indexed draw", buffer, offset, first, count);
}
