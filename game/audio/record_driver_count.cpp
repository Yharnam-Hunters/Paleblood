// SPDX-License-Identifier: GPL-2.0-or-later
// Each logged-in user supplies three FMOD recording drivers.
#include <array>
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {
RT_ORIGINAL(0x02fbff18, sce_user_service_get_login_user_id_list, int32_t(int32_t *users));

constexpr int32_t invalid_user_id = -1;
constexpr std::size_t login_user_slots = 4;
constexpr uint32_t drivers_per_user = 3;
constexpr uint32_t user_service_not_initialized = 0x80960002u;
constexpr uint32_t record_service_unavailable = 0x3a;
constexpr uint32_t record_driver_query_failed = 0x21;
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_driver_count(void *, uint32_t *count)
{
    if (*count != 0)
        return 0;

    std::array<int32_t, login_user_slots> users;
    const int32_t status = sce_user_service_get_login_user_id_list(users.data());
    if (static_cast<uint32_t>(status) == user_service_not_initialized)
        return record_service_unavailable;
    if (status < 0)
        return record_driver_query_failed;

    for (const int32_t user : users) {
        if (user != invalid_user_id)
            *count += drivers_per_user;
    }
    return 0;
}
