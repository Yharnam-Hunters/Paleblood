// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: passing the Chalice Dungeon gate's handle on in the server modes (0x02265960).
//
// The gate's two pass-on methods (0x01ea5e20, 0x01ea6080) and two callers next to the handle
// rebuild (0x01e9d352, 0x01e9d3e2) call it with the address of the owner's handle and a kind (1 or
// 2) when the owner is in a server ritual or dungeon mode. It takes FrpgNetMan's object at +0xca8;
// when the handle is set it makes a request from that object (0x02264640) and gives the request the
// handle and the kind (0x02269e30), reporting what that call reports (true once the request holds
// the handle). With no handle it does neither and reports false. The callers ignore the answer.
//
// A missing FrpgNetMan is reported and the singleton read again, as the game's singleton check
// does; the original then reads +0xca8 through the second read before testing it, so its second
// report can never be reached (a still-missing FrpgNetMan faults on that read, here as there).
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "runtime/original.h"

namespace {

struct PassOnService;  // FrpgNetMan's object at +0xca8; its class is not known
struct PassOnRequest;  // what 0x02264640 makes from it (0xe0 bytes, from the heap at 0x05940420)

// FrpgNetMan, the online manager; only the field this function reads.
struct FrpgNetMan {
    uint8_t unknown_0x000[0xca8];
    PassOnService *pass_on_service;
};
static_assert(offsetof(FrpgNetMan, pass_on_service) == 0xca8);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// Makes a request from the service (not replaced; may answer null when its allocation fails).
RT_ORIGINAL(0x02264640, make_pass_on_request, PassOnRequest *(PassOnService *service));
// Gives the request the handle (holding a reference) and the kind, marks the handle object and
// sends the request on; false only when the handle is empty (not replaced).
RT_ORIGINAL(0x02269e30, give_handle_to_request, bool(PassOnRequest *request, engine::RefObject **handle,
                                                     int32_t kind));

}  // namespace

extern "C" bool bb_frame_timing_pass_on_in_server_mode_02265960(engine::RefObject **handle, int32_t kind)
{
    FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    PassOnService *service = net->pass_on_service;
    if (*handle == nullptr) return false;
    return give_handle_to_request(make_pass_on_request(service), handle, kind);
}
