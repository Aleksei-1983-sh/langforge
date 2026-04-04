#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include <stddef.h>

typedef struct {
    char username[256];
    int words_learned;
    int active_lessons;
} user_profile_t;

enum {
    USER_SERVICE_OK = 0,
    USER_SERVICE_ERR_INVALID_CREDENTIALS = -1,
    USER_SERVICE_ERR_SERVER = -2,
    USER_SERVICE_ERR_CONFLICT = -3,
    USER_SERVICE_ERR_UNAUTHORIZED = -4,
    USER_SERVICE_ERR_INVALID_EMAIL = -5,
    USER_SERVICE_ERR_PASSWORD_TOO_SHORT = -6
};

int user_service_login(const char *username, const char *password,
                       int *user_id, char **set_cookie_header);
int user_service_register(const char *username, const char *email,
                          const char *password, int *user_id);
int user_service_get_profile(const char *cookie_header, user_profile_t *profile);

#endif
