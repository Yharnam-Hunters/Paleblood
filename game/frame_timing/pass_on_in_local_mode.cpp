// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the Chalice Dungeon gate's online lookups through FrpgNetMan: passing the gate's
// handle on in the local modes (0x02265170) and finding or making the object for a session id
// (0x02265290).
//
// Both first test FrpgNetMan's four bytes as the gate's online checks do (net_allows), then work
// with FrpgNetMan's handle service (dungeon_gate.h):
//
// - pass_on_in_local_mode: the gate's two pass-on methods (0x01ea5e20, 0x01ea6080) call it with
//   the address of the owner's handle and a kind (1 or 2) when the owner is in a local ritual or
//   dungeon mode. When the bytes allow it, it makes a request from the service (0x02264640, made
//   even with an empty handle) and gives the request the handle and the kind (0x02269d40),
//   reporting what that call reports (true once the request holds the handle). Otherwise it sets
//   the handle's words at +0x11c and +0x120 to 2 (one 64-bit store in the original) and reports
//   false. The callers ignore the answer.
// - lookup_session_object: the owner check (0x01ea5280) calls it with the place for the handle
//   and the id. When the bytes do not allow it, it stores null. Otherwise it searches the service's
//   list for a gate handle with the id; with none, it allocates one from the handle heap, builds it
//   for the id (0x02267200; nothing on a failed allocation) and appends it, null or not
//   (0x02266b50). Then it makes a request from the service (0x02264640) and gives it the handle,
//   holding a reference over the call (0x02269870), and stores the handle with a reference the
//   caller drops. It returns the place it filled.
//
// A missing FrpgNetMan is reported and the singleton read again, as the game's singleton check
// does (engine::require). Once the bytes are read the original tests the pointer again before its
// second use and would report it a second (and third) time; those reports can never be reached,
// since a still-missing FrpgNetMan faults on the read of +0x8, here as there.
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "dungeon_gate.h"
#include "runtime/original.h"

using namespace frame_timing::dungeon_gate;

namespace {

// What the local pass-on sets the handle's words at +0x11c and +0x120 to (the request path,
// 0x02269d40, sets them to 1).
constexpr uint32_t passed_on_locally = 2;

// Gives the request the handle (holding a reference) and the kind, sets the handle object's words
// at +0x11c and +0x120 to 1 and sends the request on; false only when the handle is empty
// (give_handle_to_request.cpp replaces it; called here through its original address).
RT_ORIGINAL(0x02269d40, give_handle_to_request, bool(HandleRequest *request, engine::RefObject **handle,
                                                     int32_t kind));
// Gives the request the handle (not replaced; what it reports is not used).
RT_ORIGINAL(0x02269870, give_object_to_request, void(HandleRequest *request, GateHandle **object));

// The list's handle for the id, or null; the list's ends and the id are read once.
GateHandle *find_handle(const GateHandleList &list, int64_t id)
{
    GateHandle **const end = list.end;
    for (GateHandle **entry = list.begin; entry != end; ++entry)
        if ((*entry)->id == id) return *entry;
    return nullptr;
}

}  // namespace

extern "C" bool bb_frame_timing_pass_on_in_local_mode_02265170(engine::RefObject **handle, int32_t kind)
{
    const FrpgNetMan *net = require_frpg_net_man();
    if (net_allows(net)) return give_handle_to_request(make_handle_request(net->handle_service), handle, kind);
    if (*handle != nullptr) {
        GateHandle *object = as_gate_handle(*handle);
        object->value_11c = passed_on_locally;
        object->value_120 = passed_on_locally;
    }
    return false;
}

extern "C" engine::RefObject **bb_frame_timing_lookup_session_object_02265290(engine::RefObject **found,
                                                                             const SessionId *id)
{
    const FrpgNetMan *net = require_frpg_net_man();
    if (!net_allows(net)) {
        *found = nullptr;
        return found;
    }
    HandleService *service = net->handle_service;
    GateHandle *object = find_handle(service->handles, id->value);
    if (object == nullptr) {
        void *memory = gate_handle_heap.get()->allocate(sizeof(GateHandle), gate_handle_alignment);
        if (memory != nullptr) {
            object = static_cast<GateHandle *>(memory);
            construct_gate_handle(object, id);
        }
        GateHandle *appended = object;
        append_gate_handle(&service->handles, &appended);
    }
    HandleRequest *request = make_handle_request(service);
    // The request is given its own reference, dropped after the call (the value passed, whatever
    // the call leaves in its slot).
    GateHandle *passed = object;
    if (object != nullptr) engine::retain(&object->base);
    give_object_to_request(request, &passed);
    if (object != nullptr) engine::release(&object->base);
    *found = object ? &object->base : nullptr;
    if (object != nullptr) engine::retain(&object->base);
    return found;
}
