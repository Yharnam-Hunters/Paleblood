// SPDX-License-Identifier: GPL-2.0-or-later
// A model's wind: what render_update_model_wind (0x02713870) gathers each draw for the grass,
// trees and other swaying models, from the wind sources (map wind areas and effect winds)
// attached to the model's wind receiver. Its shaders read each influence's force_and_time
// (WindForceAndTime: force xyz, wave time w) and point_force (PointWindForce: x = speed * step).
// Field names say what this function does with them; their wider meaning is mostly unknown.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/entry_manager.h"
#include "../engine/vector.h"
#include "runtime/original.h"

namespace render::wind {

using engine::EntryHandle;
using engine::Vec4;

constexpr int list_capacity = 4;

constexpr int matrix_rows = 4;
constexpr int forward_row = 2, translation_row = 3;
struct Matrix44 {
    Vec4 rows[matrix_rows];
};
static_assert(sizeof(Matrix44) == 0x40);

// One wind source's influence on a model: as handed to the draw, and as the receiver keeps it.
struct WindInfluence {
    int32_t kind;              // the source's kind (vtable slot 0x110)
    uint8_t unknown_0x04[0xc];
    Matrix44 transform;        // the source's transform (slot 0x100)
    Vec4 shape;                // (noise time x ramp, 1, cycle position as given, 0)
    Vec4 force_and_time;       // WindForceAndTime: force (x ramped) and the wave time
    Vec4 point_force;          // PointWindForce: (speed x step x ramp, slot 0x1a0, cycle position in [0, 1], 0)
    Vec4 origin_and_extra;     // the transform's last row xyz; w from slot 0x148 or 0x1a0
    Vec4 tint;                 // slot 0x178 (1, 1, 1, 1 without a source object)
    float life;                // seconds left; a kept influence fades out over its last 0.75 s
    uint8_t unknown_0xa4[0x4];
    EntryHandle source;        // the wind source's handle (invalid_entry_handle: a free slot)
    uint8_t flag_0xb0;         // always cleared here; a kept slot whose flag is set is not matched
    uint8_t unknown_0xb1[0x3];     // written with three stale stack bytes (see the hook's entry)
    uint32_t point_source;     // the source's slot 0x128 flag
    uint8_t unknown_0xb8[0x8];
};
static_assert(offsetof(WindInfluence, transform) == 0x10 && offsetof(WindInfluence, shape) == 0x50);
static_assert(offsetof(WindInfluence, force_and_time) == 0x60 && offsetof(WindInfluence, point_force) == 0x70);
static_assert(offsetof(WindInfluence, origin_and_extra) == 0x80 && offsetof(WindInfluence, tint) == 0x90);
static_assert(offsetof(WindInfluence, life) == 0xa0 && offsetof(WindInfluence, source) == 0xa8);
static_assert(offsetof(WindInfluence, flag_0xb0) == 0xb0 && offsetof(WindInfluence, point_source) == 0xb4);
static_assert(sizeof(WindInfluence) == 0xc0);

// The function's output: two lists of up to four influences. Kinds 0 and 3 not flagged as point
// sources go in the first list, which keeps the strongest four once full; all others in the second.
struct ModelWind {
    uint8_t unknown_0x000[0x8];
    uint32_t strongest_count;
    uint8_t unknown_0x00c[0x4];
    WindInfluence strongest[list_capacity];
    uint32_t other_count;
    uint8_t unknown_0x314[0xc];
    WindInfluence other[list_capacity];
};
static_assert(offsetof(ModelWind, strongest_count) == 0x8 && offsetof(ModelWind, strongest) == 0x10);
static_assert(offsetof(ModelWind, other_count) == 0x310 && offsetof(ModelWind, other) == 0x320);

// What a receiver keeps between calls (its storage, 0xc34 bytes): the live influences, how far
// each has ramped in, and the target each one fades from.
struct ReceiverWind {
    uint8_t strongest_active;
    uint8_t other_active;
    uint8_t unknown_0x002[0xe];
    WindInfluence strongest[list_capacity];
    WindInfluence other[list_capacity];
    float strongest_ramp[list_capacity];
    float other_ramp[list_capacity];
    WindInfluence strongest_target[list_capacity];
    WindInfluence other_target[list_capacity];
    uint32_t settle_calls;     // calls since the wind system started settling
};
static_assert(offsetof(ReceiverWind, strongest) == 0x10 && offsetof(ReceiverWind, other) == 0x310);
static_assert(offsetof(ReceiverWind, strongest_ramp) == 0x610 && offsetof(ReceiverWind, other_ramp) == 0x620);
static_assert(offsetof(ReceiverWind, strongest_target) == 0x630 && offsetof(ReceiverWind, other_target) == 0x930);
static_assert(offsetof(ReceiverWind, settle_calls) == 0xc30);
constexpr uint32_t receiver_wind_bytes = 0xc34;

// The receiver's list of attached wind source handles.
struct WindSourceList {
    uint8_t unknown_0x00[0x8];
    const EntryHandle *begin;
    const EntryHandle *end;
};
static_assert(offsetof(WindSourceList, begin) == 0x8 && offsetof(WindSourceList, end) == 0x10);

// A model's wind receiver (its storage is managed by engine_resize_owner_storage).
struct WindReceiver {
    uint8_t unknown_0x000[0x128];
    WindSourceList sources;
    uint8_t unknown_0x140[0x30];
    ReceiverWind *storage;
};
static_assert(offsetof(WindReceiver, sources) == 0x128 && offsetof(WindReceiver, storage) == 0x170);

// The wind system (its receivers' table and its reset state).
struct WindSystem {
    uint8_t unknown_0x00[0x8];
    WindReceiver **receivers;
    uint8_t unknown_0x10[0x38];
    uint32_t receiver_capacity;
    uint8_t reset_pending;     // one-shot, set by the system's reset
    uint8_t settling;          // set by the reset, cleared after a count of updates
    uint8_t unknown_0x4e[0x6];
    uint8_t disabled;
};
static_assert(offsetof(WindSystem, receivers) == 0x8 && offsetof(WindSystem, receiver_capacity) == 0x48);
static_assert(offsetof(WindSystem, reset_pending) == 0x4c && offsetof(WindSystem, settling) == 0x4d);
static_assert(offsetof(WindSystem, disabled) == 0x54);

// The graphics device's mode word that selects the wind's fixed step.
struct GraphicsDevice {
    uint8_t unknown_0x000[0x100];
    int32_t wind_step_mode;
};
static_assert(offsetof(GraphicsDevice, wind_step_mode) == 0x100);
constexpr int32_t sixty_hertz_wind_step_mode = 1;

struct WindSource;

// The source object's virtual methods this function calls.
struct WindSourceVtable {
    void *unknown_slots_0[4];
    Vec4 (*query_0x20)(WindSource *);          // called, result unused
    void *unknown_slots_1[23];
    void (*jitter_angles)(WindSource *, Vec4 *minimum, Vec4 *maximum);
    float (*base_speed)(WindSource *);
    void (*gust_speeds)(WindSource *, float *base, float *amplitude);
    float (*noise_time)(WindSource *);
    void (*transform)(Matrix44 *out, WindSource *);
    void (*cycle)(WindSource *, float *unused, float *position);
    int32_t (*kind)(WindSource *);
    void *unknown_slots_2[1];
    Vec4 (*base_force)(WindSource *);
    uint8_t (*point_source)(WindSource *);
    void *unknown_slots_3[2];
    uint8_t (*use_alternate_extra)(WindSource *);
    float (*alternate_extra)(WindSource *);
    void *unknown_slots_4[4];
    float (*force_scale)(WindSource *);
    Vec4 (*tint)(WindSource *);
    void *unknown_slots_5[4];
    float (*extra)(WindSource *);
};
static_assert(offsetof(WindSourceVtable, query_0x20) == 0x20 && offsetof(WindSourceVtable, jitter_angles) == 0xe0);
static_assert(offsetof(WindSourceVtable, base_speed) == 0xe8 && offsetof(WindSourceVtable, gust_speeds) == 0xf0);
static_assert(offsetof(WindSourceVtable, noise_time) == 0xf8 && offsetof(WindSourceVtable, transform) == 0x100);
static_assert(offsetof(WindSourceVtable, cycle) == 0x108 && offsetof(WindSourceVtable, kind) == 0x110);
static_assert(offsetof(WindSourceVtable, base_force) == 0x120 && offsetof(WindSourceVtable, point_source) == 0x128);
static_assert(offsetof(WindSourceVtable, use_alternate_extra) == 0x140);
static_assert(offsetof(WindSourceVtable, alternate_extra) == 0x148);
static_assert(offsetof(WindSourceVtable, force_scale) == 0x170 && offsetof(WindSourceVtable, tint) == 0x178);
static_assert(offsetof(WindSourceVtable, extra) == 0x1a0);

struct WindSource {
    const WindSourceVtable *vtable;
};

// A wind source registry entry: its state and its source object.
struct WindSourceEntry {
    uint8_t unknown_0x00[0x40];
    int32_t state;
    uint8_t unknown_0x44[0x4];
    WindSource *source;
};
static_assert(offsetof(WindSourceEntry, state) == 0x40 && offsetof(WindSourceEntry, source) == 0x48);
constexpr int32_t ignored_source_state = 1;
constexpr int32_t no_source_kind = -1;
constexpr int32_t strongest_kind_a = 0, strongest_kind_b = 3;

// The fractal noise's state (its constructor and destructor do nothing in this build).
struct NoiseState {
    uint8_t unknown_0x0[0x8];
};

RT_GLOBAL(0x05940e00, wind_system_instance, WindSystem *);
RT_GLOBAL(0x058b0bb0, wind_source_registry, const engine::EntryManager *);
RT_GLOBAL(0x059406c8, graphics_device_instance, GraphicsDevice *);
// When set, nothing ages and no influence ramps in (its meaning is unknown).
RT_GLOBAL(0x05940df9, wind_hold, const uint8_t);
// The receiver list used when the handle does not name a receiver: empty.
RT_GLOBAL(0x05ac4220, no_wind_sources, const WindSourceList);
// A cell the community frame-rate patches set to their wind step (1/60 s; 1/120 s in
// "120 FPS++" and "Uncap FPS++"). Zero in the stock image.
RT_GLOBAL(0x0575a908, patched_wind_step, const float);

RT_ORIGINAL(0x0132c720, receiver_wind, ReceiverWind *(WindReceiver *));
RT_ORIGINAL(0x0132c6c0, resize_receiver_wind, void(WindReceiver *, uint32_t byte_count));
RT_ORIGINAL(0x01314320, resolve_wind_source, WindSourceEntry *(const engine::EntryManager *, const EntryHandle *));
RT_ORIGINAL(0x02a85e30, fractal_noise,
            float(float x, float y, float z, float frequency, float persistence, NoiseState *, int32_t octaves));

}  // namespace render::wind
