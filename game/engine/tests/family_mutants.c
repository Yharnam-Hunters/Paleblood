/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Engine-specific deliberately wrong replacements for the engine function families
   (game/engine/family_cases.py MUTANTS), built into libbbfamily_mutants beside the generic ones
   (test/family_mutants.c), whose bbgame_init records the image these reach through rt_guest. */
#include <stdint.h>

#include "runtime/guest.h"

/* The guard of the static wide string the initialize_static_wide_string members build. */
#define STATIC_WIDE_STRING_GUARD 0x05acebc8u

/* Publishes the guard and does nothing else: wrong for initialize_static_wide_string, which
   builds the string and registers its destructor first on a clear guard and leaves a set guard
   alone. */
void bb_mutant_static_wide_string_guard_only(void)
{
    *(volatile uint64_t *)rt_guest(STATIC_WIDE_STRING_GUARD) = 1;
}

/* The eight destroys_with_0x..._then_deletes families (game/engine/destroy_then_delete_families.cpp):
   the family's destructor, then operator delete (0x02fbe588), with the object. */
#define DELETING_DTOR_OPERATOR_DELETE 0x02fbe588u
typedef void mutant_destroyed_fn(void *object);

static void mutant_call_on_object(uint32_t function, void *object)
{
    ((mutant_destroyed_fn *)rt_guest(function))(object);
}

/* Deletes without destroying: wrong for every destroys_with_0x..._then_deletes family. */
void bb_mutant_deleting_dtor_skips_destructor(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
}

/* Per family: destroys without deleting, and deletes before destroying. */
#define DELETING_DTOR_0x010f6190 0x010f6190u
void bb_mutant_deleting_dtor_0x010f6190_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x010f6190, object);
}
void bb_mutant_deleting_dtor_0x010f6190_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x010f6190, object);
}

#define DELETING_DTOR_0x02af2b00 0x02af2b00u
void bb_mutant_deleting_dtor_0x02af2b00_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af2b00, object);
}
void bb_mutant_deleting_dtor_0x02af2b00_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af2b00, object);
}

#define DELETING_DTOR_0x02af4240 0x02af4240u
void bb_mutant_deleting_dtor_0x02af4240_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af4240, object);
}
void bb_mutant_deleting_dtor_0x02af4240_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af4240, object);
}

#define DELETING_DTOR_0x02af6d40 0x02af6d40u
void bb_mutant_deleting_dtor_0x02af6d40_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af6d40, object);
}
void bb_mutant_deleting_dtor_0x02af6d40_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af6d40, object);
}

#define DELETING_DTOR_0x02af8220 0x02af8220u
void bb_mutant_deleting_dtor_0x02af8220_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af8220, object);
}
void bb_mutant_deleting_dtor_0x02af8220_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af8220, object);
}

#define DELETING_DTOR_0x02af9940 0x02af9940u
void bb_mutant_deleting_dtor_0x02af9940_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02af9940, object);
}
void bb_mutant_deleting_dtor_0x02af9940_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02af9940, object);
}

#define DELETING_DTOR_0x02afc240 0x02afc240u
void bb_mutant_deleting_dtor_0x02afc240_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02afc240, object);
}
void bb_mutant_deleting_dtor_0x02afc240_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02afc240, object);
}

#define DELETING_DTOR_0x02afee40 0x02afee40u
void bb_mutant_deleting_dtor_0x02afee40_kept(void *object)
{
    mutant_call_on_object(DELETING_DTOR_0x02afee40, object);
}
void bb_mutant_deleting_dtor_0x02afee40_deletes_first(void *object)
{
    mutant_call_on_object(DELETING_DTOR_OPERATOR_DELETE, object);
    mutant_call_on_object(DELETING_DTOR_0x02afee40, object);
}
