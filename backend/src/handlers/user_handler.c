#include "handlers/user_handler.h"

#include "dbug/dbug.h"
#include "libs/cJSON.h"
#include "services/user_service.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static int appendf(char **pbuf, size_t *psize, size_t *plen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        return -1;
    }

    if (*plen + (size_t)need + 1 >= *psize) {
        size_t new_size = (*psize == 0) ? 1024 : *psize * 2;
        while (new_size <= *plen + (size_t)need) {
            new_size *= 2;
        }

        char *tmp = realloc(*pbuf, new_size);
        if (!tmp) {
            return -1;
        }

        *pbuf = tmp;
        *psize = new_size;
    }

    va_start(ap, fmt);
    int written = vsnprintf(*pbuf + *plen, *psize - *plen, fmt, ap);
    va_end(ap);
    if (written < 0) {
        return -1;
    }

    *plen += (size_t)written;
    return 0;
}

static int is_sensitive_header(const char *name)
{
    if (!name) {
        return 0;
    }

    if (strcasecmp(name, "authorization") == 0) return 1;
    if (strcasecmp(name, "cookie") == 0) return 1;
    if (strcasecmp(name, "set-cookie") == 0) return 1;
    return 0;
}

static void log_request_headers(const http_request_t *req)
{
    if (!req) {
        DEBUG_PRINT_CARD_HANDLER("Request: <NULL>");
        return;
    }

    char *buf = NULL;
    size_t bufsize = 0;
    size_t buflen = 0;

    if (req->query[0] != '\0') {
        appendf(&buf, &bufsize, &buflen, "HTTP Request: %s %s?%s\n",
                req->method[0] ? req->method : "-",
                req->path[0] ? req->path : "-",
                req->query);
    } else {
        appendf(&buf, &bufsize, &buflen, "HTTP Request: %s %s\n",
                req->method[0] ? req->method : "-",
                req->path[0] ? req->path : "-");
    }

    appendf(&buf, &bufsize, &buflen, "Headers (%d):\n", req->header_count);
    for (int i = 0; i < req->header_count && i < 16; ++i) {
        const char *name = req->headers[i][0] ? req->headers[i][0] : "(null)";
        const char *value = req->headers[i][1] ? req->headers[i][1] : "";

        if (is_sensitive_header(name)) {
            appendf(&buf, &bufsize, &buflen, "  %s: <redacted>\n", name);
        } else {
            appendf(&buf, &bufsize, &buflen, "  %s: %s\n", name, value);
        }
    }

    appendf(&buf, &bufsize, &buflen, "Body-Length: %zu\n", req->body_len);
    if (req->body && req->body_len > 0) {
        size_t show = req->body_len < 256 ? req->body_len : 256;
        char tmp[257];
        memcpy(tmp, req->body, show);
        tmp[show] = '\0';
        appendf(&buf, &bufsize, &buflen, "Body-Preview: %.256s\n", tmp);
    }

    if (buf) {
        DEBUG_PRINT_CARD_HANDLER("%s", buf);
        free(buf);
    }
}

static void log_response_headers(int status, const char *content_type,
                                 const char **headers, size_t headers_count)
{
    char *buf = NULL;
    size_t bufsize = 0;
    size_t buflen = 0;

    appendf(&buf, &bufsize, &buflen, "HTTP Response: %d %s\n",
            status, content_type ? content_type : "(none)");
    appendf(&buf, &bufsize, &buflen, "Headers (%zu):\n", headers_count);

    for (size_t i = 0; i < headers_count; ++i) {
        const char *header = headers[i] ? headers[i] : "";
        const char *colon = strchr(header, ':');
        if (!colon) {
            appendf(&buf, &bufsize, &buflen, "  %s\n", header);
            continue;
        }

        size_t name_len = (size_t)(colon - header);
        char name[128];
        if (name_len >= sizeof(name)) {
            name_len = sizeof(name) - 1;
        }
        memcpy(name, header, name_len);
        name[name_len] = '\0';

        const char *value = colon + 1;
        while (*value == ' ') {
            ++value;
        }

        if (is_sensitive_header(name)) {
            appendf(&buf, &bufsize, &buflen, "  %s: <redacted>\n", name);
        } else {
            appendf(&buf, &bufsize, &buflen, "  %s: %s\n", name, value);
        }
    }

    if (buf) {
        DEBUG_PRINT_CARD_HANDLER("%s", buf);
        free(buf);
    }
}

void handle_login(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER path='%s' body_len=%zu",
                             req->path ? req->path : "-", req->body_len);

    if (!req || !conn) {
        DEBUG_PRINT_CARD_HANDLER("bad args");
        return;
    }

    size_t saved_body_len = req->body_len;
    req->body_len = 0;
    log_request_headers(req);
    req->body_len = saved_body_len;

    if (req->body == NULL || req->body_len == 0) {
        log_response_headers(400, "text/plain", NULL, 0);
        http_send_response(conn, 400, "text/plain", "Empty body\n", strlen("Empty body\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    cJSON *json_req = cJSON_ParseWithLength(req->body, (int)req->body_len);
    if (!json_req) {
        log_response_headers(400, "text/plain", NULL, 0);
        http_send_response(conn, 400, "text/plain", "Invalid JSON\n", strlen("Invalid JSON\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    const cJSON *username_item = cJSON_GetObjectItemCaseSensitive(json_req, "username");
    const cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json_req, "password");
    const char *username = (username_item && cJSON_IsString(username_item)) ? username_item->valuestring : NULL;
    const char *password = (password_item && cJSON_IsString(password_item)) ? password_item->valuestring : NULL;

    if (!username || !password) {
        const char *json_hdrs[] = {
            "Cache-Control: no-store",
            "X-Content-Type-Options: nosniff"
        };
        log_response_headers(400, "application/json", json_hdrs,
                             sizeof(json_hdrs) / sizeof(json_hdrs[0]));

        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", 0);
        cJSON_AddStringToObject(err, "message", "Missing fields");
        send_json_response(conn, 400, err);
        cJSON_Delete(err);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    int user_id = 0;
    char *cookie_hdr = NULL;
    int service_rc = user_service_login(username, password, &user_id, &cookie_hdr);

    if (service_rc == USER_SERVICE_ERR_SERVER) {
        const char *json_hdrs[] = {
            "Cache-Control: no-store",
            "X-Content-Type-Options: nosniff"
        };
        log_response_headers(500, "application/json", json_hdrs,
                             sizeof(json_hdrs) / sizeof(json_hdrs[0]));

        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", 0);
        cJSON_AddStringToObject(err, "message", "Server error");
        send_json_response(conn, 500, err);
        cJSON_Delete(err);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    if (service_rc == USER_SERVICE_ERR_INVALID_CREDENTIALS) {
        const char *json_hdrs[] = {
            "Cache-Control: no-store",
            "X-Content-Type-Options: nosniff"
        };
        log_response_headers(401, "application/json", json_hdrs,
                             sizeof(json_hdrs) / sizeof(json_hdrs[0]));

        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", 0);
        cJSON_AddStringToObject(err, "message", "Invalid credentials");
        send_json_response(conn, 401, err);
        cJSON_Delete(err);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    cJSON *out_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(out_obj, "success", 1);
    cJSON_AddNumberToObject(out_obj, "user_id", user_id);
    char *out_text = cJSON_PrintUnformatted(out_obj);

    const char *hdrs[] = {
        cookie_hdr,
        "Cache-Control: no-store",
        "X-Content-Type-Options: nosniff"
    };
    log_response_headers(200, "application/json", hdrs, sizeof(hdrs) / sizeof(hdrs[0]));
    my_send_response_with_headers(conn, 200, "application/json", out_text, strlen(out_text),
                                  hdrs, sizeof(hdrs) / sizeof(hdrs[0]));

    free(out_text);
    cJSON_Delete(out_obj);
    free(cookie_hdr);
    cJSON_Delete(json_req);

    DEBUG_PRINT_CARD_HANDLER("success user_id=%d (session issued)", user_id);
    DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
}

void handle_register(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER handle_register: path='%s' body_len=%zu",
                             req->path ? req->path : "-", req->body_len);

    if (!req || !conn) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: bad args");
        return;
    }

    if (req->body == NULL || req->body_len == 0) {
        http_send_response(conn, 400, "text/plain", "Empty body\n", strlen("Empty body\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    cJSON *json_req = cJSON_ParseWithLength(req->body, (int)req->body_len);
    if (!json_req) {
        http_send_response(conn, 400, "text/plain", "Invalid JSON\n", strlen("Invalid JSON\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    const cJSON *username_item = cJSON_GetObjectItemCaseSensitive(json_req, "username");
    const cJSON *email_item = cJSON_GetObjectItemCaseSensitive(json_req, "email");
    const cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json_req, "password");

    const char *username = (username_item && cJSON_IsString(username_item)) ? username_item->valuestring : NULL;
    const char *email = (email_item && cJSON_IsString(email_item)) ? email_item->valuestring : NULL;
    const char *password = (password_item && cJSON_IsString(password_item)) ? password_item->valuestring : NULL;

    if (!username || !email || !password) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Missing fields");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    int new_user_id = 0;
    int service_rc = user_service_register(username, email, password, &new_user_id);

    if (service_rc == USER_SERVICE_ERR_PASSWORD_TOO_SHORT) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Password too short (min 6 chars)");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    if (service_rc == USER_SERVICE_ERR_INVALID_EMAIL) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Invalid email");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    if (service_rc == USER_SERVICE_OK && new_user_id > 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 1);
        cJSON_AddNumberToObject(res, "user_id", new_user_id);
        send_json_response(conn, 201, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    if (service_rc == USER_SERVICE_OK && new_user_id == 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 1);
        cJSON_AddStringToObject(res, "message", "Registered (id not returned)");
        send_json_response(conn, 201, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    if (service_rc == USER_SERVICE_ERR_CONFLICT) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "User already exists");
        send_json_response(conn, 409, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "success", 0);
    cJSON_AddStringToObject(res, "message", "Registration failed due to server error");
    send_json_response(conn, 500, res);
    cJSON_Delete(res);
    cJSON_Delete(json_req);
    DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
}
