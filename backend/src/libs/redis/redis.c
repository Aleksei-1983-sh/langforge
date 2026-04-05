#include "libs/redis/redis.h"

#include "dbug/dbug.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const char *redis_host = "127.0.0.1";
static const char *redis_port = "6379";
static const char *redis_session_prefix = "langforge:session:";

static int write_all(int fd, const char *buf, size_t len)
{
    size_t written = 0;

    while (written < len) {
        ssize_t rc = send(fd, buf + written, len - written, 0);
        if (rc <= 0) {
            return -1;
        }
        written += (size_t) rc;
    }

    return 0;
}

static int read_exact(int fd, char *buf, size_t len)
{
    size_t read_total = 0;

    while (read_total < len) {
        ssize_t rc = recv(fd, buf + read_total, len - read_total, 0);
        if (rc <= 0) {
            return -1;
        }
        read_total += (size_t) rc;
    }

    return 0;
}

static int read_line(int fd, char *buf, size_t buf_sz)
{
    size_t pos = 0;

    if (!buf || buf_sz < 3) {
        return -1;
    }

    while (pos + 1 < buf_sz) {
        ssize_t rc = recv(fd, &buf[pos], 1, 0);
        if (rc <= 0) {
            return -1;
        }

        if (pos > 0 && buf[pos - 1] == '\r' && buf[pos] == '\n') {
            buf[pos - 1] = '\0';
            return 0;
        }

        ++pos;
    }

    return -1;
}

static int open_redis_socket(void)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai_rc = getaddrinfo(redis_host, redis_port, &hints, &res);
    if (gai_rc != 0) {
        ERROR_PRINT("redis getaddrinfo failed for %s:%s: %s",
                    redis_host, redis_port, gai_strerror(gai_rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd < 0) {
        DEBUG_PRINT_MAIN("redis connect failed to %s:%s: %s",
                         redis_host, redis_port, strerror(errno));
    }

    return fd;
}

static char *build_session_key(const char *session_token)
{
    size_t key_len;
    char *key;

    if (!session_token || session_token[0] == '\0') return NULL;

    key_len = strlen(redis_session_prefix) + strlen(session_token) + 1;
    key = malloc(key_len);
    if (!key) return NULL;

    snprintf(key, key_len, "%s%s", redis_session_prefix, session_token);
    return key;
}

static int send_resp_command(int fd, int argc, const char **argv)
{
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    int rc = REDIS_ERR;

    if (!buf) return REDIS_ERR;

    len += (size_t) snprintf(buf + len, cap - len, "*%d\r\n", argc);

    for (int i = 0; i < argc; ++i) {
        size_t arg_len = strlen(argv[i]);
        size_t needed = len + 64 + arg_len;

        if (needed >= cap) {
            size_t new_cap = cap;
            while (needed >= new_cap) new_cap *= 2;

            char *tmp = realloc(buf, new_cap);
            if (!tmp) goto end;
            buf = tmp;
            cap = new_cap;
        }

        len += (size_t) snprintf(buf + len, cap - len, "$%zu\r\n", arg_len);
        memcpy(buf + len, argv[i], arg_len);
        len += arg_len;
        memcpy(buf + len, "\r\n", 2);
        len += 2;
    }

    if (write_all(fd, buf, len) != 0) goto end;
    rc = REDIS_OK;

end:
    free(buf);
    return rc;
}

void redis_init(void)
{
    const char *host = getenv("REDIS_HOST");
    const char *port = getenv("REDIS_PORT");
    const char *prefix = getenv("REDIS_SESSION_PREFIX");

    if (host && host[0] != '\0') redis_host = host;
    if (port && port[0] != '\0') redis_port = port;
    if (prefix && prefix[0] != '\0') redis_session_prefix = prefix;
}

int redis_connect(void)
{
    int fd = open_redis_socket();
    char line[128];
    const char *argv[] = { "PING" };
    int rc = REDIS_ERR;

    if (fd < 0) return REDIS_ERR;

    if (send_resp_command(fd, 1, argv) != REDIS_OK) goto end;
    if (read_line(fd, line, sizeof(line)) != 0) goto end;
    if (strcmp(line, "+PONG") != 0) goto end;

    rc = REDIS_OK;

end:
    close(fd);
    return rc;
}

int redis_set_session(const char *session_token, int user_id, int ttl_seconds)
{
    int fd = -1;
    char *key = NULL;
    char ttl_buf[32];
    char user_id_buf[32];
    char line[128];
    const char *argv[4];
    int rc = REDIS_ERR;

    if (!session_token || user_id <= 0 || ttl_seconds <= 0) {
        return REDIS_ERR;
    }

    key = build_session_key(session_token);
    if (!key) return REDIS_ERR;

    snprintf(ttl_buf, sizeof(ttl_buf), "%d", ttl_seconds);
    snprintf(user_id_buf, sizeof(user_id_buf), "%d", user_id);

    argv[0] = "SETEX";
    argv[1] = key;
    argv[2] = ttl_buf;
    argv[3] = user_id_buf;

    fd = open_redis_socket();
    if (fd < 0) goto end;

    if (send_resp_command(fd, 4, argv) != REDIS_OK) goto end;
    if (read_line(fd, line, sizeof(line)) != 0) goto end;
    if (strcmp(line, "+OK") != 0) goto end;

    rc = REDIS_OK;

end:
    if (fd >= 0) close(fd);
    free(key);
    return rc;
}

int redis_get_session(const char *session_token, int ttl_seconds, int *user_id)
{
    int fd = -1;
    char *key = NULL;
    char line[256];
    char bulk_body[64];
    const char *argv[2];
    int rc = REDIS_ERR;

    if (!session_token || !user_id) {
        return REDIS_ERR;
    }

    *user_id = 0;
    key = build_session_key(session_token);
    if (!key) return REDIS_ERR;

    argv[0] = "GET";
    argv[1] = key;

    fd = open_redis_socket();
    if (fd < 0) goto end;

    if (send_resp_command(fd, 2, argv) != REDIS_OK) goto end;
    if (read_line(fd, line, sizeof(line)) != 0) goto end;

    if (strcmp(line, "$-1") == 0) {
        rc = REDIS_MISS;
        goto end;
    }

    if (line[0] != '$') goto end;

    long bulk_len = strtol(line + 1, NULL, 10);
    if (bulk_len <= 0 || bulk_len >= (long) sizeof(bulk_body)) goto end;

    if (read_exact(fd, bulk_body, (size_t) bulk_len) != 0) goto end;
    bulk_body[bulk_len] = '\0';

    char crlf[2];
    if (read_exact(fd, crlf, sizeof(crlf)) != 0) goto end;

    *user_id = atoi(bulk_body);
    if (*user_id <= 0) goto end;

    if (ttl_seconds > 0) {
        if (redis_set_session(session_token, *user_id, ttl_seconds) != REDIS_OK) {
            DEBUG_PRINT_MAIN("redis session ttl refresh failed for active session");
        }
    }

    rc = REDIS_OK;

end:
    if (fd >= 0) close(fd);
    free(key);
    return rc;
}
