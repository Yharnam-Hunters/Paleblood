/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>

#include "capture_poc.hpp"

using namespace capture_poc;

static const Argument fixture_arguments[] = {
    {"rdi", Role::output_inout, "out", sizeof(int32_t)},
    {"rsi", Role::input, "input", sizeof(int32_t)},
    {"rdx", Role::scalar_i32, nullptr, 0},
};
static const Descriptor fixture_descriptor = {
    "capture_poc_fixture", "0x00400000", "synthetic_run_1",
    Signature::fixture_i32_ptr_ptr_i32_to_i32, fixture_arguments, 3,
};

static int calls;
static int32_t fixture(int32_t *out, const int32_t *input, int32_t factor)
{
    ++calls;
    *out = *input * factor;
    return *out;
}

static float unsupported(float value) { ++calls; return value; }

static int self_test()
{
    int failures = 0;
    int32_t out = 0x11223344, input = 7;
    calls = 0;
    Result first = capture_and_invoke(&fixture_descriptor, static_cast<Fixture>(fixture), &out,
                                      static_cast<const int32_t *>(&input), 6);
    if (first.error != Error::none || first.observed_return != 42 || first.observed_output != 42 || calls != 1)
        ++failures;
    out = 0x11223344;
    Result second = capture_and_invoke(&fixture_descriptor, static_cast<Fixture>(fixture), &out,
                                       static_cast<const int32_t *>(&input), 6);
    if (second.error != Error::none || first.json != second.json)
        ++failures;

    calls = 0;
    Result missing = capture_and_invoke(static_cast<const Descriptor *>(nullptr), static_cast<Fixture>(fixture), &out,
                                        static_cast<const int32_t *>(&input), 6);
    if (missing.error != Error::missing_descriptor || calls != 0)
        ++failures;
    Result unknown = capture_and_invoke(&fixture_descriptor, &unsupported, 1.0f);
    if (unknown.error != Error::unsupported_signature || calls != 0)
        ++failures;

    Argument incomplete[] = {fixture_arguments[0], {"rsi", Role::input, nullptr, 0}, fixture_arguments[2]};
    Descriptor missing_memory = fixture_descriptor;
    missing_memory.arguments = incomplete;
    Result no_extent = capture_and_invoke(&missing_memory, static_cast<Fixture>(fixture), &out,
                                          static_cast<const int32_t *>(&input), 6);
    if (no_extent.error != Error::invalid_descriptor || calls != 0)
        ++failures;

    Descriptor wrong_mapping = fixture_descriptor;
    Argument wrong_registers[] = {fixture_arguments[1], fixture_arguments[0], fixture_arguments[2]};
    wrong_mapping.arguments = wrong_registers;
    Result bad_mapping = capture_and_invoke(&wrong_mapping, static_cast<Fixture>(fixture), &out,
                                            static_cast<const int32_t *>(&input), 6);
    if (bad_mapping.error != Error::invalid_descriptor || calls != 0)
        ++failures;

    alignas(int32_t) unsigned char overlapping[sizeof(int32_t) + 2] {};
    Result aliased = capture_and_invoke(&fixture_descriptor, static_cast<Fixture>(fixture),
                                        reinterpret_cast<int32_t *>(overlapping),
                                        reinterpret_cast<const int32_t *>(overlapping + 2), 6);
    if (aliased.error != Error::invalid_descriptor || calls != 0)
        ++failures;

    if (failures)
        std::fprintf(stderr, "capture POC self-test: %d failure(s)\n", failures);
    else
        std::puts("capture POC self-test: typed call, deterministic case, and fail-closed checks passed");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--self-test"))
        return self_test();
    if (argc == 2 && !std::strcmp(argv[1], "--emit")) {
        int32_t out = 0x11223344, input = 7;
        Result result = capture_and_invoke(&fixture_descriptor, static_cast<Fixture>(fixture), &out,
                                           static_cast<const int32_t *>(&input), 6);
        if (result.error != Error::none || result.observed_return != 42 || result.observed_output != 42)
            return 1;
        std::fwrite(result.json.data(), 1, result.json.size(), stdout);
        return 0;
    }
    std::fprintf(stderr, "usage: %s --self-test|--emit\n", argv[0]);
    return 2;
}
