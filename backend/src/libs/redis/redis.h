#ifndef REDIS_H
#define REDIS_H

enum {
    REDIS_OK = 0,
    REDIS_ERR = -1,
    REDIS_MISS = -2
};

void redis_init(void);
int redis_connect(void);
int redis_set_session(const char *session_token, int user_id, int ttl_seconds);
int redis_get_session(const char *session_token, int ttl_seconds, int *user_id);

#endif
