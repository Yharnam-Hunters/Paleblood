// SPDX-License-Identifier: GPL-2.0-or-later
// render_update_model_wind (0x02713870): every model draw (callers 0x0216f9d0, the model mesh
// draw, and 0x02124660) asks for its wind receiver's current wind. The receiver keeps up to four
// influences in each of two lists; this call ages them (each fades over its last 0.75 s and its
// wave time advances by the wind step), copies the live ones to the output, then samples every
// attached wind source - its speed, gusts from fractal noise, a direction jittered by noisy
// Euler angles - and adds or refreshes that source's influence, ramping it in over 0.1 s.
//
// The wind step is fixed: 1/60 s when the graphics device's mode word is 1 (as the game sets it),
// else 1/30 s. BB_TARGET_FPS reproduces the community frame-rate patches, which edit the three
// step loads (frame_timing/target_fps.h). With any of them an unknown receiver's step is the
// patches' cell (it changes nothing: without kept wind nothing is written). With 30 an existing
// receiver's 1/30 s step is that cell too; with 60 or uncapped an existing receiver steps by the
// last frame's measured time, at most 1/30 s.
#include "model_wind.h"

#include <bit>

#include "../engine/rotation_math.h"
#include "../engine/scalar.h"
#include "../engine/vector_operations.h"
#include "../frame_timing/flipper.h"
#include "../frame_timing/target_fps.h"

namespace {

using namespace render::wind;
using engine::Vec4;
namespace ops = engine::vector_operations;
namespace rotation = engine::rotation;

constexpr float influence_lifetime = 0.75f;   // a new or refreshed influence's life, and its fade time
constexpr float ramp_seconds = 0.1f;
constexpr float negligible_speed = 1.1920929e-7f;   // FLT_EPSILON
constexpr float gust_noise_y_offset = 20.0f, gust_noise_z_offset = 50.0f;
constexpr int32_t gust_noise_octaves = 5;
constexpr float jitter_noise_y_scale = 6.0f, jitter_noise_y_offset = 400.0f, jitter_noise_z_offset = 150.0f;
constexpr int32_t jitter_noise_octaves = 4;
constexpr float noise_frequency = 0.1f, noise_persistence = 0.3f, noise_amplitude = 5.0f;
constexpr float half = 0.5f;

using ReceiverHandle = uint64_t;
constexpr int stale_byte_bits = 8;
constexpr int stale_byte_count = 3;
using StaleBytes = uint8_t[stale_byte_count];
constexpr ReceiverHandle invalid_receiver = UINT64_MAX;

// ---- the step --------------------------------------------------------------------------------

float fixed_step(bool sixty_hertz)
{
    return std::bit_cast<float>(sixty_hertz ? frame_timing::k_interval_60 : frame_timing::k_interval_30);
}

// The step when the handle names no receiver (the patches point both loads at their cell).
float unknown_receiver_step()
{
    const bool sixty_hertz = graphics_device_instance.get()->wind_step_mode == sixty_hertz_wind_step_mode;
    if (frame_timing::target_fps() != frame_timing::Target::original) return patched_wind_step.get();
    return fixed_step(sixty_hertz);
}

// The step for an existing receiver.
float receiver_step()
{
    using frame_timing::Target;
    const Target target = frame_timing::target_fps();
    if (target == Target::fps60 || target == Target::uncapped) {
        // The 60 FPS++ code: the last frame's time, capped at 1/30 s.
        const float measured = frame_timing::flipper_instance.get()->frame_seconds;
        return engine::min_of(measured, std::bit_cast<float>(frame_timing::k_interval_30));
    }
    const bool sixty_hertz = graphics_device_instance.get()->wind_step_mode == sixty_hertz_wind_step_mode;
    if (target == Target::fps30 && !sixty_hertz) return patched_wind_step.get();
    return fixed_step(sixty_hertz);
}

// ---- influences ------------------------------------------------------------------------------

using ops::lanes;

void clear_influence(WindInfluence &influence)
{
    influence.kind = 0;
    influence.transform = {{lanes(1.0f, 0.0f, 0.0f, 0.0f), lanes(0.0f, 1.0f, 0.0f, 0.0f),
                            lanes(0.0f, 0.0f, 1.0f, 0.0f), lanes(0.0f, 0.0f, 0.0f, 1.0f)}};
    influence.shape = Vec4::zero();
    influence.force_and_time = Vec4::zero();
    influence.point_force = Vec4::zero();
    influence.origin_and_extra = Vec4::zero();
    influence.tint = lanes(1.0f, 1.0f, 1.0f, 1.0f);
    influence.life = 0.0f;
    influence.source = engine::invalid_entry_handle;
    influence.flag_0xb0 = 0;
    influence.point_source = 0;
}

// Copies what the original copies of an influence: everything from the transform on, padding
// included, and the kind (not the bytes after the kind or after point_source).
void copy_influence(WindInfluence &to, const WindInfluence &from)
{
    to.kind = from.kind;
    to.transform = from.transform;
    to.shape = from.shape;
    to.force_and_time = from.force_and_time;
    to.point_force = from.point_force;
    to.origin_and_extra = from.origin_and_extra;
    to.tint = from.tint;
    to.life = from.life;
    for (int i = 0; i < static_cast<int>(sizeof(to.unknown_0xa4)); ++i) to.unknown_0xa4[i] = from.unknown_0xa4[i];
    to.source = from.source;
    to.flag_0xb0 = from.flag_0xb0;
    for (int i = 0; i < static_cast<int>(sizeof(to.unknown_0xb1)); ++i) to.unknown_0xb1[i] = from.unknown_0xb1[i];
    to.point_source = from.point_source;
}

void clear_receiver_wind(ReceiverWind &wind)
{
    wind.strongest_active = 0;
    wind.other_active = 0;
    for (int i = 0; i < list_capacity; ++i) {
        wind.strongest_ramp[i] = 0.0f;
        clear_influence(wind.strongest[i]);
        clear_influence(wind.strongest_target[i]);
    }
    for (int i = 0; i < list_capacity; ++i) {
        wind.other_ramp[i] = 0.0f;
        clear_influence(wind.other[i]);
        clear_influence(wind.other_target[i]);
    }
    wind.settle_calls = 0;
}

// The first call after the wind system starts settling silences the targets the influences fade
// from: their forces (keeping each wave time) and their point speeds.
void silence_targets(ReceiverWind &wind)
{
    const Vec4 keep_all_but_x = lanes(0.0f, 1.0f, 1.0f, 1.0f);
    for (WindInfluence *targets : {wind.strongest_target, wind.other_target}) {
        for (int i = 0; i < list_capacity; ++i) {
            targets[i].point_force = ops::multiply_in_order(keep_all_but_x, targets[i].point_force);
            targets[i].force_and_time = Vec4::zero().with_w_of(targets[i].force_and_time);
        }
    }
}

// Ages one list by `age_step`: each live influence fades from its target (force and point speed
// scaled by its remaining life over 0.75 s) while its wave time advances; spent or invalid ones
// are cleared. Returns whether any is still live.
bool age_list(WindInfluence *influences, WindInfluence *targets, float *ramps, float age_step)
{
    const Vec4 time_step = Vec4::only_w(age_step);
    bool any_live = false;
    for (int i = 0; i < list_capacity; ++i) {
        WindInfluence &influence = influences[i];
        const WindInfluence &target = targets[i];
        const float life = influence.life;
        if (life > 0.0f) {
            const float fade = engine::divide(life, influence_lifetime);
            const float weight = 0.0f > fade ? 0.0f : engine::min_of(1.0f, fade);
            const Vec4 timed = ops::add_in_order(time_step, influence.force_and_time);
            influence.point_force = ops::multiply_in_order(lanes(weight, 1.0f, 1.0f, 1.0f), target.point_force);
            influence.force_and_time = ops::multiply_in_order(ops::broadcast(weight), target.force_and_time).with_w_of(timed);
            const float remaining = engine::subtract(life, age_step);
            influence.life = remaining;
            // A NaN life left after the step counts as live.
            if (!(0.0f >= remaining)) {
                any_live = true;
                continue;
            }
        }
        ramps[i] = 0.0f;
        clear_influence(influence);
        clear_influence(targets[i]);
    }
    return any_live;
}

// Copies the live influences (life > 0, a source) into the output list, at most four.
void publish_live(const WindInfluence *influences, WindInfluence *out, uint32_t &count)
{
    for (int i = 0; i < list_capacity && count != list_capacity; ++i) {
        const WindInfluence &influence = influences[i];
        if (influence.life > 0.0f && influence.source != engine::invalid_entry_handle) {
            copy_influence(out[count], influence);
            ++count;
        }
    }
}

// ---- sampling a source -----------------------------------------------------------------------

// What the source's methods report. The three out-parameter pairs (jitter angles, gust speeds,
// cycle) keep the previous source's values when the entry stops resolving before that call: the
// original's stack slots are written only by those calls (see SourceCarry). Before the first
// source they are uninitialized in the original; here they start at 0.
struct SourceReport {
    int32_t kind = 0;
    uint8_t point_source = 0;
    Matrix44 orientation{};
    Vec4 jitter_minimum = Vec4::zero(), jitter_maximum = Vec4::zero();
    float base_speed = 0.0f;
    float gust_base = 0.0f, gust_amplitude = 0.0f;
    float noise_time = 0.0f;
    float cycle_unused = 0.0f, cycle_position = 0.0f;
    float extra = 0.0f;
    Vec4 base_force = Vec4::zero();
    uint8_t use_alternate_extra = 0;
    float alternate_extra = 0.0f;
    float force_scale = 1.0f;
    Vec4 tint = lanes(1.0f, 1.0f, 1.0f, 1.0f);
    Matrix44 transform{};
};

// The out-parameter values carried from one source to the next.
struct SourceCarry {
    Vec4 jitter_minimum = Vec4::zero(), jitter_maximum = Vec4::zero();
    float gust_base = 0.0f, gust_amplitude = 0.0f;
    float cycle_unused = 0.0f, cycle_position = 0.0f;
};

void take_carry(SourceReport &report, const SourceCarry &carry)
{
    report.jitter_minimum = carry.jitter_minimum;
    report.jitter_maximum = carry.jitter_maximum;
    report.gust_base = carry.gust_base;
    report.gust_amplitude = carry.gust_amplitude;
    report.cycle_unused = carry.cycle_unused;
    report.cycle_position = carry.cycle_position;
}

void keep_carry(SourceCarry &carry, const SourceReport &report)
{
    carry.jitter_minimum = report.jitter_minimum;
    carry.jitter_maximum = report.jitter_maximum;
    carry.gust_base = report.gust_base;
    carry.gust_amplitude = report.gust_amplitude;
    carry.cycle_unused = report.cycle_unused;
    carry.cycle_position = report.cycle_position;
}

// The source entry for the current handle: looked up afresh before every method call, as the
// original does, from the handle reread from the receiver's list.
WindSourceEntry *lookup(const EntryHandle *handle_in_list)
{
    const EntryHandle handle = *handle_in_list;
    return resolve_wind_source(wind_source_registry.get(), &handle);
}

Matrix44 identity_transform()
{
    return {{lanes(1.0f, 0.0f, 0.0f, 0.0f), lanes(0.0f, 1.0f, 0.0f, 0.0f), lanes(0.0f, 0.0f, 1.0f, 0.0f),
             lanes(0.0f, 0.0f, 0.0f, 1.0f)}};
}

// The first part of the report, up to the source's kind and point flag. False: skip the source
// (ignored state, no entry or object, or no kind).
bool report_identity(const EntryHandle *handle, SourceReport &report)
{
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->state == ignored_source_state) return false;
    const WindSourceEntry *entry = lookup(handle);
    if (!entry || !entry->source) return false;
    WindSource *source = entry->source;
    report.kind = source->vtable->kind(source);
    if (report.kind == no_source_kind) return false;
    entry = lookup(handle);
    if (entry && entry->source) report.point_source = entry->source->vtable->point_source(entry->source);
    return true;
}

// The source's methods, in the original's order. Some calls check the entry's object, some do not.
void report_values(const EntryHandle *handle, SourceReport &report)
{
    // Only the orientation's third row is used (the original also overwrites the unused last one).
    if (const WindSourceEntry *entry = lookup(handle)) entry->source->vtable->transform(&report.orientation, entry->source);
    else report.orientation = identity_transform();
    if (const WindSourceEntry *entry = lookup(handle))
        entry->source->vtable->jitter_angles(entry->source, &report.jitter_minimum, &report.jitter_maximum);
    if (const WindSourceEntry *entry = lookup(handle)) report.base_speed = entry->source->vtable->base_speed(entry->source);
    if (const WindSourceEntry *entry = lookup(handle))
        entry->source->vtable->gust_speeds(entry->source, &report.gust_base, &report.gust_amplitude);
    if (const WindSourceEntry *entry = lookup(handle)) report.noise_time = entry->source->vtable->noise_time(entry->source);
    if (const WindSourceEntry *entry = lookup(handle))
        entry->source->vtable->cycle(entry->source, &report.cycle_unused, &report.cycle_position);
    if (const WindSourceEntry *entry = lookup(handle)) report.extra = entry->source->vtable->extra(entry->source);
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->source)
        report.base_force = entry->source->vtable->base_force(entry->source);
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->source)
        report.use_alternate_extra = entry->source->vtable->use_alternate_extra(entry->source);
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->source)
        report.alternate_extra = entry->source->vtable->alternate_extra(entry->source);
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->source)
        report.force_scale = entry->source->vtable->force_scale(entry->source);
    if (const WindSourceEntry *entry = lookup(handle); entry && entry->source)
        report.tint = entry->source->vtable->tint(entry->source);
}

// The gust: (gust base + |noise| x amplitude x 5, 0, 0, 0), kept only when both the gust base
// and the amplitude differ from the base speed by more than FLT_EPSILON after scaling. The noise
// is sampled whenever the base differs.
Vec4 gust(const SourceReport &report, NoiseState &noise)
{
    const float scale = report.force_scale;
    const float base_margin = ops::absolute(engine::multiply(engine::subtract(report.gust_base, report.base_speed), scale));
    Vec4 gust = Vec4::zero();
    if (base_margin > negligible_speed) {
        const float time = report.noise_time;
        const float n = fractal_noise(time, engine::add(time, gust_noise_y_offset), engine::add(time, gust_noise_z_offset),
                                      noise_frequency, noise_persistence, &noise, gust_noise_octaves);
        const float spread = engine::multiply(engine::multiply(ops::absolute(n), report.gust_amplitude), noise_amplitude);
        // (gust, 0, 0, 0) + 0: a negative zero comes out positive.
        gust = ops::add_in_order(Vec4::only_x(engine::add(spread, report.gust_base)), Vec4::zero());
    }
    if (negligible_speed >= base_margin) return Vec4::zero();
    const float amplitude_margin =
        ops::absolute(engine::multiply(engine::subtract(report.gust_amplitude, report.base_speed), scale));
    if (negligible_speed >= amplitude_margin) return Vec4::zero();
    return gust;
}

// The wind direction: the orientation's third row turned by Euler angles picked by noise
// between the jitter minimum and maximum.
Vec4 jittered_direction(const SourceReport &report, NoiseState &noise)
{
    const Vec4 range = ops::subtract(report.jitter_maximum, report.jitter_minimum);
    const float time = report.noise_time;
    const float n = fractal_noise(time, engine::add(engine::multiply(time, jitter_noise_y_scale), jitter_noise_y_offset),
                                  engine::add(time, jitter_noise_z_offset), noise_frequency, noise_persistence, &noise,
                                  jitter_noise_octaves);
    const float fraction = ops::absolute(engine::multiply(n, noise_amplitude));
    const Vec4 angles = lanes(engine::add(engine::multiply(range.x(), fraction), report.jitter_minimum.x()),
                              engine::add(engine::multiply(range.y(), fraction), report.jitter_minimum.y()),
                              engine::add(engine::multiply(range.z(), fraction), report.jitter_minimum.z()), 0.0f);
    const Vec4 half_angles = ops::multiply_in_order(angles, ops::broadcast(half));
    const rotation::RotationColumns turn =
        rotation::rotation_from_quaternion(rotation::quaternion_from_half_angles(rotation::sine_cosine(half_angles)));
    return rotation::rotate(turn, report.orientation.rows[forward_row]);
}

// The cycle position wrapped into [0, 1] by whole steps (an infinite one never finishes, as in
// the original).
float wrapped_cycle(float position)
{
    while (0.0f > position) position = engine::add(position, 1.0f);
    while (position > 1.0f) position = engine::add(position, -1.0f);
    return position;
}

// One source's sampled influence before ramping.
struct Sample {
    int32_t kind;
    uint8_t point_source;
    EntryHandle source;
    Matrix44 transform;
    Vec4 total_force;          // (force xyz, 0): base force plus step x (gust + speed x direction)
    float point_speed;         // step x (speed + gust length)
    float extra;
    float cycle;               // wrapped
    float noise_time;
    float cycle_position;      // as given
    Vec4 origin_and_extra;
    Vec4 tint;
};

Sample sample_source(const SourceReport &report, const EntryHandle *handle, NoiseState &noise, float step)
{
    const float scale = report.force_scale;
    const float speed = engine::multiply(report.base_speed, scale);
    const Vec4 gust_force = ops::multiply_in_order(ops::broadcast(scale), gust(report, noise));
    const Vec4 direction = jittered_direction(report, noise);
    if (const WindSourceEntry *entry = lookup(handle)) entry->source->vtable->query_0x20(entry->source);

    Sample sample{};
    if (const WindSourceEntry *entry = lookup(handle)) entry->source->vtable->transform(&sample.transform, entry->source);
    else sample.transform = identity_transform();

    const Vec4 force = ops::multiply_in_order(
        ops::broadcast(step), ops::add_in_order(gust_force, ops::multiply_in_order(ops::broadcast(speed), direction)));
    const float gust_length = rotation::refined_length(rotation::squared_length(gust_force));
    sample.point_speed = engine::multiply(step, engine::add(speed, gust_length));
    sample.kind = report.kind;
    sample.point_source = report.point_source;
    sample.source = *handle;
    sample.total_force = ops::add_in_order(report.base_force.with_w_of(Vec4::zero()), force.with_w_of(Vec4::zero()));
    sample.extra = report.extra;
    sample.cycle = wrapped_cycle(report.cycle_position);
    sample.noise_time = report.noise_time;
    sample.cycle_position = report.cycle_position;
    const float last = report.use_alternate_extra ? report.alternate_extra : report.extra;
    sample.origin_and_extra = ops::xyz_with_w(sample.transform.rows[translation_row], Vec4::only_w(last));
    sample.tint = report.tint;
    return sample;
}

// ---- adding a source's influence -------------------------------------------------------------

struct Context {
    ModelWind *out;
    ReceiverWind *kept;        // null: nothing is kept (no receiver storage)
    float step;                // replaced by the ramp step after every sampled source (see below)
    StaleBytes padding;        // what the original writes into each written influence's padding (see the entry)
};

// The ramped fields of an influence: its x force, point speed and noise time scaled by the ramp
// fraction `weight` (0 for a new one), and the ramp time as the force's w.
struct RampedFields {
    Vec4 shape;
    Vec4 force_and_time;
    Vec4 point_force;
};

RampedFields ramped(const Sample &sample, float weight, float ramp)
{
    return {lanes(engine::multiply(sample.noise_time, weight), 1.0f, sample.cycle_position, 0.0f),
            lanes(engine::multiply(sample.total_force.x(), weight), sample.total_force.y(), sample.total_force.z(), ramp),
            lanes(engine::multiply(sample.point_speed, weight), sample.extra, sample.cycle, 0.0f)};
}

RampedFields unramped(const Sample &sample)
{
    return {lanes(sample.noise_time, 1.0f, sample.cycle_position, 0.0f), sample.total_force,
            lanes(sample.point_speed, sample.extra, sample.cycle, 0.0f)};
}

void write_influence(WindInfluence &to, const Sample &sample, const RampedFields &fields, float life,
                     uint32_t point_source, const StaleBytes &padding)
{
    to.kind = sample.kind;
    to.transform = sample.transform;
    to.shape = fields.shape;
    to.force_and_time = fields.force_and_time;
    to.point_force = fields.point_force;
    to.origin_and_extra = sample.origin_and_extra;
    to.tint = sample.tint;
    to.life = life;
    to.source = sample.source;
    to.flag_0xb0 = 0;
    for (int i = 0; i < static_cast<int>(sizeof(to.unknown_0xb1)); ++i) to.unknown_0xb1[i] = padding[i];
    to.point_source = point_source;
}

// One kept list and the output list it feeds.
struct ListView {
    WindInfluence *kept;
    WindInfluence *targets;
    float *ramps;
    uint8_t *active;
    WindInfluence *out;
    bool match_flagged;        // a kept slot with flag_0xb0 set still matches its source
};

int find_kept(const ListView &list, EntryHandle source)
{
    for (int i = 0; i < list_capacity; ++i) {
        if (list.kept[i].source == source && (list.match_flagged || list.kept[i].flag_0xb0 == 0)) return i;
    }
    return -1;
}

int find_free(const ListView &list)
{
    for (int i = 0; i < list_capacity; ++i) {
        if (0.0f >= list.kept[i].life || list.kept[i].source == engine::invalid_entry_handle) return i;
    }
    return -1;
}

void keep(const ListView &list, int slot, const Sample &sample, const RampedFields &fields, const Context &context)
{
    WindInfluence &kept = list.kept[slot];
    write_influence(kept, sample, fields, influence_lifetime, sample.point_source, context.padding);
    copy_influence(list.targets[slot], kept);
    *list.active = 1;
}

enum class Kept { refreshed, added, no_room };

// An output entry by index. The first list's count can pass four (see add_strongest); the
// original then reads and writes "entries" past the list, over the second list's count and
// entries, and so does this, by address.
WindInfluence &listed_entry(WindInfluence *list, uint32_t index)
{
    return *reinterpret_cast<WindInfluence *>(reinterpret_cast<uintptr_t>(list) + index * sizeof(WindInfluence));
}

// Refreshes a kept source (ramping it further in, and its output entry if it is listed among the
// first `listed`) or keeps it in a free slot, writing it to `new_out` in the output.
Kept refresh_or_keep(const ListView &list, const Sample &sample, const Context &context, WindInfluence *new_out,
                     uint32_t listed)
{
    if (const int slot = find_kept(list, sample.source); slot >= 0) {
        const float ramp = engine::add(context.step, list.ramps[slot]);
        list.ramps[slot] = ramp;
        const float weight = engine::min_of(engine::divide(ramp, ramp_seconds), 1.0f);
        const RampedFields fields = ramped(sample, weight, ramp);
        for (uint32_t i = 0; i < listed; ++i) {
            if (WindInfluence &entry = listed_entry(list.out, i); entry.source == sample.source) {
                write_influence(entry, sample, fields, 0.0f, sample.point_source, context.padding);
                break;
            }
        }
        keep(list, slot, sample, fields, context);
        return Kept::refreshed;
    }
    const int slot = find_free(list);
    if (slot < 0) return Kept::no_room;
    list.ramps[slot] = 0.0f;
    const float no_ramp = 0.0f;
    const RampedFields fields = ramped(sample, no_ramp, no_ramp);
    write_influence(*new_out, sample, fields, 0.0f, sample.point_source, context.padding);
    keep(list, slot, sample, fields, context);
    return Kept::added;
}

void add_other(Context &context, const Sample &sample)
{
    ModelWind &out = *context.out;
    const uint32_t listed = out.other_count;
    if (listed == list_capacity || !context.kept) return;
    ReceiverWind &kept = *context.kept;
    const ListView list{kept.other, kept.other_target, kept.other_ramp, &kept.other_active, out.other, true};
    if (refresh_or_keep(list, sample, context, &out.other[listed], listed) == Kept::added) ++out.other_count;
}

float force_length(Vec4 force) { return rotation::refined_length(rotation::squared_length(force)); }

void add_strongest(Context &context, const Sample &sample)
{
    ModelWind &out = *context.out;
    const uint32_t listed = out.strongest_count;
    WindInfluence *new_out = &listed_entry(out.strongest, listed);
    if (listed == list_capacity) {
        // Full: the source replaces the first listed influence weaker than it, unramped, even
        // when nothing is kept.
        int replaced = 0;
        while (!(force_length(sample.total_force) > force_length(out.strongest[replaced].force_and_time))) {
            if (++replaced == list_capacity) return;
        }
        new_out = &out.strongest[replaced];
        const uint32_t not_point = 0;
        write_influence(*new_out, sample, unramped(sample), 0.0f, not_point, context.padding);
    }
    if (!context.kept) return;
    ReceiverWind &kept = *context.kept;
    const ListView list{kept.strongest, kept.strongest_target, kept.strongest_ramp, &kept.strongest_active,
                        out.strongest, false};
    // Quirk: in a full list a newly kept source overwrites the replaced entry again, ramped from
    // zero, and is counted as a fifth entry; a later new source then goes to "entry" five, past
    // the list. Four listed entries are normally four live kept slots, leaving no free one; only
    // sources whose handle is the invalid handle (which the real lookup rejects) get there, since
    // a slot kept for such a source still counts as free.
    if (refresh_or_keep(list, sample, context, new_out, out.strongest_count) == Kept::added) ++out.strongest_count;
}

// ---- the receiver ----------------------------------------------------------------------------

void clear_output(ModelWind &out)
{
    out.strongest_count = 0;
    for (WindInfluence &influence : out.strongest) clear_influence(influence);
    out.other_count = 0;
    for (WindInfluence &influence : out.other) clear_influence(influence);
}

WindReceiver *receiver_at(const WindSystem *system, ReceiverHandle handle)
{
    const uint32_t index = static_cast<uint32_t>(handle);
    if (index >= system->receiver_capacity) return nullptr;
    return system->receivers[index];
}

// The receiver's kept wind, allocating and clearing it on first use. `found`: the handle names a
// receiver (else the unknown receiver's step applies).
ReceiverWind *receiver_storage(const ReceiverHandle *receiver_handle, bool &found)
{
    found = false;
    ReceiverHandle handle = *receiver_handle;
    if (handle == invalid_receiver) return nullptr;
    if (WindReceiver *receiver = receiver_at(wind_system_instance.get(), handle)) {
        if (ReceiverWind *wind = receiver_wind(receiver)) {
            found = true;
            return wind;
        }
        handle = *receiver_handle;
        if (handle == invalid_receiver) return nullptr;
    }
    if (WindReceiver *receiver = receiver_at(wind_system_instance.get(), handle)) {
        resize_receiver_wind(receiver, receiver_wind_bytes);
        handle = *receiver_handle;
    }
    if (handle == invalid_receiver) return nullptr;
    WindReceiver *receiver = receiver_at(wind_system_instance.get(), handle);
    if (!receiver) return nullptr;
    found = true;
    ReceiverWind *wind = receiver_wind(receiver);
    if (wind) clear_receiver_wind(*wind);
    return wind;
}

// Ages the kept influences and lists the live ones.
void update_kept(ReceiverWind &kept, ModelWind &out, float step)
{
    float age_step = wind_hold.get() ? 0.0f : step;
    const WindSystem *system = wind_system_instance.get();
    if (system->settling || system->reset_pending) {
        const uint32_t calls = kept.settle_calls++;
        age_step = 0.0f;
        if (calls == 0) silence_targets(kept);
    } else {
        kept.settle_calls = 0;
    }
    if (kept.strongest_active && !age_list(kept.strongest, kept.strongest_target, kept.strongest_ramp, age_step))
        kept.strongest_active = 0;
    if (kept.other_active && !age_list(kept.other, kept.other_target, kept.other_ramp, age_step)) kept.other_active = 0;
    out.strongest_count = 0;
    out.other_count = 0;
    if (kept.strongest_active) publish_live(kept.strongest, out.strongest, out.strongest_count);
    if (kept.other_active) publish_live(kept.other, out.other, out.other_count);
}

const WindSourceList &source_list(const ReceiverHandle *receiver_handle)
{
    const ReceiverHandle handle = *receiver_handle;
    if (handle != invalid_receiver) {
        const WindSystem *system = wind_system_instance.get();
        const uint32_t index = static_cast<uint32_t>(handle);
        // No null check: the original reads a missing receiver's list through a null pointer.
        if (index < system->receiver_capacity) return system->receivers[index]->sources;
    }
    return no_wind_sources.get();
}

}  // namespace

// The body, entered from the hook below with the three stale stack bytes.
extern "C" __attribute__((visibility("hidden"), used)) void
render_update_model_wind_body(const ReceiverHandle *receiver_handle, ModelWind *out, uint32_t stale_bytes)
{
    clear_output(*out);
    if (wind_system_instance.get()->disabled) return;

    bool found = false;
    ReceiverWind *kept = receiver_storage(receiver_handle, found);
    Context context{out, nullptr, found ? receiver_step() : unknown_receiver_step(), {}};
    for (int i = 0; i < stale_byte_count; ++i) context.padding[i] = static_cast<uint8_t>(stale_bytes >> (i * stale_byte_bits));
    if (kept) {
        update_kept(*kept, *out, context.step);
        context.kept = kept;
    }

    // The original also calls the noise state's constructor and destructor, which do nothing
    // (both are members of the returns_nothing family).
    NoiseState noise{};
    SourceCarry carry;
    const WindSourceList &sources = source_list(receiver_handle);
    for (const EntryHandle *handle = sources.begin; handle != sources.end; ++handle) {
        SourceReport report;
        if (!report_identity(handle, report)) continue;
        take_carry(report, carry);
        report_values(handle, report);
        keep_carry(carry, report);
        const Sample sample = sample_source(report, handle, noise, context.step);
        // Quirk: the hold flag's zero ramp step replaces the step for every later source.
        context.step = wind_hold.get() ? 0.0f : context.step;
        const bool strongest = (sample.kind == strongest_kind_a || sample.kind == strongest_kind_b) && !sample.point_source;
        if (strongest) add_strongest(context, sample);
        else add_other(context, sample);
    }
}

// Quirk: the original fills each influence it writes with three bytes of its own stack frame that
// it never writes (rbp - 0xd3 .. rbp - 0xd1): whatever the stack held there, 0xdb .. 0xd9 bytes
// below the caller's stack pointer, before the call. The entry reads them from there before
// anything else uses that stack, so the influence padding gets the same bytes.
#if defined(__APPLE__)
#define MODEL_WIND_BODY "_render_update_model_wind_body"
#else
#define MODEL_WIND_BODY "render_update_model_wind_body"
#endif
extern "C" __attribute__((naked, visibility("default"))) void bb_render_update_model_wind(const ReceiverHandle *, ModelWind *)
{
    asm volatile("movzwl -0xdb(%rsp), %edx\n\t"
                 "movzbl -0xd9(%rsp), %ecx\n\t"
                 "shll $16, %ecx\n\t"
                 "orl %ecx, %edx\n\t"
                 "jmp " MODEL_WIND_BODY);
}
