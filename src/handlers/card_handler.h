
#ifndef CARD_HANDLER_H
#define CARD_HANDLER_H

#include "libs/http.h"

void handle_index(http_connection_t *conn, http_request_t *req);
void handle_cards(http_connection_t *conn, http_request_t *req);
void handle_me(http_connection_t *conn, http_request_t *req);
void handle_static(http_connection_t *conn, http_request_t *req);
void handle_login(http_connection_t *conn, http_request_t *req);
void handle_register(http_connection_t *conn, http_request_t *req);
#endif

