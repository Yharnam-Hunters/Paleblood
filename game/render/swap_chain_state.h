// SPDX-License-Identifier: GPL-2.0-or-later
// Fields observed by the lower-level swap-chain present routine. Opaque parts remain owned
// by their original helpers; these layouts do not specify a pixel format or a GPU tile layout.
#pragma once

#include "swap_chain.h"

namespace render {

struct PresentBufferRecord {
    uint32_t value;
    uint8_t unknown_0x04[0x28];
};
static_assert(sizeof(PresentBufferRecord) == 0x2c);

struct GpuCommandState {
    uint8_t unknown_0x0000[0xb548];
};
struct GpuPresentPreparation {
    uint8_t unknown_0x0000[0x1c0];
};
struct GpuOwnerLink {
    uint8_t unknown_0x00[0x8];
    void *queue;
};
static_assert(offsetof(GpuOwnerLink, queue) == 0x8);
struct PresentGpuContext {
    uint8_t unknown_0x00[0x8];
    GpuOwnerLink *owner;
    GpuCommandState commands;
    GpuPresentPreparation preparation;
    uint64_t owner_thread;
};
static_assert(offsetof(PresentGpuContext, owner) == 0x8);
static_assert(offsetof(PresentGpuContext, commands) == 0x10);
static_assert(offsetof(PresentGpuContext, preparation) == 0xb558);
static_assert(offsetof(PresentGpuContext, owner_thread) == 0xb718);

struct PresentDescriptor {
    uint8_t unknown_0x00[0x8];
    uint64_t shifted_buffer_value;
    uint8_t unknown_0x10[0x28];
};
static_assert(offsetof(PresentDescriptor, shifted_buffer_value) == 0x8);
static_assert(sizeof(PresentDescriptor) == 0x38);
struct PresentAuxiliary {
    uint8_t unknown_0x00;
};
struct PresentSourcePayload {
    uint8_t unknown_0x00;
};
struct PresentSource {
    uint8_t unknown_0x00[0x18];
    PresentSourcePayload payload;
};
static_assert(offsetof(PresentSource, payload) == 0x18);

struct SwapChainState {
    PresentBufferRecord first_buffer;
    uint8_t unknown_0x002c[0x2ac];
    PresentGpuContext *context;
    int64_t frame;
    uint8_t unknown_0x02e8[0x8];
    int32_t video_out;
    uint8_t unknown_0x02f4[0x4];
    uint64_t flip_queue;
    uint8_t unknown_0x0300[0x28];
    uint32_t buffer_count;
    uint8_t unknown_0x032c[0x1c];
    PresentDescriptor descriptor;
    PresentAuxiliary auxiliary;
};
static_assert(offsetof(SwapChainState, first_buffer) == 0x0);
static_assert(offsetof(SwapChainState, context) == 0x2d8);
static_assert(offsetof(SwapChainState, frame) == 0x2e0);
static_assert(offsetof(SwapChainState, video_out) == 0x2f0);
static_assert(offsetof(SwapChainState, flip_queue) == 0x2f8);
static_assert(offsetof(SwapChainState, buffer_count) == 0x328);
static_assert(offsetof(SwapChainState, descriptor) == 0x348);
static_assert(offsetof(SwapChainState, auxiliary) == 0x380);

}  // namespace render
