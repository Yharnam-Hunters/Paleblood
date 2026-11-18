// SPDX-License-Identifier: GPL-2.0-or-later
// Eight engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one deleting
// destructor: the class's destructor runs on the object, then operator delete (0x02fbe588) frees
// it. The copies of one family call the same destructor; the families differ only in which:
// - destroys_with_0x010f6190_then_deletes
// - destroys_with_0x02af2b00_then_deletes
// - destroys_with_0x02af4240_then_deletes
// - destroys_with_0x02af6d40_then_deletes
// - destroys_with_0x02af8220_then_deletes
// - destroys_with_0x02af9940_then_deletes
// - destroys_with_0x02afc240_then_deletes
// - destroys_with_0x02afee40_then_deletes
// Nothing is read from the object; the destructor's result is not used.
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

RT_ORIGINAL(0x010f6190, destroy_0x010f6190, void(void *));
RT_ORIGINAL(0x02af2b00, destroy_0x02af2b00, void(void *));
RT_ORIGINAL(0x02af4240, destroy_0x02af4240, void(void *));
RT_ORIGINAL(0x02af6d40, destroy_0x02af6d40, void(void *));
RT_ORIGINAL(0x02af8220, destroy_0x02af8220, void(void *));
RT_ORIGINAL(0x02af9940, destroy_0x02af9940, void(void *));
RT_ORIGINAL(0x02afc240, destroy_0x02afc240, void(void *));
RT_ORIGINAL(0x02afee40, destroy_0x02afee40, void(void *));

template <typename Destroy> void destroy_then_delete(const Destroy &destroy, void *object)
{
    destroy(object);
    operator_delete(object);
}

}  // namespace

extern "C" void bb_engine_destroys_with_0x010f6190_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x010f6190, object);
}

extern "C" void bb_engine_destroys_with_0x02af2b00_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02af2b00, object);
}

extern "C" void bb_engine_destroys_with_0x02af4240_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02af4240, object);
}

extern "C" void bb_engine_destroys_with_0x02af6d40_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02af6d40, object);
}

extern "C" void bb_engine_destroys_with_0x02af8220_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02af8220, object);
}

extern "C" void bb_engine_destroys_with_0x02af9940_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02af9940, object);
}

extern "C" void bb_engine_destroys_with_0x02afc240_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02afc240, object);
}

extern "C" void bb_engine_destroys_with_0x02afee40_then_deletes(void *object)
{
    destroy_then_delete(destroy_0x02afee40, object);
}
