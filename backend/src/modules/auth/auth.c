#include "modules/auth/auth.h"

#include "db/db.h"
#include "dbug/dbug.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int get_session_ttl_from_env(void)
{
    const char *s = getenv("SESSION_MAX_AGE");
    if (!s || s[0] == '\0') return 2592000;

    char *endptr = NULL;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || v <= 0) return 2592000;

    return (int)v;
}

static char *cookie_get_value(const char *cookie_header, const char *name)
{
    if (!cookie_header || !name) return NULL;

    size_t name_len = strlen(name);
    const char *p = cookie_header;

    while (*p) {
        while (*p == ' ' || *p == ';') ++p;

        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            const char *value = p + name_len + 1;
            const char *end = value;
            while (*end && *end != ';') ++end;

            size_t value_len = (size_t)(end - value);
            char *out = malloc(value_len + 1);
            if (!out) return NULL;

            memcpy(out, value, value_len);
            out[value_len] = '\0';
            return out;
        }

        while (*p && *p != ';') ++p;
        if (*p == ';') ++p;
    }

    return NULL;
}

int auth_validate_session(const char *cookie_header, int *user_id)
{
    if (!cookie_header || !user_id) return AUTH_ERR_SERVER;

    char *session_token = cookie_get_value(cookie_header, "session");
    if (!session_token) {
        return AUTH_ERR_UNAUTHORIZED;
    }

    int ttl = get_session_ttl_from_env();
    int uid = db_userid_by_session(session_token, ttl);
    free(session_token);

    if (uid < 0) {
        ERROR_PRINT("auth_validate_session: db_userid_by_session failed");
        return AUTH_ERR_SERVER;
    }

    if (uid == 0) {
        return AUTH_ERR_UNAUTHORIZED;
    }

    *user_id = uid;
    return AUTH_OK;
}

int auth_login(const char *username, const char *password,
               int *user_id, char **set_cookie_header)
{
    if (!username || !password || !user_id || !set_cookie_header) {
        return AUTH_ERR_SERVER;
    }

    *set_cookie_header = NULL;
    *user_id = 0;

    int uid = db_login_user(username, password);
    if (uid == -1) {
        return AUTH_ERR_INVALID_CREDENTIALS;
    }
    if (uid < 0) {
        ERROR_PRINT("auth_login: db_login_user failed with code=%d", uid);
        return AUTH_ERR_SERVER;
    }

    int ttl = get_session_ttl_from_env();
    char *session_token = db_create_session(uid, ttl);
    if (!session_token) {
        ERROR_PRINT("auth_login: db_create_session failed for user_id=%d", uid);
        return AUTH_ERR_SERVER;
    }

    size_t hdr_size = strlen(session_token) + 128;
    char *cookie_header = malloc(hdr_size);
    if (!cookie_header) {
        free(session_token);
        return AUTH_ERR_SERVER;
    }

    int written = snprintf(cookie_header, hdr_size,
                           "Set-Cookie: session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d",
                           session_token, ttl);
    free(session_token);

    if (written < 0 || (size_t)written >= hdr_size) {
        free(cookie_header);
        ERROR_PRINT("auth_login: cookie header truncated");
        return AUTH_ERR_SERVER;
    }

    *user_id = uid;
    *set_cookie_header = cookie_header;
    return AUTH_OK;
}

int auth_register(const char *username, const char *email,
                  const char *password, int *user_id)
{
    if (!username || !email || !password || !user_id) {
        return AUTH_ERR_SERVER;
    }

    int new_user_id = db_register_user(username, email, password);
    if (new_user_id == -2) {
        return AUTH_ERR_CONFLICT;
    }
    if (new_user_id < 0) {
        ERROR_PRINT("auth_register: db_register_user failed with code=%d", new_user_id);
        return AUTH_ERR_SERVER;
    }

    *user_id = new_user_id;
    return AUTH_OK;
}
