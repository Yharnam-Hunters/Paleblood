/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN matchmaking (libSceNpMatching2):
   finding and joining other players' worlds. The runtime is offline, so matchmaking starts but
   never reaches a server.

   Facts, from shadPS4's libSceNpMatching2 (np_types2.h, np_error.h) and the other ports, which
   agree; implemented here independently:
   - sceNpMatching2Initialize(const parameter *): the parameter block is 0x30 bytes, its own size
     at offset 0x20; a size of 0x30 or the older 0x28 is accepted, anything else (or no block)
     answers 0x80550C0A (invalid argument), and a second initialize 0x80550C02 (already
     initialized).
   - sceNpMatching2Terminate(void): ends it; called before initialize it answers 0x80550C03 (not
     initialized).
   The executable's call (0x010ba2d0) builds the block on its stack and treats a negative answer
   as failure. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define NP_MATCHING2_ERROR_ALREADY_INITIALIZED ((int32_t)0x80550C02u)
#define NP_MATCHING2_ERROR_NOT_INITIALIZED ((int32_t)0x80550C03u)
#define NP_MATCHING2_ERROR_INVALID_ARGUMENT ((int32_t)0x80550C0Au)
enum { PARAMETER_SIZE_OFFSET = 0x20, PARAMETER_BYTES = 0x30, OLDER_PARAMETER_BYTES = 0x28 };

static atomic_int initialized;

static int32_t initialize(const uint8_t *parameter)
{
    if (!parameter) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    uint64_t size;
    memcpy(&size, parameter + PARAMETER_SIZE_OFFSET, sizeof size);
    if (size != PARAMETER_BYTES && size != OLDER_PARAMETER_BYTES) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    int expected = 0;
    return atomic_compare_exchange_strong(&initialized, &expected, 1) ? 0 : NP_MATCHING2_ERROR_ALREADY_INITIALIZED;
}

static int32_t terminate(void)
{
    int expected = 1;
    return atomic_compare_exchange_strong(&initialized, &expected, 0) ? 0 : NP_MATCHING2_ERROR_NOT_INITIALIZED;
}

RT_SYSLIB("libSceNpMatching2", sceNpMatching2Initialize, initialize);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2Terminate, terminate);
