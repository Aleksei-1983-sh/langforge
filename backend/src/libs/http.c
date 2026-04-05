/* http.c */
#define _GNU_SOURCE

#include "http.h"

#include "modules/realtime/realtime_hub.h"
#include "modules/realtime/realtime_ws.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_HANDLER 16
#define MAX_CLIENTS 10000
#define MAX_BUFFER 8192
#define MAX_EPOLL_EVENTS 128

enum {
    HTTP_CONN_MODE_HTTP = 0,
    HTTP_CONN_MODE_WS = 1,
    HTTP_CONN_MODE_SSE = 2
};

struct http_connection_s {
    int fd;
    char buffer[MAX_BUFFER];
    size_t buf_len;
    int should_close;
    int keep_open;
    int mode;
    http_request_t req;
};

typedef struct {
    char method[8];
    char path[256];
    http_handler_fn handler;
} handler_entry_t;

static handler_entry_t handlers[MAX_HANDLER];
static int handler_count = 0;

static int server_fd = -1;
static int epoll_fd = -1;
static http_connection_t *clients[MAX_CLIENTS];

static int send_all_nonblocking(int fd, const char *data, size_t len)
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
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }
        return -1;
    }

    return 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static int register_client(http_connection_t *conn)
{
    int i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i]) {
            clients[i] = conn;
            return 0;
        }
    }

    return -1;
}

static void unregister_client(http_connection_t *conn)
{
    int i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == conn) {
            clients[i] = NULL;
            return;
        }
    }
}

static void close_connection(http_connection_t *conn)
{
    if (!conn) {
        return;
    }

    if (epoll_fd >= 0 && conn->fd >= 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    }
    if (conn->fd >= 0) {
        rt_hub_remove_client(conn->fd);
        close(conn->fd);
    }
    if (conn->req.body) {
        free(conn->req.body);
        conn->req.body = NULL;
    }
    unregister_client(conn);
    free(conn);
}

static http_connection_t *alloc_connection(int fd)
{
    http_connection_t *conn = malloc(sizeof(*conn));

    if (!conn) {
        return NULL;
    }

    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;
    conn->mode = HTTP_CONN_MODE_HTTP;
    return conn;
}

static size_t header_length(const char *raw, size_t raw_len)
{
    const char *end_headers;

    end_headers = memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!end_headers) {
        return 0;
    }

    return (size_t) (end_headers - raw) + 4;
}

static size_t content_length_from_raw(const char *raw, size_t raw_len)
{
    size_t headers_len;
    size_t i;

    headers_len = header_length(raw, raw_len);
    if (headers_len == 0) {
        return 0;
    }

    for (i = 0; i + 16 < headers_len; i++) {
        if (strncasecmp(raw + i, "Content-Length:", 15) == 0) {
            const char *value = raw + i + 15;
            while ((size_t) (value - raw) < headers_len &&
                   (*value == ' ' || *value == '\t')) {
                value++;
            }
            return (size_t) strtoul(value, NULL, 10);
        }
    }

    return 0;
}

static int is_request_complete(const char *raw, size_t raw_len)
{
    size_t headers_len;
    size_t body_len;

    headers_len = header_length(raw, raw_len);
    if (headers_len == 0) {
        return 0;
    }

    body_len = content_length_from_raw(raw, raw_len);
    if (raw_len < headers_len + body_len) {
        return 0;
    }

    return 1;
}

int http_parse_request(const char *raw, size_t raw_len, http_request_t *req)
{
    char *tmp;
    char *line;
    char *saveptr;
    char *request_line;
    char *headers_blob;
    char *body_start;
    char *query;
    char *method;
    char *path;
    size_t body_len = 0;
    int hcount = 0;

    if (!raw || raw_len == 0 || !req) {
        return -1;
    }

    tmp = malloc(raw_len + 1);
    if (!tmp) {
        return -1;
    }

    memcpy(tmp, raw, raw_len);
    tmp[raw_len] = '\0';
    memset(req, 0, sizeof(*req));

    DBG_REQUEST("\n\n%.*s\n\n", (int) raw_len, tmp);

    body_start = strstr(tmp, "\r\n\r\n");
    if (!body_start) {
        free(tmp);
        return -1;
    }
    *body_start = '\0';
    body_start += 4;

    request_line = strtok_r(tmp, "\r\n", &saveptr);
    if (!request_line) {
        free(tmp);
        return -1;
    }

    method = strtok(request_line, " ");
    path = strtok(NULL, " ");
    if (!method || !path) {
        free(tmp);
        return -1;
    }

    strncpy(req->method, method, sizeof(req->method) - 1);
    strncpy(req->path, path, sizeof(req->path) - 1);

    query = strchr(req->path, '?');
    if (query) {
        *query = '\0';
        query++;
        strncpy(req->query, query, sizeof(req->query) - 1);
    }

    headers_blob = saveptr;
    while (headers_blob && *headers_blob != '\0') {
        line = strtok_r(NULL, "\r\n", &saveptr);
        if (!line) {
            break;
        }
        if (*line == '\0') {
            continue;
        }
        if (hcount >= MAX_HEADERS) {
            break;
        }

        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }

        *colon = '\0';
        colon++;
        while (*colon == ' ' || *colon == '\t') {
            colon++;
        }

        strncpy(req->headers[hcount][0], line, MAX_HEADER_VALUE - 1);
        strncpy(req->headers[hcount][1], colon, MAX_HEADER_VALUE - 1);
        hcount++;
    }
    req->header_count = hcount;

    {
        const char *content_length = http_get_header(req, "Content-Length");
        if (content_length) {
            body_len = (size_t) strtoul(content_length, NULL, 10);
        }
    }

    if (body_len > 0) {
        req->body = malloc(body_len + 1);
        if (!req->body) {
            free(tmp);
            return -1;
        }
        memcpy(req->body, body_start, body_len);
        req->body[body_len] = '\0';
        req->body_len = body_len;
    }

    free(tmp);
    return 0;
}

const char *http_get_header(http_request_t *req, const char *key)
{
    int i;

    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i][0], key) == 0) {
            return req->headers[i][1];
        }
    }
    return NULL;
}

static http_handler_fn find_handler(const char *method, const char *path)
{
    int h;

    for (h = 0; h < handler_count; h++) {
        const char *registered_path = handlers[h].path;
        size_t registered_len;

        if (strcasecmp(handlers[h].method, method) != 0) {
            continue;
        }

        if (strcmp(registered_path, path) == 0) {
            return handlers[h].handler;
        }

        registered_len = strlen(registered_path);
        if (registered_len >= 2 &&
            registered_path[registered_len - 1] == '*' &&
            registered_path[registered_len - 2] == '/') {
            if (strncmp(registered_path, path, registered_len - 1) == 0) {
                return handlers[h].handler;
            }
        }

        if (registered_len > 0 && registered_path[registered_len - 1] == '/') {
            if (strncmp(registered_path, path, registered_len) == 0) {
                return handlers[h].handler;
            }
        }
    }

    return NULL;
}

static int add_epoll_interest(http_connection_t *conn)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    event.data.ptr = conn;

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &event);
}

int http_register_handler(const char *method, const char *path, http_handler_fn handler)
{
    if (handler_count >= MAX_HANDLER) {
        return -1;
    }
    strncpy(handlers[handler_count].method, method, 7);
    handlers[handler_count].method[7] = '\0';
    strncpy(handlers[handler_count].path, path, 255);
    handlers[handler_count].path[255] = '\0';
    handlers[handler_count].handler = handler;
    handler_count++;
    DBG("Registered handler %s %s", method, path);
    return 0;
}

int http_server_start(int port)
{
    struct sockaddr_in addr;
    struct epoll_event event;
    int opt = 1;

    DBG("Запуск сервера на порту %d", port);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (set_nonblocking(server_fd) != 0) {
        perror("fcntl");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t) port);

    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    memset(clients, 0, sizeof(clients));
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = NULL;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0) {
        perror("epoll_ctl");
        close(epoll_fd);
        close(server_fd);
        epoll_fd = -1;
        server_fd = -1;
        return -1;
    }

    DBG("Сервер запущен");
    return 0;
}

static void accept_new_connections(void)
{
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        http_connection_t *conn;

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            return;
        }

        if (set_nonblocking(client_fd) != 0) {
            close(client_fd);
            continue;
        }

        conn = alloc_connection(client_fd);
        if (!conn) {
            close(client_fd);
            continue;
        }

        if (register_client(conn) != 0 || add_epoll_interest(conn) != 0) {
            close_connection(conn);
            continue;
        }
    }
}

static void process_http_connection(http_connection_t *conn)
{
    for (;;) {
        ssize_t n = recv(conn->fd,
                         conn->buffer + conn->buf_len,
                         sizeof(conn->buffer) - conn->buf_len - 1,
                         0);

        if (n > 0) {
            conn->buf_len += (size_t) n;
            conn->buffer[conn->buf_len] = '\0';
            if (is_request_complete(conn->buffer, conn->buf_len)) {
                http_handler_fn handler;

                if (conn->req.body) {
                    free(conn->req.body);
                    conn->req.body = NULL;
                }

                if (http_parse_request(conn->buffer, conn->buf_len, &conn->req) != 0) {
                    http_send_response(conn, 400, "text/plain", "Bad Request", strlen("Bad Request"));
                    conn->should_close = 1;
                    return;
                }

                handler = find_handler(conn->req.method, conn->req.path);
                if (!handler) {
                    http_send_response(conn, 404, "text/plain", "Not Found", strlen("Not Found"));
                    conn->should_close = 1;
                    return;
                }

                handler(conn, &conn->req);
                if (!conn->keep_open) {
                    conn->should_close = 1;
                }
                return;
            }
            if (conn->buf_len >= sizeof(conn->buffer) - 1) {
                http_send_response(conn, 413, "text/plain", "Payload Too Large", strlen("Payload Too Large"));
                conn->should_close = 1;
                return;
            }
            continue;
        }

        if (n == 0) {
            conn->should_close = 1;
            return;
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        conn->should_close = 1;
        return;
    }
}

static void process_ws_connection(http_connection_t *conn)
{
    char frame[4096];
    int rc;

    for (;;) {
        rc = rt_ws_read_frame(conn->fd, frame, sizeof(frame));
        if (rc > 0) {
            continue;
        }
        if (rc == 0) {
            return;
        }
        conn->should_close = 1;
        return;
    }
}

static void process_sse_connection(http_connection_t *conn)
{
    char discard[256];

    for (;;) {
        ssize_t rc = recv(conn->fd, discard, sizeof(discard), 0);

        if (rc > 0) {
            continue;
        }
        if (rc == 0) {
            conn->should_close = 1;
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        conn->should_close = 1;
        return;
    }
}

void http_server_poll(void)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int i;
    int ready = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, 100);

    if (ready < 0) {
        if (errno != EINTR) {
            perror("epoll_wait");
        }
        return;
    }

    for (i = 0; i < ready; i++) {
        http_connection_t *conn = (http_connection_t *) events[i].data.ptr;

        if (!conn) {
            accept_new_connections();
            continue;
        }

        if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
            conn->should_close = 1;
        } else if (events[i].events & EPOLLIN) {
            if (conn->mode == HTTP_CONN_MODE_WS) {
                process_ws_connection(conn);
            } else if (conn->mode == HTTP_CONN_MODE_SSE) {
                process_sse_connection(conn);
            } else {
                process_http_connection(conn);
            }
        }

        if (conn->should_close) {
            close_connection(conn);
        }
    }
}

void http_server_stop(void)
{
    int i;

    DBG("Останов сервера");

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            close_connection(clients[i]);
        }
    }

    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    if (epoll_fd >= 0) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    handler_count = 0;
}

static const char *reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}

int http_send_raw(http_connection_t *conn, const char *data, size_t len)
{
    if (!conn || !data) {
        return -1;
    }
    if (send_all_nonblocking(conn->fd, data, len) != 0) {
        conn->should_close = 1;
        return -1;
    }
    return 0;
}

int http_connection_fd(http_connection_t *conn)
{
    if (!conn) {
        return -1;
    }
    return conn->fd;
}

const char *http_connection_request_buffer(http_connection_t *conn)
{
    if (!conn) {
        return NULL;
    }
    return conn->buffer;
}

void http_connection_keep_open(http_connection_t *conn)
{
    if (!conn) {
        return;
    }
    conn->keep_open = 1;
}

void http_connection_mark_websocket(http_connection_t *conn)
{
    if (!conn) {
        return;
    }
    conn->mode = HTTP_CONN_MODE_WS;
}

void http_connection_mark_sse(http_connection_t *conn)
{
    if (!conn) {
        return;
    }
    conn->mode = HTTP_CONN_MODE_SSE;
}

int http_send_response(http_connection_t *conn, int status_code,
                       const char *content_type, const char *body,
                       size_t body_len)
{
    char header[512];
    const char *reason = reason_phrase(status_code);
    int header_len;

    DBG("http_send_response: status=%d, body_len=%zu", status_code, body_len);

    if (!conn) {
        return -1;
    }

    header_len = snprintf(header, sizeof(header),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          status_code, reason, content_type, body_len);
    if (header_len < 0 || (size_t) header_len >= sizeof(header)) {
        return -1;
    }
    if (http_send_raw(conn, header, (size_t) header_len) != 0) {
        return -1;
    }
    if (body_len > 0 && http_send_raw(conn, body, body_len) != 0) {
        return -1;
    }
    return 0;
}

int my_send_response_with_headers(http_connection_t *conn,
                                  int status,
                                  const char *mime,
                                  const char *body,
                                  size_t len,
                                  const char **headers,
                                  size_t headers_count)
{
    char *hdr = NULL;
    size_t alloc = 0;
    int hdr_len = 0;
    const char *reason;
    size_t i;

    if (!conn) {
        return -1;
    }
    if (!mime) {
        mime = "application/octet-stream";
    }
    if (!body) {
        body = "";
    }
    if (headers_count && !headers) {
        headers_count = 0;
    }

    reason = reason_phrase(status);
    alloc = 256;
    for (i = 0; i < headers_count; ++i) {
        if (headers[i]) {
            alloc += strlen(headers[i]) + 2;
        }
    }
    alloc += 64;

    hdr = (char *) malloc(alloc);
    if (!hdr) {
        return -1;
    }

    hdr_len = snprintf(hdr, alloc,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n",
                       status, reason, mime, len);
    if (hdr_len < 0 || (size_t) hdr_len >= alloc) {
        free(hdr);
        return -1;
    }

    for (i = 0; i < headers_count; ++i) {
        const char *h = headers[i];
        size_t need;
        int n;

        if (!h) {
            continue;
        }
        need = strlen(h) + 2;
        if ((size_t) hdr_len + need + 4 >= alloc) {
            size_t new_alloc = alloc * 2 + need + 128;
            char *p = (char *) realloc(hdr, new_alloc);
            if (!p) {
                free(hdr);
                return -1;
            }
            hdr = p;
            alloc = new_alloc;
        }
        n = snprintf(hdr + hdr_len, alloc - (size_t) hdr_len, "%s\r\n", h);
        if (n < 0) {
            free(hdr);
            return -1;
        }
        hdr_len += n;
    }

    if ((size_t) hdr_len + 2 >= alloc) {
        char *p = (char *) realloc(hdr, alloc + 4);
        if (!p) {
            free(hdr);
            return -1;
        }
        hdr = p;
        alloc += 4;
    }

    hdr[hdr_len++] = '\r';
    hdr[hdr_len++] = '\n';

    if (http_send_raw(conn, hdr, (size_t) hdr_len) != 0) {
        free(hdr);
        return -1;
    }
    if (len > 0 && http_send_raw(conn, body, len) != 0) {
        free(hdr);
        return -1;
    }

    free(hdr);
    return 0;
}

static ssize_t read_all(int sock, char **out_buf)
{
    size_t cap = 1024;
    size_t len = 0;
    char *buf = malloc(cap);

    if (!buf) {
        return -1;
    }
    for (;;) {
        ssize_t n = recv(sock, buf + len, cap - len, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return -1;
        }
        if (n == 0) {
            break;
        }
        len += (size_t) n;
        if (len + 1 >= cap) {
            char *tmp;
            cap *= 2;
            tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                return -1;
            }
            buf = tmp;
        }
    }
    buf[len] = '\0';
    *out_buf = buf;
    return (ssize_t) len;
}

static int http_request_internal(const char *host, const char *port,
                                 const char *path, const char *method,
                                 const char *body, const char *headers[],
                                 char **response_out)
{
    int sock = -1;
    int rc = -1;
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    char *request = NULL;
    size_t offset = 0;

    DBG("[HTTP] http_request_internal: %s %s:%s%s\n", method, host, port, path);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        goto cleanup;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        goto cleanup;
    }

    request = malloc(MAX_BUFFER);
    if (!request) {
        goto cleanup;
    }

    offset += (size_t) snprintf(request + offset, MAX_BUFFER - offset,
                                "%s %s HTTP/1.1\r\nHost: %s:%s\r\n",
                                method, path, host, port);

    if (headers) {
        int i;
        for (i = 0; headers[i]; i++) {
            offset += (size_t) snprintf(request + offset, MAX_BUFFER - offset,
                                        "%s\r\n", headers[i]);
        }
    }

    if (body) {
        offset += (size_t) snprintf(request + offset, MAX_BUFFER - offset,
                                    "Content-Length: %zu\r\n", strlen(body));
    }

    offset += (size_t) snprintf(request + offset, MAX_BUFFER - offset, "\r\n");
    if (body) {
        offset += (size_t) snprintf(request + offset, MAX_BUFFER - offset, "%s", body);
    }

    if (send_all_nonblocking(sock, request, offset) != 0) {
        goto cleanup;
    }
    if (read_all(sock, response_out) < 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (res) {
        freeaddrinfo(res);
    }
    if (sock >= 0) {
        close(sock);
    }
    free(request);
    return rc;
}

int http_get(const char *host, const char *port, const char *path,
             const char *headers[], char **response_out)
{
    return http_request_internal(host, port, path, "GET", NULL, headers, response_out);
}

int http_post(const char *host, const char *port, const char *path,
              const char *body, const char *headers[], char **response_out)
{
    return http_request_internal(host, port, path, "POST", body, headers, response_out);
}

void http_free_response(char *response)
{
    free(response);
}
