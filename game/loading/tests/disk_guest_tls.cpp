// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the actual allocator helper on owned guest TLS. Original-call addresses name only
// writable owned image storage here; the initializer is our own function, never game bytes.
#include "../disk_device_open.cpp"
#include "runtime/guest_pointer.h"
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
struct OwnedTCB {
    ThreadHeap *heap;
    uintptr_t self;
};
struct State {
    OwnedTCB first{},second{};
    ThreadHeap heap{};
    engine::AllocatorVtable vtable{};
    unsigned initialized{},queried{};
    bool rebind{};
    bool bad_info{};
};
thread_local State *active;
std::atomic<unsigned> checks;
#define CHECK(v) do { ++checks; if (!(v)) { std::fprintf(stderr,"disk TLS line%d: %s\n",__LINE__,#v); return 1; } } while (0)

void initialize_heap()
{
    ++active->initialized;
    OwnedTCB *target=active->rebind ? &active->second : &active->first;
    target->heap=&active->heap;
    if (active->rebind) rt_guest_pointer_bind(&target->self);
}
void heap_info(engine::AllocatorInfo *info,engine::Allocator *heap,int32_t option)
{
    ++active->queried;
    if (heap!=&active->heap.allocator || option) active->bad_info=true;
    *info={};info->flags=engine::allocator_info_valid;
}
struct Binding {
    ~Binding() { rt_guest_pointer_clear(); active=nullptr; }
};
enum class Scenario { preseeded, lazy, rebound };
int one_case(Scenario scenario)
{
    State state;active=&state;
    state.vtable.info=heap_info;state.heap.allocator.vtable=&state.vtable;
    state.first.self=reinterpret_cast<uintptr_t>(&state.first.self);
    state.second.self=reinterpret_cast<uintptr_t>(&state.second.self);
    state.rebind=scenario==Scenario::rebound;
    if (scenario==Scenario::preseeded) state.first.heap=&state.heap;
    rt_guest_pointer_bind(&state.first.self);Binding bound;
    engine::Allocator *actual=thread_temporary_allocator();
    // Check the pointer before using it: a wrong-slot implementation fails without dereferencing
    // a null/foreign allocator. The real container info operation is checked only on owned data.
    CHECK(actual==&state.heap.allocator);
    CHECK(state.initialized==(scenario==Scenario::preseeded ? 0u : 1u));
    CHECK(rt_game_guest_thread_pointer()==reinterpret_cast<unsigned char *>(
        scenario==Scenario::rebound ? &state.second.self : &state.first.self));
    engine::check_container_heap(actual);
    CHECK(state.queried==1 && !state.bad_info);
    CHECK(state.first.heap==(scenario==Scenario::rebound ? nullptr : &state.heap));
    return 0;
}
int cases()
{
    const Scenario scenarios[]={Scenario::preseeded,Scenario::lazy,Scenario::rebound};
    for (Scenario scenario : scenarios) if (one_case(scenario)) return 1;
    return 0;
}
void *worker(void *out)
{
    *static_cast<int *>(out)=cases();return nullptr;
}
}

int main()
{
    constexpr size_t owned_image_bytes=0x05400000;
    rt_image=static_cast<unsigned char *>(mmap(nullptr,owned_image_bytes,PROT_READ|PROT_WRITE|PROT_EXEC,
                                             MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(rt_image!=MAP_FAILED);
    const int64_t heap_slot=-static_cast<int64_t>(offsetof(OwnedTCB,self));
    std::memcpy(const_cast<int64_t *>(thread_heap_offset.address()),&heap_slot,sizeof heap_slot);
    // A normal absolute indirect jump to our owned initializer preserves the original void ABI.
    constexpr size_t absolute_jump_bytes=14, jump_target_offset=6;
    constexpr uint8_t indirect_jump_opcode=0xff, rip_relative_jump_operand=0x25;
    unsigned char jump[absolute_jump_bytes]={indirect_jump_opcode,rip_relative_jump_operand};
    const uintptr_t target=reinterpret_cast<uintptr_t>(initialize_heap);
    std::memcpy(jump+jump_target_offset,&target,sizeof target);
    std::memcpy(thread_heap_init.address(),jump,sizeof jump);
    auto *code=static_cast<char *>(thread_heap_init.address());
    __builtin___clear_cache(code,code+sizeof jump);
    CHECK(rt_game_guest_pointer_initialize()==0);
    CHECK(cases()==0);
    pthread_t thread;int result=1;
    CHECK(pthread_create(&thread,nullptr,worker,&result)==0);
    CHECK(pthread_join(thread,nullptr)==0 && result==0);
    CHECK(munmap(rt_image,owned_image_bytes)==0);rt_image=nullptr;
    std::printf("disk guest TLS: preseed/lazy/rebind on main+worker; %u checks passed\n",checks.load());
    return 0;
}
