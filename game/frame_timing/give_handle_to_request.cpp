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
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "runtime/original.h"

namespace {

struct FrpgNetMan;  // the online manager; only its presence is checked here
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// The request 0x02264640 makes (0xe0 bytes): a vtable, the handle it holds and the kind.
struct PassOnRequest;
struct PassOnRequestVtable {
    void *unknown_0x00[5];
    void (*send)(PassOnRequest *request, int32_t code);
    void *unknown_0x30;
    void (*prepare)(PassOnRequest *request);  // called before the local send; its purpose is not known
};
static_assert(offsetof(PassOnRequestVtable, send) == 0x28 && offsetof(PassOnRequestVtable, prepare) == 0x38);
struct PassOnRequest {
    const PassOnRequestVtable *vtable;
    uint8_t unknown_0x08[0xc0];
    engine::RefObject *handle;
    uint8_t unknown_0xd0[0xc];
    int32_t kind;
};
static_assert(offsetof(PassOnRequest, handle) == 0xc8 && offsetof(PassOnRequest, kind) == 0xdc &&
              sizeof(PassOnRequest) == 0xe0);

// The gate's handle object, as far as these write it.
struct HandleObject {
    engine::RefObject base;
    uint8_t unknown_0x010[0x10c];
    uint32_t value_11c;
    uint32_t value_120;
};
static_assert(offsetof(HandleObject, value_11c) == 0x11c && offsetof(HandleObject, value_120) == 0x120);
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
void hold_handle(PassOnRequest *request, engine::RefObject *const *handle, engine::RefObject *object)
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

HandleObject *handle_object(engine::RefObject *object)
{
    return reinterpret_cast<HandleObject *>(object);
}

}  // namespace

extern "C" bool bb_frame_timing_give_handle_to_server_request_02269e30(PassOnRequest *request,
                                                                       engine::RefObject **handle, int32_t kind)
{
    check_frpg_net_man();
    engine::RefObject *object = *handle;
    if (object == nullptr) return false;
    hold_handle(request, handle, object);
    request->kind = kind;
    handle_object(object)->value_120 = given_to_request;
    request->vtable->send(request, server_request_code);
    return true;
}

extern "C" bool bb_frame_timing_give_handle_to_local_request_02269d40(PassOnRequest *request,
                                                                      engine::RefObject **handle, int32_t kind)
{
    check_frpg_net_man();
    engine::RefObject *object = *handle;
    if (object == nullptr) return false;
    hold_handle(request, handle, object);
    request->kind = kind;
    HandleObject *marked = handle_object(object);
    marked->value_11c = given_to_request;
    marked->value_120 = given_to_request;
    request->vtable->prepare(request);
    request->vtable->send(request, local_request_code);
    return true;
}
