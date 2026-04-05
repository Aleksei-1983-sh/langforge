#include "handlers/profile_handler.h"

#include "dbug/dbug.h"
#include "libs/cJSON.h"
#include "services/user_service.h"

#include <stdlib.h>
#include <string.h>

static int send_json_response(http_connection_t *conn, int status, cJSON *obj)
{
    if (!conn || !obj) {
        return -1;
    }

    char *out = cJSON_PrintUnformatted(obj);
    if (!out) {
        return -1;
    }

    const char *hdrs[] = {
        "Cache-Control: no-store",
        "X-Content-Type-Options: nosniff"
    };
    int rc = my_send_response_with_headers(conn, status, "application/json", out, strlen(out),
                                           hdrs, sizeof(hdrs) / sizeof(hdrs[0]));
    free(out);
    return rc;
}

void handle_me(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER handle_me: path='%s'", req && req->path ? req->path : "-");

    if (!conn || !req) {
        DEBUG_PRINT_CARD_HANDLER("handle_me: bad args");
        return;
    }

    const char *cookie_hdr = http_get_header(req, "Cookie");
    if (!cookie_hdr) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Unauthorized");
        send_json_response(conn, 401, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_me: no cookie");
        return;
    }

    user_profile_t profile;
    int service_rc = user_service_get_profile(cookie_hdr, &profile);
    if (service_rc == USER_SERVICE_ERR_UNAUTHORIZED) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Unauthorized");
        send_json_response(conn, 401, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_me: unauthorized");
        return;
    }

    if (service_rc != USER_SERVICE_OK) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Server error");
        send_json_response(conn, 500, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_me: service error=%d", service_rc);
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "username", profile.username);
    cJSON_AddNumberToObject(user, "words_learned", profile.words_learned);
    cJSON_AddNumberToObject(user, "active_lessons", profile.active_lessons);
    cJSON_AddItemToObject(resp, "user", user);
    cJSON_AddBoolToObject(resp, "success", 1);

    send_json_response(conn, 200, resp);
    cJSON_Delete(resp);

    DEBUG_PRINT_CARD_HANDLER("EXIT handle_me: success username='%s'", profile.username);
}
