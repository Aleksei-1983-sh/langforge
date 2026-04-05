#ifndef INTERNAL_API_PROFILE_API_H
#define INTERNAL_API_PROFILE_API_H

#include <stddef.h>

typedef struct {
    char username[256];
    int words_learned;
    int active_lessons;
} profile_api_user_profile_t;

enum {
    PROFILE_API_OK = 0,
    PROFILE_API_ERR_SERVER = -1,
    PROFILE_API_ERR_NOT_FOUND = -2,
    PROFILE_API_ERR_INVALID_ARGUMENT = -3
};

int profile_api_get_user_profile(int user_id, profile_api_user_profile_t *profile);

#endif
