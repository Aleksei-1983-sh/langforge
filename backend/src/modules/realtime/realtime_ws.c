#include "modules/realtime/realtime_ws.h"

#include "modules/realtime/realtime_hub.h"

#include <ctype.h>
#include <errno.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static int send_bytes(int fd, const char *data, size_t len)
{
    size_t total = 0;

    while (total < len) {
        ssize_t rc = send(fd, data + total, len - total, MSG_NOSIGNAL);
        if (rc > 0) {
            total += (size_t) rc;
            continue;
        }
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

static int parse_job_id_query(const char *query)
{
    const char *cursor = query;

    if (!query || *query == '\0') {
        return 0;
    }

    while (*cursor != '\0') {
        const char *next = strchr(cursor, '&');
        size_t len = next ? (size_t) (next - cursor) : strlen(cursor);

        if (len > 7 && strncmp(cursor, "job_id=", 7) == 0) {
            long value = strtol(cursor + 7, NULL, 10);
            if (value > 0 && value <= 2147483647L) {
                return (int) value;
            }
            return 0;
        }

        if (!next) {
            break;
        }
        cursor = next + 1;
    }

    return 0;
}

static const char *find_header_value(const char *request, const char *header_name)
{
    static char value[256];
    const char *line = request;
    size_t header_len;

    if (!request || !header_name) {
        return NULL;
    }

    header_len = strlen(header_name);

    while (line && *line != '\0') {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;
        size_t len;

        if (!line_end) {
            break;
        }
        if (line_end == line) {
            break;
        }

        colon = memchr(line, ':', (size_t) (line_end - line));
        if (colon) {
            len = (size_t) (colon - line);
            if (len == header_len && strncasecmp(line, header_name, header_len) == 0) {
                const char *value_start = colon + 1;
                size_t value_len;

                while (value_start < line_end && isspace((unsigned char) *value_start)) {
                    value_start++;
                }
                value_len = (size_t) (line_end - value_start);
                if (value_len >= sizeof(value)) {
                    return NULL;
                }
                memcpy(value, value_start, value_len);
                value[value_len] = '\0';
                return value;
            }
        }

        line = line_end + 2;
    }

    return NULL;
}

static int ws_send_frame(int fd, uint8_t opcode, const char *payload, size_t payload_len)
{
    unsigned char header[4];
    size_t header_len = 0;

    if (payload_len > 4096) {
        return -1;
    }

    header[header_len++] = (unsigned char) (0x80 | (opcode & 0x0f));
    if (payload_len < 126) {
        header[header_len++] = (unsigned char) payload_len;
    } else {
        header[header_len++] = 126;
        header[header_len++] = (unsigned char) ((payload_len >> 8) & 0xff);
        header[header_len++] = (unsigned char) (payload_len & 0xff);
    }

    if (send_bytes(fd, (const char *) header, header_len) != 0) {
        return -1;
    }
    if (payload_len > 0 && send_bytes(fd, payload, payload_len) != 0) {
        return -1;
    }
    return 0;
}

int rt_ws_handshake(int fd, const char *request)
{
    const char *key;
    char combined[256];
    unsigned char digest[SHA_DIGEST_LENGTH];
    unsigned char accept_key[64];
    char response[512];
    int encoded_len;
    int response_len;

    if (fd < 0 || !request) {
        return -1;
    }

    key = find_header_value(request, "Sec-WebSocket-Key");
    if (!key) {
        return -1;
    }

    if (snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID) >= (int) sizeof(combined)) {
        return -1;
    }

    SHA1((const unsigned char *) combined, strlen(combined), digest);
    encoded_len = EVP_EncodeBlock(accept_key, digest, SHA_DIGEST_LENGTH);
    if (encoded_len <= 0) {
        return -1;
    }

    response_len = snprintf(response, sizeof(response),
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: %s\r\n"
                            "\r\n",
                            accept_key);
    if (response_len < 0 || response_len >= (int) sizeof(response)) {
        return -1;
    }

    return send_bytes(fd, response, (size_t) response_len);
}

int rt_ws_send_text(int fd, const char *msg)
{
    size_t len;

    if (fd < 0 || !msg) {
        return -1;
    }

    len = strlen(msg);
    return ws_send_frame(fd, 0x1, msg, len);
}

int rt_ws_read_frame(int fd, char *out, size_t out_size)
{
    unsigned char peek[8];
    unsigned char frame[4200];
    uint64_t payload_len;
    size_t header_len = 2;
    size_t frame_len;
    ssize_t rc;
    uint8_t opcode;
    int masked;
    int i;

    if (fd < 0 || !out || out_size == 0) {
        return -1;
    }

    rc = recv(fd, peek, 2, MSG_PEEK);
    if (rc == 0) {
        return -1;
    }
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (rc < 2) {
        return 0;
    }

    opcode = (uint8_t) (peek[0] & 0x0f);
    masked = (peek[1] & 0x80) != 0;
    payload_len = (uint64_t) (peek[1] & 0x7f);

    if (!masked) {
        return -1;
    }
    if ((peek[0] & 0x80) == 0) {
        return -1;
    }

    if (payload_len == 126) {
        rc = recv(fd, peek, 4, MSG_PEEK);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return 0;
            }
            return -1;
        }
        if (rc < 4) {
            return 0;
        }
        payload_len = (uint64_t) (((unsigned int) peek[2] << 8) | (unsigned int) peek[3]);
        header_len = 4;
    } else if (payload_len == 127) {
        return -1;
    }

    header_len += 4;
    if (payload_len + 1 > out_size || header_len + payload_len > sizeof(frame)) {
        return -1;
    }

    frame_len = header_len + (size_t) payload_len;
    rc = recv(fd, frame, frame_len, MSG_PEEK);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if ((size_t) rc < frame_len) {
        return 0;
    }

    rc = recv(fd, frame, frame_len, 0);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if ((size_t) rc != frame_len) {
        return -1;
    }

    {
        unsigned char *mask = frame + header_len - 4;
        unsigned char *payload = frame + header_len;

        for (i = 0; i < (int) payload_len; i++) {
            out[i] = (char) (payload[i] ^ mask[i % 4]);
        }
        out[payload_len] = '\0';
    }

    if (opcode == 0x8) {
        return -1;
    }
    if (opcode == 0x9) {
        return ws_send_frame(fd, 0xA, out, (size_t) payload_len);
    }
    if (opcode != 0x1) {
        return 1;
    }

    return (int) payload_len;
}

int rt_sse_send(int fd, const char *json)
{
    char buffer[4608];
    int len;

    if (fd < 0 || !json) {
        return -1;
    }

    len = snprintf(buffer, sizeof(buffer), "data: %s\n\n", json);
    if (len < 0 || len >= (int) sizeof(buffer)) {
        return -1;
    }

    return send_bytes(fd, buffer, (size_t) len);
}

void handle_realtime_ws(http_connection_t *conn, http_request_t *req)
{
    const char *raw_request;
    int fd;
    int job_id;

    if (!conn || !req) {
        return;
    }

    raw_request = http_connection_request_buffer(conn);
    fd = http_connection_fd(conn);
    if (!raw_request || fd < 0) {
        http_send_response(conn, 500, "text/plain", "Realtime transport unavailable", strlen("Realtime transport unavailable"));
        return;
    }

    if (rt_ws_handshake(fd, raw_request) != 0) {
        http_send_response(conn, 400, "text/plain", "WebSocket handshake failed", strlen("WebSocket handshake failed"));
        return;
    }

    http_connection_keep_open(conn);
    http_connection_mark_websocket(conn);
    rt_hub_mark_websocket(fd);

    job_id = parse_job_id_query(req->query);
    if (job_id > 0) {
        rt_hub_set_subscription(fd, job_id);
    }
}

void handle_realtime_sse(http_connection_t *conn, http_request_t *req)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";
    int fd;
    int job_id;

    if (!conn || !req) {
        return;
    }

    if (http_send_raw(conn, response, sizeof(response) - 1) != 0) {
        return;
    }

    fd = http_connection_fd(conn);
    if (fd < 0) {
        return;
    }

    http_connection_keep_open(conn);
    http_connection_mark_sse(conn);
    rt_hub_mark_sse(fd);

    job_id = parse_job_id_query(req->query);
    if (job_id > 0) {
        rt_hub_set_subscription(fd, job_id);
    }

    (void) send_bytes(fd, ": connected\n\n", strlen(": connected\n\n"));
}
