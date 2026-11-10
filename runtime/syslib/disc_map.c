/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: files and saves. The disc map (libSceDiscMap), which tells the file library
   whether a part of a file is already on the console's hard disk or still only on the disc.

   int32_t sceDiscMapIsRequestOnHDD(const char *path, int64_t offset, int64_t bytes, int32_t *on_hdd)
   (shadPS4's disc_map.cpp declares the shape). The bundled file library's one call
   (libSceFios2.elf+0x2d71c) passes a path, an offset, a size and a variable preset to 0: on 0 it
   takes a nonzero variable as on the hard disk, and on any other answer it treats the request as
   not mapped and remembers that the map is unavailable.

   The runtime serves the title from files on the host, with no disc, so there is no disc bitmap:
   it answers 0x81100004, "no bitmap info", as shadPS4 does for a title without a disc map (its
   disc_map_codes.h numbers the error), and writes nothing. A missing path or variable stops by
   name.

   The library's other import has no public name, only its NID, ioKMruft1ek, so it is registered
   with RT_SYSLIB_NID. The file library calls it as (const char *path, int64_t offset, int64_t
   bytes, int32_t *, int32_t *, int32_t *) (libSceFios2.elf+0x29c6f and +0x2a381: a path, an
   offset, a size and three variables) and takes any answer other than 0 as "no map", the same
   way. shadPS4 answers it with "no bitmap info" too, as does a family of native ports
   (facts only; PS4Recomp answers 0x80820001), and the maintainer's ruling of 2026-11-10 settles
   on 0x81100004. Nothing is written. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>

#define DISC_MAP_ERROR_NO_BITMAP_INFO ((int32_t)0x81100004u)

static int32_t is_request_on_hdd(const char *path, int64_t offset, int64_t bytes, int32_t *on_hdd)
{
    (void)offset, (void)bytes;
    if (!path || !on_hdd) rt_fatal("sceDiscMapIsRequestOnHDD", "no path, or no variable for the answer: the error value is not established");
    return DISC_MAP_ERROR_NO_BITMAP_INFO;
}

static int32_t request_map_info(const char *path, int64_t offset, int64_t bytes, int32_t *first, int32_t *second,
                                int32_t *third)
{
    (void)offset, (void)bytes;
    if (!path || !first || !second || !third)
        rt_fatal("ioKMruft1ek (libSceDiscMap)", "no path, or no variable for an answer: the error value is not established");
    return DISC_MAP_ERROR_NO_BITMAP_INFO;
}

RT_SYSLIB("libSceDiscMap", sceDiscMapIsRequestOnHDD, is_request_on_hdd);
RT_SYSLIB_NID("libSceDiscMap", "ioKMruft1ek", request_map_info);
