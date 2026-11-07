/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RECT_LIST_H
#define RUNTIME_RECT_LIST_H

/* Rect lists (primitive type 17, DI_PT_RECTLIST): each three vertices make a screen-aligned
   rectangle, and the GPU makes its fourth corner. Vulkan has no such primitive, so the renderer
   draws each rect's three vertices as a triangle through a geometry stage that adds the fourth
   (runtime/renderer/rect_list.c).

   Which vertex is the corner: the one whose x matches one neighbour's and whose y matches the
   other's (the right angle); the fourth corner is the two others minus it, for the position and
   every output alike. That is how shadPS4's rect-list expansion (emit_spirv_quad_rect.cpp) finds
   it; this is our own code. The rectangle comes out as a strip: corner, the next vertex, the one
   after, then the fourth, so the two triangles share the diagonal from the corner's neighbours. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The geometry stage for a vertex shader with parameters 0 to parameters - 1 (vec4s at those
   locations) and clip_distances ClipDistance entries (0 for none). 0 and *spirv (free() it) with
   *word_count words, or -1 when the counts are out of range or there is no memory. */
int rt_rect_list_geometry(unsigned parameters, unsigned clip_distances, uint32_t **spirv, size_t *word_count);

enum { RT_RECT_LIST_MAX_PARAMETERS = 32, RT_RECT_LIST_MAX_CLIP_DISTANCES = 8 };

#ifdef __cplusplus
}
#endif

#endif
