// SPDX-License-Identifier: GPL-2.0-or-later
// Convert a derived parameter into the tuple accepted by record normalization.
// Unsupported derived values leave the output untouched. The middle classification
// depends on whether the caller requests one parameter or several.
#include <iterator>

#include "record_parameters.h"

namespace {
constexpr uint32_t unsupported_parameter = 0x80d30000;
enum TupleClassification : uint32_t {
    unsupported_classification = 0,
    variable_classification_base = 2,
    paired_classification = 3,
    fixed_classification = 4,
};
struct ParameterTuple {
    uint32_t classification;
    uint32_t first;
    uint32_t second;
};
// Indexed by derived parameter; rows 0/3 are unsupported. The first member selects
// a classification, while first/second carry the flags and candidate selection.
constexpr ParameterTuple tuple_by_derived[] = {
    {0, 0, 0}, {4, 0, 0}, {4, 0, 1}, {0, 0, 0},
    {4, 2, 2}, {4, 4, 2}, {4, 64, 1}, {2, 0, 1},
    {3, 32, 4}, {2, 16, 1}, {2, 0, 1}, {3, 32, 4}, {2, 16, 1},
};
}  // namespace

extern "C" uint32_t bb_render_record_apply_mapping(uint32_t *output, uint32_t derived,
                                                   uint32_t mapping, uint32_t parameter)
{
    if (derived >= std::size(tuple_by_derived)) return unsupported_parameter;
    ParameterTuple tuple = tuple_by_derived[derived];
    if (tuple.classification == unsupported_classification) return unsupported_parameter;
    if (tuple.classification == variable_classification_base && parameter > 1)
        tuple.classification = fixed_classification;
    return render::apply_record_parameter_tuple(output, tuple.classification, tuple.first,
                                               mapping, parameter, tuple.second);
}
