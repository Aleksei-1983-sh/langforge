#ifndef GENERATION_JOB_HANDLER_H
#define GENERATION_JOB_HANDLER_H

#include "libs/http.h"

void handle_generation_jobs_create(http_connection_t *conn, http_request_t *req);
void handle_generation_jobs_routes(http_connection_t *conn, http_request_t *req);

#endif
