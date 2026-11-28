// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: giving a pass-on request the Chalice Dungeon gate's handle, in the server modes
// (0x02269e30, called by 0x02265960) and in the local modes (0x02269d40, called by 0x02265170).
//
// Both first check that FrpgNetMan exists (a missing one is reported; it is not read again or
// used). An empty handle gives false and nothing else. Otherwise the request takes a reference to
// the handle's object:
// - when it already holds that object, nothing changes;
// - otherwise it drops the object it held, if any, sets its slot to null and reads the handle again
//   (dropping the last reference can change it), then holds what it read, taking a reference.
// It then takes the kind. The handle's object, as first read, is marked: the server one sets its
// word at +0x120 to 1, the local one its words at +0x11c and +0x120 (one 64-bit store in the
// original). The request is sent on through its vtable with its code (0x13 in the server modes;
// the local one first calls the request's slot 0x38, whose purpose is not known, then sends it
// with 0xc). Both give true.
//
// pass_on_in_server_mode.cpp and pass_on_in_local_mode.cpp still call these as originals; their
// edge rows stub them.
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "dungeon_gate.h"

using namespace frame_timing::dungeon_gate;

namespace {

// What the handle object's words are set to once a request holds it.
constexpr uint32_t given_to_request = 1;

// The codes the request is sent on with.
constexpr int32_t server_request_code = 0x13;
constexpr int32_t local_request_code = 0xc;

// FrpgNetMan's check: a missing one is reported (the report carries on) and not read again.
void check_frpg_net_man()
{
    if (frpg_net_man_instance.get() == nullptr) engine::report_missing(frpg_net_man_name.address());
}

// Makes the request hold the handle's object (first read as `object`, not null), as the engine's
// counted reference assignment does.
void hold_handle(HandleRequest *request, engine::RefObject *const *handle, engine::RefObject *object)
{
    engine::RefObject *held = request->handle;
    if (held == object) return;
    engine::RefObject *value = object;
    if (held != nullptr) {
        engine::release(held);
        request->handle = nullptr;
        value = *handle;
    }
    request->handle = value;
    if (value != nullptr) engine::retain(value);
}

// What both functions do first: check FrpgNetMan, then (unless the handle is empty) make the
// request hold the handle's object and take the kind. Gives the object as first read, or null.
GateHandle *hand_over(HandleRequest *request, engine::RefObject **handle, int32_t kind)
{
    check_frpg_net_man();
    engine::RefObject *object = *handle;
    if (object == nullptr) return nullptr;
    hold_handle(request, handle, object);
    request->kind = kind;
    return as_gate_handle(object);
}

}  // namespace

extern "C" bool bb_frame_timing_give_handle_to_server_request_02269e30(HandleRequest *request,
                                                                       engine::RefObject **handle, int32_t kind)
{
    GateHandle *marked = hand_over(request, handle, kind);
    if (marked == nullptr) return false;
    marked->value_120 = given_to_request;
    request->vtable->send(request, server_request_code);
    return true;
}

extern "C" bool bb_frame_timing_give_handle_to_local_request_02269d40(HandleRequest *request,
                                                                      engine::RefObject **handle, int32_t kind)
{
    GateHandle *marked = hand_over(request, handle, kind);
    if (marked == nullptr) return false;
    marked->value_11c = given_to_request;
    marked->value_120 = given_to_request;
    request->vtable->prepare(request);
    request->vtable->send(request, local_request_code);
    return true;
}
