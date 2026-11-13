// SPDX-License-Identifier: GPL-2.0-or-later
// Real shared-header consumers on owned canonical TLS, with a safe owned wrong-domain control.
#include "runtime/game_guest_pointer.h"
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
namespace { unsigned char *owned_decoy_thread_pointer(); }
#define rt_game_guest_thread_pointer owned_decoy_thread_pointer
#endif
#include "../temporary_heap_create.cpp"
#include "../temporary_heap_operations.cpp"
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
#undef rt_game_guest_thread_pointer
#endif
#include "runtime/guest_pointer.h"
#include <array>
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
constexpr size_t owned_image_bytes=0x05b00000, storage_bytes=0x20000;
constexpr size_t range_begin=0x100, range_end=0x1000, initial_bottom=0x131;
constexpr uint64_t request_bytes=0x600, rebound_size=request_bytes*2, decoy_size=request_bytes*3;
constexpr uint64_t expected_default_bytes=0x20000, allocation_bytes=13, top_bytes=32, aligned_top_bytes=19;
constexpr uintptr_t alignment=256;
constexpr size_t event_capacity=8, free_event=2, destroy_event_count=3, free_header_offset=range_end/2;
enum { case_argc=3, worker_argc=4, case_index=2, worker_index=3 };
struct OwnedTCB { TemporaryHeap *heap; uint64_t size; uintptr_t self; };
enum class Event { allocate, report, query, allocator, free };
struct State {
    OwnedTCB first{},second{},decoy{};
    TemporaryHeap first_heap{},second_heap{},decoy_heap{};
    BackingAllocator backing{};
    engine::fixed_allocator::BackingAllocatorVtable backing_vtable{};
    alignas(4096) unsigned char storage[storage_bytes]{};
    alignas(4096) unsigned char second_storage[storage_bytes]{};
    alignas(4096) unsigned char decoy_storage[storage_bytes]{};
    std::array<Event,event_capacity> events{};
    unsigned event_count{},allocations{},frees{},reports{};
    bool rebind_allocate{},rebind_free{},allocation_fails{},recover_report{},bad{};
    TemporaryHeap *freed{};
};
thread_local State *active;
std::atomic<unsigned> checks,scenarios;
thread_local const char *case_name;
#define CHECK(v) do { ++checks; if (!(v)) { std::fprintf(stderr,"temporary operations mismatch case=%s line=%d check=%s\n",case_name,__LINE__,#v); return 1; } } while (0)
#if defined(PB_TEST_WRONG_TEMPORARY_TCB)
unsigned char *owned_decoy_thread_pointer() { return reinterpret_cast<unsigned char *>(&active->decoy.self); }
#endif
void event(Event e)
{
    if (active->event_count<active->events.size()) active->events[active->event_count]=e;
    else active->bad=true;
    ++active->event_count;
}
void *allocate(BackingAllocator *allocator,uint64_t size)
{
    event(Event::allocate);++active->allocations;
    if (allocator!=&active->backing || size!=request_bytes) active->bad=true;
    if (active->rebind_allocate) rt_guest_pointer_bind(&active->second.self);
    return active->allocation_fails ? nullptr : active->storage;
}
void release(BackingAllocator *allocator,void *memory)
{
    event(Event::free);++active->frees;active->freed=static_cast<TemporaryHeap *>(memory);
    if (allocator!=&active->backing || memory!=&active->first_heap) active->bad=true;
    const auto &r=active->first_heap.region;
    if (r.lowest || r.highest || r.bottom || r.top) active->bad=true;
    if (active->rebind_free) rt_guest_pointer_bind(&active->second.self);
}
void *create_query() { event(Event::query);quit_query_instance.get()=active;return active; }
BackingAllocator *create_allocator() { event(Event::allocator);return &active->backing; }
void returning_report(const char *file,int32_t line,const char *message,...)
{
    event(Event::report);++active->reports;
    if (file!=source_file.address() || line!=line_out_of_memory || message!=message_out_of_memory.address()) active->bad=true;
    if (active->recover_report) {
        active->second.heap=reinterpret_cast<TemporaryHeap *>(active->storage);
        rt_guest_pointer_bind(&active->second.self);
    }
}
void region(TemporaryHeap &heap,unsigned char *storage)
{
    heap.region={reinterpret_cast<uintptr_t>(storage+range_begin),reinterpret_cast<uintptr_t>(storage+range_end),
                 reinterpret_cast<uintptr_t>(storage+initial_bottom),reinterpret_cast<uintptr_t>(storage+range_end)};
}
struct Binding { ~Binding() { rt_guest_pointer_clear();active=nullptr; } };
void install_jump(void *destination,uintptr_t target)
{
    constexpr size_t jump_bytes=14,target_offset=6;
    constexpr unsigned char indirect_jump=0xff,rip_relative=0x25;
    unsigned char bytes[jump_bytes]={indirect_jump,rip_relative};
    std::memcpy(bytes+target_offset,&target,sizeof target);std::memcpy(destination,bytes,sizeof bytes);
    auto *code=static_cast<char *>(destination);__builtin___clear_cache(code,code+sizeof bytes);
}
constexpr const char *names[]={"create","destroy","set_size","ensure","ensure_copy","allocator","mark","rewind",
    "allocate","allocate_aligned","allocate_top","allocate_aligned_top","free","create_preseeded","create_rebind",
    "create_report_rebind","allocator_rebind","allocator_failed","mark_rebind","destroy_rebind","set_size_default"};
int one_case(const char *name)
{
    case_name=name;State state;active=&state;
    region(state.first_heap,state.storage);region(state.second_heap,state.second_storage);region(state.decoy_heap,state.decoy_storage);
    state.first={&state.first_heap,request_bytes,reinterpret_cast<uintptr_t>(&state.first.self)};
    state.second={&state.second_heap,rebound_size,reinterpret_cast<uintptr_t>(&state.second.self)};
    state.decoy={&state.decoy_heap,decoy_size,reinterpret_cast<uintptr_t>(&state.decoy.self)};
    state.backing_vtable.allocate=allocate;state.backing_vtable.free=release;state.backing.vtable=&state.backing_vtable;
    quit_query_instance.get()=&state;default_allocator.get()=&state.backing;
    const TemporaryHeap saved_decoy=state.decoy_heap;
    rt_guest_pointer_bind(&state.first.self);Binding binding;
    const uintptr_t bottom=state.first_heap.region.bottom,top=state.first_heap.region.top;
    auto *created=reinterpret_cast<TemporaryHeap *>(state.storage);
    if (!std::strcmp(name,"create") || !std::strcmp(name,"create_rebind") || !std::strcmp(name,"create_report_rebind")) {
        state.first.heap=nullptr;
        state.rebind_allocate=!std::strcmp(name,"create_rebind");
        state.allocation_fails=state.recover_report=!std::strcmp(name,"create_report_rebind");
        bb_engine_temporary_heap_create_for_thread_0247d250();
        const bool rebound=state.rebind_allocate || state.recover_report;
        CHECK((rebound ? state.second.heap : state.first.heap)==created);
        CHECK(state.first.heap==(rebound ? nullptr : created));
        CHECK(state.allocations==1 && state.reports==unsigned(state.recover_report));
        CHECK(state.events[0]==Event::allocate && (!state.recover_report || state.events[1]==Event::report));
        CHECK(created->region.lowest==reinterpret_cast<uintptr_t>(state.storage+sizeof(TemporaryHeap)));
        CHECK(created->region.bottom==created->region.lowest);
        CHECK(created->region.highest==reinterpret_cast<uintptr_t>(state.storage+request_bytes));
        CHECK(created->region.top==created->region.highest);
        CHECK(created->vtable==temporary_heap_vtable.address());
        CHECK(created->allocator_vtable==temporary_heap_allocator_vtable.address()+vtable_entries_offset);
        CHECK(created->owner==&created->vtable);
    } else if (!std::strcmp(name,"create_preseeded")) {
        bb_engine_temporary_heap_create_for_thread_0247d250();
        CHECK(state.first.heap==&state.first_heap && state.allocations==0 && state.event_count==0);
    } else if (!std::strcmp(name,"destroy") || !std::strcmp(name,"destroy_rebind")) {
        state.rebind_free=!std::strcmp(name,"destroy_rebind");
        quit_query_instance.get()=nullptr;default_allocator.get()=nullptr;
        bb_engine_temporary_heap_destroy_for_thread_0247d3a0();
        CHECK(state.frees==1 && state.freed==&state.first_heap);
        CHECK(state.events[0]==Event::query && state.events[1]==Event::allocator && state.events[free_event]==Event::free);
        CHECK(state.event_count==destroy_event_count && default_allocator.get()==&state.backing);
        CHECK(state.first.heap==(state.rebind_free ? &state.first_heap : nullptr));
        CHECK(state.second.heap==(state.rebind_free ? nullptr : &state.second_heap));
    } else if (!std::strcmp(name,"set_size") || !std::strcmp(name,"set_size_default")) {
        const bool defaults=!std::strcmp(name,"set_size_default");
        bb_engine_temporary_heap_set_size_0247d430(defaults ? 0 : request_bytes+1);
        CHECK(state.first.size==(defaults ? expected_default_bytes : request_bytes+1));
        CHECK(state.decoy.size==decoy_size);
    } else if (!std::strcmp(name,"ensure") || !std::strcmp(name,"ensure_copy")) {
        state.first.heap=nullptr;
        if (!std::strcmp(name,"ensure")) bb_engine_temporary_heap_ensure_0247d590();
        else bb_engine_temporary_heap_ensure_0247dd30();
        CHECK(state.first.heap==created && state.allocations==1);
    } else if (!std::strcmp(name,"allocator") || !std::strcmp(name,"allocator_rebind") || !std::strcmp(name,"allocator_failed")) {
        const bool rebind=!std::strcmp(name,"allocator_rebind"),failed=!std::strcmp(name,"allocator_failed");
        if (rebind || failed) state.first.heap=nullptr;
        state.rebind_allocate=rebind;state.allocation_fails=failed;
        const uintptr_t result=bb_engine_temporary_heap_allocator_0247d470();
        const uintptr_t expected=failed ? 0 : reinterpret_cast<uintptr_t>(rebind ? created : &state.first_heap);
        CHECK(result==expected+sizeof(engine::linear_heap::Heap));
        CHECK(state.allocations==unsigned(rebind || failed));
        CHECK(!rebind || (state.first.heap==nullptr && state.second.heap==created));
        CHECK(!failed || (state.first.heap==nullptr && state.reports==1));
    } else if (!std::strcmp(name,"mark") || !std::strcmp(name,"mark_rebind")) {
        const bool rebind=!std::strcmp(name,"mark_rebind");
        if (rebind) { state.first.heap=nullptr;state.rebind_allocate=true; }
        const uintptr_t result=bb_engine_temporary_heap_mark_0247d5b0();
        CHECK(result==(rebind ? reinterpret_cast<uintptr_t>(state.storage+sizeof(TemporaryHeap)) : bottom));
        CHECK(!rebind || (state.first.heap==nullptr && state.second.heap==created));
    } else if (!std::strcmp(name,"rewind")) {
        const uintptr_t mark=reinterpret_cast<uintptr_t>(state.storage+range_begin+1);
        bb_engine_temporary_heap_free_until_0247d5f0(mark);CHECK(state.first_heap.region.bottom==mark);
    } else if (!std::strcmp(name,"allocate")) {
        void *result=bb_engine_temporary_heap_allocate_0247d4b0(nullptr,allocation_bytes);
        CHECK(result==reinterpret_cast<void *>(bottom) && state.first_heap.region.bottom==bottom+allocation_bytes);
    } else if (!std::strcmp(name,"allocate_aligned")) {
        constexpr uintptr_t mask=alignment-1;
        const uintptr_t expected=(bottom+mask)&~mask;
        void *result=bb_engine_temporary_heap_allocate_aligned_0247d510(nullptr,allocation_bytes,alignment);
        CHECK(result==reinterpret_cast<void *>(expected) && state.first_heap.region.bottom==expected+allocation_bytes);
        CHECK(reinterpret_cast<uintptr_t>(result)%alignment==0);
    } else if (!std::strcmp(name,"allocate_top") || !std::strcmp(name,"allocate_aligned_top")) {
        const bool aligned=!std::strcmp(name,"allocate_aligned_top");
        const uintptr_t expected=aligned ? (top-aligned_top_bytes)&~uintptr_t(alignment-1) : top-top_bytes;
        void *result=aligned ? bb_engine_temporary_heap_allocate_aligned_top_0247dde0(nullptr,aligned_top_bytes,alignment)
                             : bb_engine_temporary_heap_allocate_top_0247dd60(nullptr,top_bytes);
        CHECK(result==reinterpret_cast<void *>(expected));
        CHECK(state.first_heap.region.top==expected-sizeof(engine::linear_heap::TopHeader));
        const auto *header=reinterpret_cast<engine::linear_heap::TopHeader *>(expected-sizeof(engine::linear_heap::TopHeader));
        CHECK(header->above==top && header->in_use==1);
        CHECK(!aligned || reinterpret_cast<uintptr_t>(result)%alignment==0);
    } else if (!std::strcmp(name,"free")) {
        auto *header=reinterpret_cast<engine::linear_heap::TopHeader *>(state.storage+free_header_offset);
        header->above=top;header->in_use=1;state.first_heap.region.top=reinterpret_cast<uintptr_t>(header);
        bb_engine_temporary_heap_free_0247e1a0(nullptr,header+1);
        CHECK(state.first_heap.region.top==top);
    } else { CHECK(false); }
    CHECK(!state.bad);
    CHECK(state.decoy.heap==&state.decoy_heap && state.decoy.size==decoy_size);
    CHECK(std::memcmp(&saved_decoy,&state.decoy_heap,sizeof saved_decoy)==0);
    ++scenarios;return 0;
}
struct Worker { const char *only;int result=1; };
int cases(const char *only)
{
    if (only) return one_case(only);
    for (const char *name:names) if (one_case(name)) return 1;
    return 0;
}
void *worker(void *opaque) { auto *w=static_cast<Worker *>(opaque);w->result=cases(w->only);return nullptr; }
}
int main(int argc,char **argv)
{
    const char *only=nullptr;bool worker_only=false;
    if (argc>=case_argc && !std::strcmp(argv[1],"--case")) only=argv[case_index];
    if (argc==worker_argc && !std::strcmp(argv[worker_index],"--worker")) worker_only=true;
    case_name="setup";
    CHECK(argc==1 || (only && (argc==case_argc || worker_only)));
    rt_image=static_cast<unsigned char *>(mmap(nullptr,owned_image_bytes,PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));CHECK(rt_image!=MAP_FAILED);
    const int64_t heap_slot=-static_cast<int64_t>(offsetof(OwnedTCB,self));
    const int64_t size_slot=static_cast<int64_t>(offsetof(OwnedTCB,size))-static_cast<int64_t>(offsetof(OwnedTCB,self));
    std::memcpy(const_cast<int64_t *>(thread_heap_offset.address()),&heap_slot,sizeof heap_slot);
    std::memcpy(const_cast<int64_t *>(thread_heap_size_offset.address()),&size_slot,sizeof size_slot);
    install_jump(create_for_thread.address(),reinterpret_cast<uintptr_t>(bb_engine_temporary_heap_create_for_thread_0247d250));
    install_jump(quit_query_create.address(),reinterpret_cast<uintptr_t>(create_query));
    install_jump(default_allocator_create.address(),reinterpret_cast<uintptr_t>(create_allocator));
    install_jump(engine::fatal_error.address(),reinterpret_cast<uintptr_t>(returning_report));
    CHECK(rt_game_guest_pointer_initialize()==0);
    int result=worker_only ? 0 : cases(only);
    if (!result) {
        pthread_t thread;Worker work{only};CHECK(pthread_create(&thread,nullptr,worker,&work)==0);
        CHECK(pthread_join(thread,nullptr)==0);result=work.result;
    }
    CHECK(munmap(rt_image,owned_image_bytes)==0);rt_image=nullptr;
    if (result) { std::printf("temporary_operations_owned_data_mismatch case=%s thread=%s returned=1\n",only ? only : "all",worker_only ? "worker" : "main");return 1; }
    std::printf("temporary operations guest TLS: %u scenarios; %u checks passed\n",scenarios.load(),checks.load());return 0;
}
