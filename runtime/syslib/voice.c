/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. Voice chat (libSceVoice), on a console with no headset and no one to
   talk to: the library works, its ports carry no sound.

   The executable's voice chat (0x010c8000) initializes the library (sceVoiceInit(param, 100) at
   0x010c8069, 0 is success), then creates a microphone port (type 0) and a speaker port (type 5)
   for the initial user (0x010c81bc, 0x010c824e; 0 is success, and on failure it deletes the port
   and keeps id 0xff). Its microphone loop (0x010c8a50) asks sceVoiceGetPortInfo and reads from the
   port only while the info's byte count is not 0. Peer ports, connections, Start and the incoming
   packet path (0x010c83f2 to 0x010c8df7) are reached only with an online peer. Teardown deletes
   ports (also id 0xff), disconnects, stops and ends, ignoring every answer.

   Facts used (facts only): shadPS4's voice.cpp and voice.h, PS4Recomp's ps4_voice.c, the droogie
   bbhost runtime and a family of native ports (the "native runtime" family) all answer success for these calls with no
   audio behind them; the port info is 0x20 bytes (type, state, edge pointer, byte count at 0x10,
   frame size at 0x14, edge count at 0x18; shadPS4's voice.h, matching the executable's reads), and
   shadPS4 reports a frame size of 1, which the incoming packet path divides by. Their error codes
   disagree (shadPS4 0x804e08xx, the PS Vita's numbers; PS4Recomp 0x80233xxx), so misuse stops by
   name instead of answering either.

   What the runtime does: Init answers 0; CreatePort hands out the lowest free id from 0 and answers
   0; DeletePort answers 0 for any id (the executable also deletes 0xff after a failed create, and
   ignores the answer); Connect, Disconnect, Start, Stop and End answer 0 (End forgets every port);
   GetPortInfo zeroes the info, writes the port's type, frame size 1 and byte count 0, and answers
   0; ReadFromOPort reads 0 bytes; WriteToIPort discards the data. What stops by name: Init twice,
   any other call before Init, a NULL pointer, an id that names no port (except for DeletePort),
   and more ports than the table holds. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

enum { MAX_PORTS = 32, PORT_INFO_BYTES = 0x20, FRAME_SIZE = 1 };

typedef struct {
    int32_t type;
    int32_t state;
    uint64_t edge;
    uint32_t byte_count, frame_size;
    uint16_t edge_count, reserved16;
    uint32_t reserved;
} port_info;
_Static_assert(sizeof(port_info) == PORT_INFO_BYTES, "the voice port info is 0x20 bytes");

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static int port_used[MAX_PORTS];
static int32_t port_type[MAX_PORTS];

/* Stops by name unless the library is initialized; called with the lock held. */
static void require_init(const char *caller)
{
    if (initialized) return;
    pthread_mutex_unlock(&lock);
    rt_fatal(caller, "a call before sceVoiceInit: the error value is not established");
}

/* Stops by name unless id names a port; called with the lock held. */
static void require_port(const char *caller, uint32_t id)
{
    if (id < MAX_PORTS && port_used[id]) return;
    pthread_mutex_unlock(&lock);
    rt_fatal(caller, "a port id that names no port: the error value is not established");
}

static int32_t voice_init(const void *param, uint32_t version)
{
    static const char fn[] = "sceVoiceInit";
    (void)version;
    if (!param) rt_fatal(fn, "no parameter: the error value is not established");
    pthread_mutex_lock(&lock);
    if (initialized) {
        pthread_mutex_unlock(&lock);
        rt_fatal(fn, "a second init: the error value is not established");
    }
    initialized = 1;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_end(void)
{
    pthread_mutex_lock(&lock);
    require_init("sceVoiceEnd");
    initialized = 0;
    memset(port_used, 0, sizeof port_used);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_create_port(uint32_t *id, const int32_t *param)
{
    static const char fn[] = "sceVoiceCreatePort";
    if (!id || !param) rt_fatal(fn, "no id or parameter: the error value is not established");
    pthread_mutex_lock(&lock);
    require_init(fn);
    uint32_t slot = 0;
    while (slot < MAX_PORTS && port_used[slot]) slot++;
    if (slot == MAX_PORTS) {
        pthread_mutex_unlock(&lock);
        rt_fatal(fn, "more ports than the runtime's table holds");
    }
    port_used[slot] = 1, port_type[slot] = param[0];
    pthread_mutex_unlock(&lock);
    *id = slot;
    return 0;
}

static int32_t voice_delete_port(uint32_t id)
{
    pthread_mutex_lock(&lock);
    require_init("sceVoiceDeletePort");
    if (id < MAX_PORTS) port_used[id] = 0;
    pthread_mutex_unlock(&lock);
    return 0;
}

/* Connect and Disconnect: both ports must exist. */
static int32_t connect_ports(const char *caller, uint32_t in, uint32_t out)
{
    pthread_mutex_lock(&lock);
    require_init(caller);
    require_port(caller, in);
    require_port(caller, out);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_connect(uint32_t in, uint32_t out) { return connect_ports("sceVoiceConnectIPortToOPort", in, out); }
static int32_t voice_disconnect(uint32_t in, uint32_t out) { return connect_ports("sceVoiceDisconnectIPortFromOPort", in, out); }

static int32_t voice_start(const void *param)
{
    if (!param) rt_fatal("sceVoiceStart", "no parameter: the error value is not established");
    pthread_mutex_lock(&lock);
    require_init("sceVoiceStart");
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_stop(void)
{
    pthread_mutex_lock(&lock);
    require_init("sceVoiceStop");
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_get_port_info(uint32_t id, port_info *info)
{
    static const char fn[] = "sceVoiceGetPortInfo";
    if (!info) rt_fatal(fn, "no info: the error value is not established");
    pthread_mutex_lock(&lock);
    require_init(fn);
    require_port(fn, id);
    *info = (port_info){.type = port_type[id], .frame_size = FRAME_SIZE};
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t voice_read(uint32_t id, void *data, uint32_t *size)
{
    static const char fn[] = "sceVoiceReadFromOPort";
    if (!data || !size) rt_fatal(fn, "no buffer or size: the error value is not established");
    pthread_mutex_lock(&lock);
    require_init(fn);
    require_port(fn, id);
    pthread_mutex_unlock(&lock);
    *size = 0;
    return 0;
}

static int32_t voice_write(uint32_t id, const void *data, uint32_t *size, int16_t frame_gaps)
{
    static const char fn[] = "sceVoiceWriteToIPort";
    (void)frame_gaps;
    if (!data || !size) rt_fatal(fn, "no data or size: the error value is not established");
    pthread_mutex_lock(&lock);
    require_init(fn);
    require_port(fn, id);
    pthread_mutex_unlock(&lock);
    return 0;
}

RT_SYSLIB("libSceVoice", sceVoiceInit, voice_init);
RT_SYSLIB("libSceVoice", sceVoiceEnd, voice_end);
RT_SYSLIB("libSceVoice", sceVoiceCreatePort, voice_create_port);
RT_SYSLIB("libSceVoice", sceVoiceDeletePort, voice_delete_port);
RT_SYSLIB("libSceVoice", sceVoiceConnectIPortToOPort, voice_connect);
RT_SYSLIB("libSceVoice", sceVoiceDisconnectIPortFromOPort, voice_disconnect);
RT_SYSLIB("libSceVoice", sceVoiceStart, voice_start);
RT_SYSLIB("libSceVoice", sceVoiceStop, voice_stop);
RT_SYSLIB("libSceVoice", sceVoiceGetPortInfo, voice_get_port_info);
RT_SYSLIB("libSceVoice", sceVoiceReadFromOPort, voice_read);
RT_SYSLIB("libSceVoice", sceVoiceWriteToIPort, voice_write);
