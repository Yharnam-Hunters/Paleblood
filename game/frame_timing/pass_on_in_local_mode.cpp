// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the Chalice Dungeon gate's online lookups through FrpgNetMan: passing the gate's
// handle on in the local modes (0x02265170) and finding or making the object for a session id
// (0x02265290).
//
// Both first test FrpgNetMan's four bytes as the gate's online checks do (+0x8 set, +0x9f6, +0x9f8
// and +0xa50 clear; what they mean is not known), then work with FrpgNetMan's object at +0xca8:
//
// - pass_on_in_local_mode: the gate's two pass-on methods (0x01ea5e20, 0x01ea6080) call it with
//   the address of the owner's handle and a kind (1 or 2) when the owner is in a local ritual or
//   dungeon mode. When the bytes allow it, it makes a request from the object (0x02264640, made
//   even with an empty handle) and gives the request the handle and the kind (0x02269d40),
//   reporting what that call reports (true once the request holds the handle). Otherwise it sets
//   the handle object's words at +0x11c and +0x120 to 2 (one 64-bit store in the original) and
//   reports false. The callers ignore the answer.
// - lookup_session_object: the owner check (0x01ea5280) calls it with the place for the object
//   and the id. When the bytes do not allow it, it stores null. Otherwise it searches the object's
//   list at +0x50 for an entry whose id (+0x10) is the id; with none, it allocates 0x130 bytes from
//   the heap at 0x05940438, builds an entry for the id in them (0x02267200; nothing on a failed
//   allocation) and appends it, null or not (0x02266b50). Then it makes a request from the object
//   (0x02264640) and gives it the entry, holding a reference over the call (0x02269870), and
//   stores the entry with a reference the caller drops. It returns the place it filled.
//
// A missing FrpgNetMan is reported and the singleton read again, as the game's singleton check
// does (engine::require). Once the bytes are read the original tests the pointer again before its
// second use and would report it a second (and third) time; those reports can never be reached,
// since a still-missing FrpgNetMan faults on the read of +0x8, here as there.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "runtime/original.h"

namespace {

// An entry of the object's list: a reference-counted object made for an id (0x130 bytes).
struct SessionObject {
    engine::RefObject base;
    int64_t id;
    uint8_t unknown_0x018[0x118];
};
static_assert(offsetof(SessionObject, id) == 0x10 && sizeof(SessionObject) == 0x130);
constexpr uint64_t session_object_alignment = 8;

// The list of entries: a vector of pointers whose first field is not read here.
struct SessionObjectList {
    void *unknown_0x00;
    SessionObject **begin;
    SessionObject **end;
};
static_assert(offsetof(SessionObjectList, begin) == 0x8 && offsetof(SessionObjectList, end) == 0x10);

// FrpgNetMan's object at +0xca8: it keeps the entries and makes requests; its class is not known.
struct SessionService {
    uint8_t unknown_0x00[0x50];
    SessionObjectList objects;
};
static_assert(offsetof(SessionService, objects) == 0x50);
struct PassOnRequest;  // what 0x02264640 makes from the service (0xe0 bytes)

// FrpgNetMan, the online manager: the four bytes the online checks test and the service.
struct FrpgNetMan {
    uint8_t unknown_0x000[0x8];
    uint8_t flag_008;  // must be nonzero
    uint8_t unknown_0x009[0x9ed];
    uint8_t flag_9f6;  // must be zero
    uint8_t unknown_0x9f7[0x1];
    uint8_t flag_9f8;  // must be zero
    uint8_t unknown_0x9f9[0x57];
    uint8_t flag_a50;  // must be zero
    uint8_t unknown_0xa51[0x257];
    SessionService *service;
};
static_assert(offsetof(FrpgNetMan, flag_008) == 0x8 && offsetof(FrpgNetMan, flag_9f6) == 0x9f6 &&
              offsetof(FrpgNetMan, flag_9f8) == 0x9f8 && offsetof(FrpgNetMan, flag_a50) == 0xa50 &&
              offsetof(FrpgNetMan, service) == 0xca8);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);
// The heap the entries come from.
RT_GLOBAL(0x05940438, session_allocator_instance, engine::Allocator *);

// The gate's handle object, as far as the local pass-on writes it.
struct HandleObject {
    engine::RefObject base;
    uint8_t unknown_0x010[0x10c];
    uint32_t value_11c;
    uint32_t value_120;
};
static_assert(offsetof(HandleObject, value_11c) == 0x11c && offsetof(HandleObject, value_120) == 0x120);
// What the local pass-on sets both words to (the request path, 0x02269d40, sets them to 1).
constexpr uint32_t passed_on_locally = 2;

// The id the owner check is given (its first 64 bits).
struct SessionId {
    int64_t value;
};

// Makes a request from the service (not replaced; may answer null when its allocation fails).
RT_ORIGINAL(0x02264640, make_request, PassOnRequest *(SessionService *service));
// Gives the request the handle (holding a reference) and the kind, sets the handle object's words
// at +0x11c and +0x120 to 1 and sends the request on; false only when the handle is empty (not
// replaced).
RT_ORIGINAL(0x02269d40, give_handle_to_request, bool(PassOnRequest *request, engine::RefObject **handle,
                                                     int32_t kind));
// Builds an entry for the id in the memory (not replaced).
RT_ORIGINAL(0x02267200, build_session_object, void(void *memory, const SessionId *id));
// Appends an entry to the list (not replaced).
RT_ORIGINAL(0x02266b50, append_session_object, void(SessionObjectList *list, SessionObject **object));
// Gives the request the entry (not replaced; what it reports is not used).
RT_ORIGINAL(0x02269870, give_object_to_request, void(PassOnRequest *request, SessionObject **object));

// FrpgNetMan's four bytes, as the gate's online checks test them.
bool net_allows(const FrpgNetMan *net)
{
    return net->flag_008 != 0 && net->flag_9f8 == 0 && net->flag_9f6 == 0 && net->flag_a50 == 0;
}

// The list's entry for the id, or null; the list's ends and the id are read once.
SessionObject *find_session_object(const SessionObjectList &list, int64_t id)
{
    SessionObject **const end = list.end;
    for (SessionObject **entry = list.begin; entry != end; ++entry)
        if ((*entry)->id == id) return *entry;
    return nullptr;
}

}  // namespace

extern "C" bool bb_frame_timing_pass_on_in_local_mode_02265170(engine::RefObject **handle, int32_t kind)
{
    const FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    if (net_allows(net)) return give_handle_to_request(make_request(net->service), handle, kind);
    if (*handle != nullptr) {
        HandleObject *object = reinterpret_cast<HandleObject *>(*handle);
        object->value_11c = passed_on_locally;
        object->value_120 = passed_on_locally;
    }
    return false;
}

extern "C" engine::RefObject **bb_frame_timing_lookup_session_object_02265290(engine::RefObject **found,
                                                                             const SessionId *id)
{
    const FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    if (!net_allows(net)) {
        *found = nullptr;
        return found;
    }
    SessionService *service = net->service;
    SessionObject *object = find_session_object(service->objects, id->value);
    if (object == nullptr) {
        void *memory = session_allocator_instance.get()->allocate(sizeof(SessionObject), session_object_alignment);
        if (memory != nullptr) {
            build_session_object(memory, id);
            object = static_cast<SessionObject *>(memory);
        }
        SessionObject *appended = object;
        append_session_object(&service->objects, &appended);
    }
    PassOnRequest *request = make_request(service);
    // The request is given its own reference, dropped after the call (the value passed, whatever
    // the call leaves in its slot).
    SessionObject *passed = object;
    if (object != nullptr) engine::retain(&object->base);
    give_object_to_request(request, &passed);
    if (object != nullptr) engine::release(&object->base);
    *found = object ? &object->base : nullptr;
    if (object != nullptr) engine::retain(&object->base);
    return found;
}
