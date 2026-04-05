#ifndef REALTIME_WS_H
#define REALTIME_WS_H

#include <stddef.h>

#include "libs/http.h"

int rt_ws_handshake(int fd, const char *request);
int rt_ws_send_text(int fd, const char *msg);
int rt_ws_read_frame(int fd, char *out, size_t out_size);
int rt_sse_send(int fd, const char *json);

void handle_realtime_ws(http_connection_t *conn, http_request_t *req);
void handle_realtime_sse(http_connection_t *conn, http_request_t *req);

#endif
