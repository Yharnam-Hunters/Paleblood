/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN identity comparisons
   (libSceNpCommon), which need no network.

   An online id is 16 characters, a terminator and 3 bytes of padding (20 bytes); an NP id is an
   online id followed by 8 option bytes and 8 reserved bytes (36 bytes). sceNpCmpOnlineId(const
   online id *, const online id *) compares the 16 characters as strncmp does, so the comparison
   ends at a terminator; sceNpCmpNpId(const NP id *, const NP id *) compares the characters the same
   way and then every option and reserved byte. Both answer 0 for the same identity, 0x80550609
   (not a match) otherwise, and 0x80550003 (invalid argument) for a missing one. Facts from
   shadPS4's np_common.cpp, np_types.h and np_error.h, and PS4Recomp's libSceNp (ps4_np.c), which
   agree; the code is the runtime's own. The executable compares NP ids at 0x010a28d1 and online
   ids at 0x010b4676, each testing the answer against 0. */
#include "runtime/syslib.h"

#include <stdint.h>
#include <string.h>

#define NP_ERROR_INVALID_ARGUMENT ((int32_t)0x80550003u)
#define NP_UTIL_ERROR_NOT_MATCH ((int32_t)0x80550609u)
enum { NP_ONLINE_ID_CHARACTERS = 16, NP_ID_OPTION_BYTES = 8, NP_ID_RESERVED_BYTES = 8 };

typedef struct {
    char data[NP_ONLINE_ID_CHARACTERS];
    int8_t terminator;
    int8_t padding[3];
} np_online_id;
_Static_assert(sizeof(np_online_id) == 20, "an online id is 20 bytes");

typedef struct {
    np_online_id handle;
    uint8_t options[NP_ID_OPTION_BYTES];
    uint8_t reserved[NP_ID_RESERVED_BYTES];
} np_id;
_Static_assert(sizeof(np_id) == 36, "an NP id is 36 bytes");

static int32_t compare_online_id(const np_online_id *a, const np_online_id *b)
{
    if (!a || !b) return NP_ERROR_INVALID_ARGUMENT;
    return strncmp(a->data, b->data, NP_ONLINE_ID_CHARACTERS) ? NP_UTIL_ERROR_NOT_MATCH : 0;
}
RT_SYSLIB("libSceNpCommon", sceNpCmpOnlineId, compare_online_id);

static int32_t compare_np_id(const np_id *a, const np_id *b)
{
    if (!a || !b) return NP_ERROR_INVALID_ARGUMENT;
    if (strncmp(a->handle.data, b->handle.data, NP_ONLINE_ID_CHARACTERS) ||
        memcmp(a->options, b->options, NP_ID_OPTION_BYTES) || memcmp(a->reserved, b->reserved, NP_ID_RESERVED_BYTES))
        return NP_UTIL_ERROR_NOT_MATCH;
    return 0;
}
RT_SYSLIB("libSceNpCommon", sceNpCmpNpId, compare_np_id);
