// SPDX-License-Identifier: GPL-2.0-or-later
// Return the name and identifier of a logged-in user's FMOD recording input.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "record_login_call.h"

namespace {
struct RecordingDriverId {
    int32_t user;
    uint16_t reserved;
    uint16_t input_type;
    uint64_t tail;
};
static_assert(sizeof(RecordingDriverId) == 16);
static_assert(offsetof(RecordingDriverId, input_type) == 6);
static_assert(offsetof(RecordingDriverId, tail) == 8);

RT_ORIGINAL(0x02fbff88, sce_user_service_get_user_name, int32_t(int32_t, char *, std::size_t));
RT_ORIGINAL(0x011c4870, engine_string_append, void *(void *, const void *));
RT_GLOBAL(0x04c7d1f9, voice_chat_suffix, const char);
RT_GLOBAL(0x04c7d205, general_suffix, const char);
RT_GLOBAL(0x04c7d20e, voice_recognition_suffix, const char);

constexpr std::size_t login_user_slots = 4;
constexpr int32_t invalid_user = -1;
constexpr int32_t inputs_per_user = 3;
constexpr uint16_t voice_chat = 0, general = 1, voice_recognition = 5;
constexpr uint32_t login_uninitialized = 0x80960002u, service_unavailable = 0x3a;
constexpr uint32_t login_error = 0x21, missing_driver = 0x25, name_error = 0x39;
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_driver_info(void *, int32_t driver, char *name,
                                                      int32_t name_bytes, void *identifier)
{
    std::array<int32_t, login_user_slots> users;
    const int32_t login = audio::sce_user_service_get_login_user_id_list(users.data());
    if (static_cast<uint32_t>(login) == login_uninitialized)
        return service_unavailable;
    if (login < 0)
        return login_error;

    for (int32_t user : users) {
        if (user == invalid_user)
            continue;
        if (driver >= inputs_per_user) {
            driver -= inputs_per_user;
            continue;
        }

        const uint16_t type = driver == 2 ? voice_recognition : driver == 1 ? general : voice_chat;
        if (name != nullptr) {
            if (sce_user_service_get_user_name(user, name, static_cast<std::size_t>(name_bytes)) != 0)
                return name_error;
            const char *suffix = type == voice_recognition ? voice_recognition_suffix.address() :
                                 type == general ? general_suffix.address() : voice_chat_suffix.address();
            engine_string_append(name, suffix);
        }
        if (identifier != nullptr) {
            const RecordingDriverId result{user, 0, type, 0};
            std::memcpy(identifier, &result, sizeof(result));
        }
        return 0;
    }
    return missing_driver;
}
