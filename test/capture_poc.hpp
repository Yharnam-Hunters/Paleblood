/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_CAPTURE_POC_HPP
#define TEST_CAPTURE_POC_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace capture_poc {

using Fixture = int32_t (*)(int32_t *, const int32_t *, int32_t);

enum class Signature { fixture_i32_ptr_ptr_i32_to_i32, unsupported };
enum class Role { output_inout, input, scalar_i32 };

struct Argument {
    const char *reg;
    Role role;
    const char *buffer;
    size_t size;
};

struct Descriptor {
    const char *function;
    const char *address;
    const char *run_id;
    Signature signature;
    const Argument *arguments;
    size_t argument_count;
};

enum class Error { none, missing_descriptor, unsupported_signature, invalid_descriptor };

struct Result {
    Error error = Error::none;
    std::string json;
    int32_t observed_return = 0;
    int32_t observed_output = 0;
};

template <typename T> struct SignatureOf {
    static constexpr Signature value = Signature::unsupported;
};
template <> struct SignatureOf<Fixture> {
    static constexpr Signature value = Signature::fixture_i32_ptr_ptr_i32_to_i32;
};

inline bool safe_component(const char *text, size_t max)
{
    if (!text || !*text || std::strlen(text) > max || !std::strcmp(text, ".") || !std::strcmp(text, ".."))
        return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
            return false;
    return true;
}

inline std::string hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string out = "00000000";
    for (int i = 7; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[value & 0xf];
        value >>= 4;
    }
    return out;
}

inline std::string bytes(const void *ptr, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    const auto *data = static_cast<const unsigned char *>(ptr);
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xf];
    }
    return out;
}

inline bool valid_fixture_descriptor(const Descriptor &d)
{
    if (!safe_component(d.function, 127) || !safe_component(d.run_id, 63) ||
        !d.address || std::strcmp(d.address, "0x00400000") ||
        d.signature != Signature::fixture_i32_ptr_ptr_i32_to_i32 ||
        !d.arguments || d.argument_count != 3)
        return false;
    const Argument expected[] = {
        {"rdi", Role::output_inout, "out", sizeof(int32_t)},
        {"rsi", Role::input, "input", sizeof(int32_t)},
        {"rdx", Role::scalar_i32, nullptr, 0},
    };
    for (size_t i = 0; i < 3; ++i) {
        const Argument &a = d.arguments[i];
        if (!a.reg || std::strcmp(a.reg, expected[i].reg) || a.role != expected[i].role ||
            a.size != expected[i].size ||
            ((a.buffer || expected[i].buffer) && (!a.buffer || !expected[i].buffer || std::strcmp(a.buffer, expected[i].buffer))))
            return false;
    }
    return true;
}

template <typename Fn, typename... Args>
Result capture_and_invoke(const Descriptor *descriptor, Fn target, Args... args)
{
    Result result;
    if constexpr (SignatureOf<Fn>::value != Signature::fixture_i32_ptr_ptr_i32_to_i32) {
        result.error = Error::unsupported_signature;
        return result;
    } else {
        if (!descriptor) {
            result.error = Error::missing_descriptor;
            return result;
        }
        if (!target || !valid_fixture_descriptor(*descriptor)) {
            result.error = Error::invalid_descriptor;
            return result;
        }
        if constexpr (!std::is_same_v<std::tuple<Args...>, std::tuple<int32_t *, const int32_t *, int32_t>>) {
            result.error = Error::unsupported_signature;
            return result;
        } else {
            int32_t *out = std::get<0>(std::tuple<Args...>(args...));
            const int32_t *input = std::get<1>(std::tuple<Args...>(args...));
            const int32_t factor = std::get<2>(std::tuple<Args...>(args...));
            const uintptr_t out_address = reinterpret_cast<uintptr_t>(out);
            const uintptr_t input_address = reinterpret_cast<uintptr_t>(input);
            constexpr uintptr_t extent = sizeof(int32_t);
            const bool overflow = out_address > std::numeric_limits<uintptr_t>::max() - extent ||
                                  input_address > std::numeric_limits<uintptr_t>::max() - extent;
            const bool overlap = !overflow && out_address < input_address + extent && input_address < out_address + extent;
            if (!out || !input || overflow || overlap) {
                result.error = Error::invalid_descriptor;
                return result;
            }
            const std::map<std::string, std::string> buffers = {
                {"input", bytes(input, sizeof *input)},
                {"out", bytes(out, sizeof *out)},
            };
            result.observed_return = target(out, input, factor);
            result.observed_output = *out;
            result.json = "{\"schema\":1,\"address\":\"" + std::string(descriptor->address) +
                          "\",\"id\":\"typed_fixture\",\"returns\":\"i32\",\"capture\":{\"function\":\"" +
                          descriptor->function + "\",\"run_id\":\"" + descriptor->run_id + "\"},";
            result.json += "\"args\":{\"rdi\":\"buf:out\",\"rsi\":\"buf:input\",\"rdx\":\"0x" +
                           hex32(static_cast<uint32_t>(factor)) + "\"},\"buffers\":{";
            bool first = true;
            for (const auto &buffer : buffers) {
                if (!first) result.json += ',';
                first = false;
                result.json += "\"" + buffer.first + "\":{\"size\":4,\"bytes\":\"" + buffer.second + "\"}";
            }
            result.json += "},\"memory\":[],\"stubs\":[],\"imports\":[]}\n";
            return result;
        }
    }
}

} // namespace capture_poc

#endif
