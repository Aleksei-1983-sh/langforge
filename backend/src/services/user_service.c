#include "services/user_service.h"

#include "dbug/dbug.h"
#include "internal_api/auth_api.h"
#include "internal_api/profile_api.h"
#include "libs/validate.h"

#include <string.h>

int user_service_login(const char *username, const char *password,
                       int *user_id, char **set_cookie_header)
{
    int rc = auth_api_login(username, password, user_id, set_cookie_header);

    if (rc == AUTH_API_ERR_INVALID_CREDENTIALS) return USER_SERVICE_ERR_INVALID_CREDENTIALS;
    if (rc == AUTH_API_ERR_SERVER) return USER_SERVICE_ERR_SERVER;
    if (rc != AUTH_API_OK) return USER_SERVICE_ERR_SERVER;

    return USER_SERVICE_OK;
}

int user_service_register(const char *username, const char *email,
                          const char *password, int *user_id)
{
    if (!username || !email || !password || !user_id) {
        return USER_SERVICE_ERR_SERVER;
    }

    if (strlen(password) < 6) {
        return USER_SERVICE_ERR_PASSWORD_TOO_SHORT;
    }

    if (!is_valid_email(email)) {
        return USER_SERVICE_ERR_INVALID_EMAIL;
    }

    int rc = auth_api_register(username, email, password, user_id);
    if (rc == AUTH_API_ERR_CONFLICT) return USER_SERVICE_ERR_CONFLICT;
    if (rc == AUTH_API_ERR_SERVER) return USER_SERVICE_ERR_SERVER;
    if (rc != AUTH_API_OK) return USER_SERVICE_ERR_SERVER;

    return USER_SERVICE_OK;
}

int user_service_get_profile(const char *cookie_header, user_profile_t *profile)
{
    if (!cookie_header || !profile) {
        return USER_SERVICE_ERR_SERVER;
    }

    int user_id = 0;
    int auth_rc = auth_api_validate_session(cookie_header, &user_id);
    if (auth_rc == AUTH_API_ERR_UNAUTHORIZED) return USER_SERVICE_ERR_UNAUTHORIZED;
    if (auth_rc != AUTH_API_OK) return USER_SERVICE_ERR_SERVER;

    memset(profile, 0, sizeof(*profile));

    profile_api_user_profile_t profile_data;
    int rc = profile_api_get_user_profile(user_id, &profile_data);
    if (rc != PROFILE_API_OK) {
        ERROR_PRINT("user_service_get_profile: profile_api_get_user_profile failed rc=%d", rc);
        return USER_SERVICE_ERR_SERVER;
    }

    memcpy(profile->username, profile_data.username, sizeof(profile->username));
    profile->words_learned = profile_data.words_learned;
    profile->active_lessons = profile_data.active_lessons;

    return USER_SERVICE_OK;
}
