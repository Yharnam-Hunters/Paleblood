/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network control (libSceNetCtl). The runtime is offline, so these report
   "not connected" the way the system does and the guest takes its offline paths.

   Facts used, and the evidence for each.

   The imported names are the whole of what the executable wants from this library:
   sceNetCtlCheckCallback, sceNetCtlGetInfo, sceNetCtlGetNatInfo, sceNetCtlGetState,
   sceNetCtlRegisterCallback and sceNetCtlUnregisterCallback, with no sceNetCtlInit or
   sceNetCtlTerm (symbols/imports.csv, written by tools/boot.py). A library the guest never
   initialises still has to answer a state query, which is consistent with the state and
   information calls reading system-wide state rather than a per-process session.

   The PS4 calls these with the System V AMD64 convention (runtime/include/runtime/syslib.h). The
   loader binds an import by NID, which is the symbol name hashed (runtime/nid.c), so nothing but
   the signatures below and test/test_syslib.c states an argument count: each signature here is a
   claim about the guest's ABI.
     -  Argument shape comes from the public Vita-era declarations of the same API family,
     include/psp2/net/netctl.h in vitasdk/vita-headers and the PSP2SDK header reference at
     psp2sdk.github.io/netctl_8h.html: a state query takes `int *state`, an information query
     takes an `int` module id and a pointer, a NAT query takes one pointer, and the callback check
     takes nothing and returns `int`. Vita is a different operating system: these declarations are
     evidence for the shape of the calls, never proof of a PS4 value.
     -  The registration pair is keyed by a callback id the system hands back, which the guest's
     own code establishes. Its call at 0x010b8352 passes a function in the image (`lea
     -0x3d6b(%rip)`), `this`, and `lea -0x78(%rbp)`, a stack slot it never writes; it reads that
     slot back at 0x010b8428, stores it into its object at +0x130, and passes that field as the
     only argument of the unregister call at 0x010b8f8d. So the third argument is an out-parameter
     and the id is the caller's handle - the Vita-era shape (`func, arg, int *cid` / `int cid`),
     not state keying. The callback's dispatch signature is observed as well: the guest's callback
     at 0x010b45e0 tests its second argument for null and compares its first against the connected
     state. We only store the callback and never call it, so its own signature needs no claim -
     the two public Vita-era sources disagree with each other about its return type, which is
     reason enough to keep the pointer opaque.
     -  0 means success. Public SCE-family documentation states it as the return convention (the
     `@return 0 on success` lines in include/psp2/error.h of vita-headers), and this runtime
     already uses 0 for success in libkernel's entries. libSceNetCtl's own error codes have no
     public source at all, so no path below returns an error value: an input that would need one
     is a fatal instead, naming the missing fact.

   Everything PS4-specific and not established is left unresolved and fails loudly: see the
   questions
   at each rt_fatal and docs/runtime-syscalls.md. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* The state an offline runtime reports.

   The evidence for the number is the guest's own use of it. One call site writes the state to a
   stack slot and tests that slot against 0 at 0x00dfd260 to decide whether to keep waiting;
   another bounds it with `cmp $0x3; ja` before indexing a four-entry table at 0x020413f3, so the
   states this guest can see are closed at {0, 1, 2, 3}; and equality with 3 is the connected
   branch at 0x010b839f, as it is in the callback the guest registers (0x010b45e5, comparing its
   first argument against 3). So 0 is "not up" and 3 is "connected". The Vita-era declarations of
   this family (SceNetCtlState in include/psp2/net/netctl.h of vita-headers) name the same two
   ends first and last, which supplies the enumerator names but not the numbers. Still
   unestablished: what 1 and 2 mean, and any state beyond the ones this guest tests - the official
   header or one observed call on a console settles
   those. A guest that goes on to take an online path here has proven this constant wrong. */
enum { NETCTL_STATE_DISCONNECTED = 0 };

/* The registrations the guest has made, each under the id we handed back. Nothing is ever called: the
   runtime never leaves the disconnected state, so no state change is ever due. */
typedef struct netctl_callback {
    struct netctl_callback *next;
    int id;         /* our handle, written back through the caller's pointer */
    void *function; /* kept opaque: we never call it, so its signature stays unmodelled */
    void *arg;
} netctl_callback;

static pthread_mutex_t netctl_lock = PTHREAD_MUTEX_INITIALIZER;
static netctl_callback *netctl_callbacks;

/* Ids start at 1 so an id can never look like a callback-id slot the guest failed to fill in: it
   reads the slot straight back (0x010b8428) and stores it, so 0 would be indistinguishable from
   an
   unwritten value. */
static int netctl_next_id = 1;

/* Reports the connection state. Offline, the answer is the disconnected state, and the call
   succeeds: an error here would mean the system itself has no answer, which is not this case. */
static int netctl_get_state(int *state)
{
    /* rt_fatal: what does the PS4 return for a null state pointer? No public source gives
       libSceNetCtl's error codes. The official header, or one call with a null pointer on a
       console,
       settles it. */
    if (!state) rt_fatal("sceNetCtlGetState", "the PS4 error value for a null state pointer is not established");
    *state = NETCTL_STATE_DISCONNECTED;
    return 0;
}
RT_SYSLIB("libSceNetCtl", sceNetCtlGetState, netctl_get_state);

/* Runs the callbacks made due by a connection state change. The runtime is offline for its whole
   life, so no change is ever due and no registered callback runs here.

   The value to return is unresolved: sceNetCtlCheckCallback may return a plain success code or
   the number of callbacks it ran. The public Vita-era documentation gives only the declaration,
   so neither reading is established as the PS4's. Offline both readings are 0, which is the one
   answer that needs no guess. The moment the runtime reports a connected state this has to become
   a fatal,
   because the two readings would then differ. */
static int netctl_check_callback(void) { return 0; }
RT_SYSLIB("libSceNetCtl", sceNetCtlCheckCallback, netctl_check_callback);

/* Registers a callback for a connection state change and hands back the id that names it. */
static int netctl_register_callback(void *callback, void *arg, int *id)
{
    /* rt_fatal: does the PS4 accept a null callback, and what is its error value if it rejects one?
       libSceNetCtl's error codes are not public. A disassembly of libSceNetCtl's own entry, or
       the
       header's parameter documentation, would settle it. */
    if (!callback) rt_fatal("sceNetCtlRegisterCallback", "the PS4 answer for a null callback is not established");
    /* rt_fatal: a null callback-id slot. The guest always passes one (0x010b834b), so nothing here
       shows what the system does without it, and writing nothing back would leave the caller
       reading
       an uninitialised slot. */
    if (!id) rt_fatal("sceNetCtlRegisterCallback", "the PS4 answer for a null callback-id pointer is not established");
    pthread_mutex_lock(&netctl_lock);
    netctl_callback *c = calloc(1, sizeof *c);
    if (!c) {
        pthread_mutex_unlock(&netctl_lock);
        /* rt_fatal: the error value for a registration the system could not keep (its callback table
           full, presumably: the size of that table is also not established). */
        rt_fatal("sceNetCtlRegisterCallback", "the PS4 error value for a registration it could not keep is not established");
    }
    c->id = netctl_next_id++;
    c->function = callback;
    c->arg = arg;
    c->next = netctl_callbacks;
    netctl_callbacks = c;
    *id = c->id;
    pthread_mutex_unlock(&netctl_lock);
    return 0;
}
RT_SYSLIB("libSceNetCtl", sceNetCtlRegisterCallback, netctl_register_callback);

/* Removes the registration named by an id from a previous register call. */
static int netctl_unregister_callback(int id)
{
    pthread_mutex_lock(&netctl_lock);
    netctl_callback **link = &netctl_callbacks;
    while (*link && (*link)->id != id) link = &(*link)->next;
    const int removed = *link != NULL;
    if (removed) {
        netctl_callback *gone = *link;
        *link = gone->next;
        free(gone);
    }
    pthread_mutex_unlock(&netctl_lock);
    /* rt_fatal: what does the PS4 do for an id it never handed back, or one already unregistered -
       report it, and with which error value, or succeed quietly? The guest caches its id at
       +0x130 and unregisters that, so the case does not arise in the paths we can see. The
       official header
       or one console call settles it. */
    if (!removed) rt_fatal("sceNetCtlUnregisterCallback", "the PS4 answer for an id it never handed back is not established");
    return 0;
}
RT_SYSLIB("libSceNetCtl", sceNetCtlUnregisterCallback, netctl_unregister_callback);

/* Reads one piece of network information into the caller's structure.

   The layout and the answer are still not established, so the call stops before touching the
   buffer: the Vita-era SceNetCtlInfo union is another operating system's, and nothing public
   describes the PS4 structure. What the guest does tell us is which questions it asks: every call
   site in the executable passes module id 14 (0x00dfd2b1 and 0x020413ee, each with a buffer in
   the second argument) and prints that buffer as a string on the success path, and no other
   module id appears anywhere. That is evidence about the guest, not about the system: what id 14
   means, what the offline answer is, and how large the structure is stay open until the official
   header or a console answers them.

   The module id is named in the message because the guest's own requests are the evidence for
   which
   ids matter: the boot log shows what the guest actually asks for. */
static int netctl_get_info(int module, void *info)
{
    (void)info; /* never read: no established PS4 layout to write through */
    char question[96];
    snprintf(question, sizeof question, "no established PS4 answer for module id %d", module);
    rt_fatal("sceNetCtlGetInfo", question);
}
RT_SYSLIB_UNRESOLVED("libSceNetCtl", sceNetCtlGetInfo, netctl_get_info);

/* Reports the NAT type of the connection.

   The guest's own sites bound the structure: each writes a size of 16 at offset 0 before the call
   (0x01889417) and reads back only offset 8 afterwards (0x01889436), which matches the Vita-era
   SceNetCtlNatInfo opening with a caller-set size. Not established is the PS4 meaning of the
   value at offset 8 and what a system with no connection reports there, so the call stops before
   writing through it. The reported size is named in the message: a caller saying something other
   than 16 is
   itself evidence that the layout is not what the sites suggest. */
static int netctl_get_nat_info(void *nat_info)
{
    char question[160];
    /* rt_fatal: the PS4 answer for a null structure pointer (its error codes are not public), and
       reading the size below needs the pointer to exist first. */
    if (!nat_info) rt_fatal("sceNetCtlGetNatInfo", "the PS4 answer for a null structure pointer is not established");
    /* Read only: the size at offset 0 is the one field the guest's own sites establish. */
    snprintf(question, sizeof question,
             "the PS4 meaning of the value at offset 8 and the offline NAT type are not established (caller reports %d bytes)",
             *(const int *)nat_info);
    rt_fatal("sceNetCtlGetNatInfo", question);
}
RT_SYSLIB_UNRESOLVED("libSceNetCtl", sceNetCtlGetNatInfo, netctl_get_nat_info);
