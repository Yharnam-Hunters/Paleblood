// SPDX-License-Identifier: GPL-2.0-or-later
// Resolve a recording driver index into its logged-in user and input type.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "record_login_call.h"

namespace {
constexpr std::size_t login_user_slots = 4;
constexpr int32_t invalid_user = -1, inputs_per_user = 3;
constexpr uint32_t voice_chat = 0, general = 1, voice_recognition = 5;
constexpr uint32_t login_uninitialized = 0x80960002u, service_unavailable = 0x3a;
constexpr uint32_t login_error = 0x21, missing_driver = 0x25;
}  // namespace

extern "C" uint32_t bb_audio_fmod_select_record_input(void *, int32_t driver,
                                                      void *selected_user, void *input_type)
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
        std::memcpy(selected_user, &user, sizeof(user));
        // A negative index selects this user without replacing the caller's type.
        if (driver >= 0) {
            const uint32_t type = driver == 2 ? voice_recognition : driver == 1 ? general : voice_chat;
            std::memcpy(input_type, &type, sizeof(type));
        }
        return 0;
    }
    return missing_driver;
}
