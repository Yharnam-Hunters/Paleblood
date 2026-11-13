// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real initializer on qualified owned TLS; no guest executable is loaded.
#include "runtime/game_guest_pointer.h"
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
namespace { unsigned char *owned_decoy_thread_pointer(); }
#define rt_game_guest_thread_pointer owned_decoy_thread_pointer
#endif
#include "../temporary_heap_create.cpp"
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
#undef rt_game_guest_thread_pointer
#endif
#include "runtime/guest_pointer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>

extern "C" {
unsigned char *rt_image;
rt_game_guest_offset_fn rt_game_guest_offset_provider;
}

namespace {
using engine::fixed_allocator::BackingAllocatorVtable;
enum class Scenario { preseeded, lazy, allocator_rebind, fatal_rebind };
enum class Event { allocation=1, report=2 };
constexpr unsigned event_capacity=2;
constexpr size_t guard_count=2, guard_bytes=8, expected_header_bytes=0x38, expected_default_bytes=0x20000;
constexpr uint64_t requested_bytes=0x80, rebound_setting_bytes=0x100;
constexpr unsigned char guard_value=0x6b;
constexpr ptrdiff_t allocator_vtable_header_bytes=0x10;
struct OwnedTCB {
    TemporaryHeap *heap;
    uint64_t size;
    uintptr_t self;
};
struct State {
    OwnedTCB first{},second{},decoy{};
    TemporaryHeap decoy_heap{};
    BackingAllocator backing{};
    BackingAllocatorVtable vtable{};
    alignas(16) unsigned char storage[expected_default_bytes+guard_bytes*guard_count];
    Scenario scenario{};
    uint64_t expected_size{};
    Event events[event_capacity]{};
    unsigned event_count{},allocations{};
    bool bad{};
};
thread_local State *active;
std::atomic<unsigned> checks,scenarios;
#define CHECK(v) do { ++checks; if (!(v)) { std::fprintf(stderr,"temporary heap TLS line%d: %s\n",__LINE__,#v); return 1; } } while (0)
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
unsigned char *owned_decoy_thread_pointer()
{
    return reinterpret_cast<unsigned char *>(&active->decoy.self);
}
#endif
void event(Event value)
{
    if (active->event_count<event_capacity) active->events[active->event_count]=value;
    else active->bad=true;
    ++active->event_count;
}
void *allocate(BackingAllocator *allocator,uint64_t size)
{
    event(Event::allocation);++active->allocations;
    if (allocator!=&active->backing || size!=active->expected_size) active->bad=true;
    if (active->scenario==Scenario::allocator_rebind) rt_guest_pointer_bind(&active->second.self);
    return active->scenario==Scenario::fatal_rebind ? nullptr : active->storage+guard_bytes;
}
void returning_fatal(const char *file,int32_t line,const char *message,...)
{
    event(Event::report);
    if (active->scenario!=Scenario::fatal_rebind || file!=source_file.address() ||
        line!=line_out_of_memory || message!=message_out_of_memory.address() || active->first.heap)
        active->bad=true;
    active->second.heap=reinterpret_cast<TemporaryHeap *>(active->storage+guard_bytes);
    rt_guest_pointer_bind(&active->second.self);
}
struct Binding {
    ~Binding() { rt_guest_pointer_clear();active=nullptr; }
};
int one_case(Scenario scenario)
{
    State state;active=&state;state.scenario=scenario;
    std::memset(state.storage,guard_value,sizeof state.storage);
    state.vtable.allocate=allocate;state.backing.vtable=&state.vtable;
    state.first.self=reinterpret_cast<uintptr_t>(&state.first.self);
    state.second.self=reinterpret_cast<uintptr_t>(&state.second.self);
    state.decoy.self=reinterpret_cast<uintptr_t>(&state.decoy.self);
    state.decoy.heap=&state.decoy_heap;
    state.first.size=scenario==Scenario::lazy ? 0 : requested_bytes;
    state.second.size=rebound_setting_bytes; // A callback must not retroactively change the captured request size.
    state.expected_size=state.first.size ? state.first.size : expected_default_bytes;
    auto *expected=reinterpret_cast<TemporaryHeap *>(state.storage+guard_bytes);
    if (scenario==Scenario::preseeded) state.first.heap=expected;
    quit_query_instance.get()=&state;default_allocator.get()=&state.backing;
    rt_guest_pointer_bind(&state.first.self);Binding bound;
    bb_engine_temporary_heap_create_for_thread_0247d250();
    const bool rebound=scenario==Scenario::allocator_rebind || scenario==Scenario::fatal_rebind;
    OwnedTCB *target=rebound ? &state.second : &state.first;
    // A safe wrong-domain control returns an owned decoy, never host GS:0. Check the result
    // before dereferencing an allocator or heap; an early return leaves the guest slot null.
    if (scenario==Scenario::lazy && !state.first.heap && state.decoy.heap==&state.decoy_heap && !state.allocations)
        std::puts("temporary_heap_wrong_getter guest_null=1 decoy_preserved=1 allocation_calls=0");
    CHECK(target->heap==expected);
    CHECK(state.first.heap==(rebound ? nullptr : expected));
    CHECK(state.second.heap==(rebound ? expected : nullptr));
    CHECK(state.decoy.heap==&state.decoy_heap);
    CHECK(rt_game_guest_thread_pointer()==reinterpret_cast<unsigned char *>(&target->self));
    CHECK(!state.bad);
    const unsigned events=scenario==Scenario::preseeded ? 0u : scenario==Scenario::fatal_rebind ? event_capacity : 1u;
    CHECK(state.event_count==events && state.allocations==(events ? 1u : 0u));
    CHECK(!events || state.events[0]==Event::allocation);
    CHECK(events<event_capacity || state.events[1]==Event::report);
    if (scenario==Scenario::preseeded) {
        CHECK(std::all_of(state.storage,state.storage+sizeof state.storage,[](unsigned char v){return v==guard_value;}));
    } else {
        CHECK(expected->start==state.storage+guard_bytes+expected_header_bytes && expected->current_start==expected->start);
        CHECK(expected->end==state.storage+guard_bytes+state.expected_size && expected->current_end==expected->end);
        CHECK(expected->vtable==temporary_heap_vtable.address());
        CHECK(expected->allocator_vtable==temporary_heap_allocator_vtable.address()+allocator_vtable_header_bytes);
        CHECK(expected->owner==&expected->vtable);
        CHECK(std::all_of(state.storage,state.storage+guard_bytes,[](unsigned char v){return v==guard_value;}));
        CHECK(std::all_of(state.storage+guard_bytes+expected_header_bytes,state.storage+sizeof state.storage,[](unsigned char v){return v==guard_value;}));
    }
    ++scenarios;return 0;
}
int cases()
{
    for (Scenario scenario : {Scenario::preseeded,Scenario::lazy,Scenario::allocator_rebind,Scenario::fatal_rebind})
        if (one_case(scenario)) return 1;
    return 0;
}
void *worker(void *out) { *static_cast<int *>(out)=cases();return nullptr; }
void install_jump(void *destination,uintptr_t target)
{
    constexpr size_t jump_bytes=14, target_offset=6;
    constexpr unsigned char indirect_jump=0xff, rip_relative=0x25;
    unsigned char jump[jump_bytes]={indirect_jump,rip_relative};
    std::memcpy(jump+target_offset,&target,sizeof target);std::memcpy(destination,jump,sizeof jump);
    auto *code=static_cast<char *>(destination);__builtin___clear_cache(code,code+sizeof jump);
}
}
int main()
{
    constexpr size_t owned_image_bytes=0x05800000;
    rt_image=static_cast<unsigned char *>(mmap(nullptr,owned_image_bytes,PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(rt_image!=MAP_FAILED);
    const int64_t heap_slot=-static_cast<int64_t>(offsetof(OwnedTCB,self));
    const int64_t size_slot=static_cast<int64_t>(offsetof(OwnedTCB,size))-static_cast<int64_t>(offsetof(OwnedTCB,self));
    std::memcpy(const_cast<int64_t *>(thread_heap_offset.address()),&heap_slot,sizeof heap_slot);
    std::memcpy(const_cast<int64_t *>(thread_heap_size_offset.address()),&size_slot,sizeof size_slot);
    install_jump(engine::fatal_error.address(),reinterpret_cast<uintptr_t>(returning_fatal));
    CHECK(rt_game_guest_pointer_initialize()==0);
    if (cases()) return 1;
    pthread_t thread;int result=1;
    CHECK(pthread_create(&thread,nullptr,worker,&result)==0);
    CHECK(pthread_join(thread,nullptr)==0 && result==0);
    CHECK(munmap(rt_image,owned_image_bytes)==0);rt_image=nullptr;
    std::printf("temporary heap guest TLS: 4 cases on main+worker; %u scenarios; %u checks passed\n",scenarios.load(),checks.load());
    return 0;
}
