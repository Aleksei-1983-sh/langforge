#include "internal_api/profile_api.h"

#include "db/db.h"

#include <string.h>

int profile_api_get_user_profile(int user_id, profile_api_user_profile_t *profile)
{
    if (user_id <= 0 || !profile) {
        return PROFILE_API_ERR_INVALID_ARGUMENT;
    }

    memset(profile, 0, sizeof(*profile));

    int rc = db_get_user_profile(user_id,
                                 profile->username,
                                 sizeof(profile->username),
                                 &profile->words_learned,
                                 &profile->active_lessons);
    if (rc == -2) {
        return PROFILE_API_ERR_NOT_FOUND;
    }
    if (rc != 0) {
        return PROFILE_API_ERR_SERVER;
    }

    return PROFILE_API_OK;
}
