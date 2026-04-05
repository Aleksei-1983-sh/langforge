#ifndef USER_HANDLER_H
#define USER_HANDLER_H

#include "libs/http.h"

void handle_login(http_connection_t *conn, http_request_t *req);
void handle_register(http_connection_t *conn, http_request_t *req);

#endif
