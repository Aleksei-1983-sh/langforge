#ifndef AUTH_H
#define AUTH_H

enum {
    AUTH_OK = 0,
    AUTH_ERR_INVALID_CREDENTIALS = -1,
    AUTH_ERR_SERVER = -2,
    AUTH_ERR_CONFLICT = -3,
    AUTH_ERR_UNAUTHORIZED = -4
};

int auth_validate_session(const char *cookie_header, int *user_id);
int auth_login(const char *username, const char *password,
               int *user_id, char **set_cookie_header);
int auth_register(const char *username, const char *email,
                  const char *password, int *user_id);

#endif
