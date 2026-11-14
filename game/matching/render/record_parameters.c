// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the render record builder's selector and parameter
// helpers. The small table the replacement of render_reduce_record_parameter reads is clang's
// lookup table for its switch.
#include <stdint.h>

enum {
    PARAMETER_0 = 0,
    PARAMETER_1 = 1,
    PARAMETER_2 = 2,
    PARAMETER_3 = 3,
};

enum {
    SCALE_FOR_1 = 2,
    SCALE_FOR_2 = 4,
    SCALE_FOR_3 = 8,
};

// The scale (1, 2, 4 or 8) of a parameter 0..3; 0 for anything else.
uint32_t render_reduce_record_parameter(uint32_t parameter)
{
    switch (parameter) {
    case PARAMETER_0:
        return 1;
    case PARAMETER_1:
        return SCALE_FOR_1;
    case PARAMETER_2:
        return SCALE_FOR_2;
    case PARAMETER_3:
        return SCALE_FOR_3;
    default:
        return 0;
    }
}

enum {
    FORCE_FIRST_CODE = 0x20,
    FORCE_SECOND_CODE = 0x40,
    FORCED_FIRST_CODE = 2,
    FORCED_SECOND_CODE = 4,
    KIND_DIRECT = 4,
    KIND_MODE_SELECTED = 3,
    MODE_ALTERNATE = 4,
    BASE_DEFAULT = 7,
    BASE_DIRECT = 8,
    BASE_ALTERNATE = 9,
    EXTRA_PARAMETER = 3,
};

// Derive a normalization code: flag bits 5 and 6 force 2 and 4; otherwise a base chosen by kind
// and mode, plus 3 when the first argument is not one. The selector is unused.
uint32_t render_record_derive_parameter(uint32_t kind, uint32_t selector, uint32_t first,
                                        uint32_t flags, uint32_t mode)
{
    (void)selector;
    if (flags & FORCE_FIRST_CODE)
        return FORCED_FIRST_CODE;
    if (flags & FORCE_SECOND_CODE)
        return FORCED_SECOND_CODE;
    uint32_t code;
    if (kind == KIND_DIRECT) {
        code = BASE_DIRECT;
        if (first != 1)
            code += EXTRA_PARAMETER;
    } else if (kind == KIND_MODE_SELECTED && mode == MODE_ALTERNATE) {
        code = BASE_ALTERNATE;
        if (first != 1)
            code += EXTRA_PARAMETER;
    } else {
        code = BASE_DEFAULT;
        if (first != 1)
            code += EXTRA_PARAMETER;
    }
    return code;
}
