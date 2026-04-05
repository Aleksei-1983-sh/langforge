#ifndef INTERNAL_API_AUTH_API_H
#define INTERNAL_API_AUTH_API_H

enum {
    AUTH_API_OK = 0,
    AUTH_API_ERR_INVALID_CREDENTIALS = -1,
    AUTH_API_ERR_SERVER = -2,
    AUTH_API_ERR_CONFLICT = -3,
    AUTH_API_ERR_UNAUTHORIZED = -4
};

int auth_api_validate_session(const char *cookie_header, int *user_id);
int auth_api_login(const char *username, const char *password,
                   int *user_id, char **set_cookie_header);
int auth_api_register(const char *username, const char *email,
                      const char *password, int *user_id);

#endif
