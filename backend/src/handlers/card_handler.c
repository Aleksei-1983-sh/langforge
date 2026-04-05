#include "handlers/card_handler.h"

#include "dbug/dbug.h"
#include "libs/cJSON.h"
#include "services/card_service.h"

#include <stdlib.h>
#include <string.h>

static int
parse_query_param(const char *path, const char *key, char *out, size_t out_size)
{
    const char *q = strchr(path, '?');
    if (!q) {
        return -1;
    }

    q++;
    size_t key_len = strlen(key);
    while (*q) {
        const char *amp = strchr(q, '&');
        size_t seg_len = amp ? (size_t)(amp - q) : strlen(q);

        if (seg_len > key_len &&
            strncmp(q, key, key_len) == 0 &&
            q[key_len] == '=') {
            size_t value_len = seg_len - key_len - 1;
            if (value_len >= out_size) {
                value_len = out_size - 1;
            }

            memcpy(out, q + key_len + 1, value_len);
            out[value_len] = '\0';
            return 0;
        }

        if (!amp) {
            break;
        }
        q = amp + 1;
    }

    return -1;
}

void handle_cards(http_connection_t *conn, http_request_t *req)
{
    DBG("ENTER handle_cards: %s %s", req->method, req->path);

    if (strcmp(req->method, "GET") == 0) {
        char user_id_str[16];
        if (parse_query_param(req->path, "user_id", user_id_str, sizeof(user_id_str)) != 0) {
            http_send_response(conn, 400, "text/plain",
                               "Missing user_id\n", strlen("Missing user_id\n"));
            DBG("EXIT handle_cards GET: missing user_id");
            return;
        }

        int user_id = atoi(user_id_str);
        if (user_id <= 0) {
            http_send_response(conn, 400, "text/plain",
                               "Invalid user_id\n", strlen("Invalid user_id\n"));
            DBG("EXIT handle_cards GET: invalid user_id");
            return;
        }

        Word *words = NULL;
        size_t count = 0;
        if (card_service_list(user_id, &words, &count) != CARD_SERVICE_OK) {
            http_send_response(conn, 500, "text/plain",
                               "DB Error\n", strlen("DB Error\n"));
            DBG("EXIT handle_cards GET: DB error");
            return;
        }

        cJSON *json_array = cJSON_CreateArray();
        if (!json_array) {
            card_service_free_words(words, count);
            http_send_response(conn, 500, "text/plain",
                               "Server error\n", strlen("Server error\n"));
            DBG("EXIT handle_cards GET: JSON alloc error");
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            cJSON *item = cJSON_CreateObject();
            if (!item) {
                continue;
            }

            cJSON_AddStringToObject(item, "word", words[i].word ? words[i].word : "");
            cJSON_AddStringToObject(item, "transcription",
                                    words[i].transcription ? words[i].transcription : "");
            cJSON_AddStringToObject(item, "translation",
                                    words[i].translation ? words[i].translation : "");
            cJSON_AddStringToObject(item, "example", words[i].example ? words[i].example : "");
            cJSON_AddItemToArray(json_array, item);
        }

        card_service_free_words(words, count);

        char *out = cJSON_PrintUnformatted(json_array);
        cJSON_Delete(json_array);
        if (!out) {
            http_send_response(conn, 500, "text/plain",
                               "Server error\n", strlen("Server error\n"));
            DBG("EXIT handle_cards GET: JSON print error");
            return;
        }

        http_send_response(conn, 200, "application/json", out, strlen(out));
        free(out);
        DBG("EXIT handle_cards GET: success");
        return;
    }

    if (strcmp(req->method, "POST") == 0) {
        if (!req->body || req->body_len == 0) {
            http_send_response(conn, 400, "text/plain",
                               "Empty body\n", strlen("Empty body\n"));
            DBG("EXIT handle_cards POST: empty body");
            return;
        }

        cJSON *json_req = cJSON_Parse(req->body);
        if (!json_req) {
            http_send_response(conn, 400, "text/plain",
                               "Invalid JSON\n", strlen("Invalid JSON\n"));
            DBG("EXIT handle_cards POST: invalid JSON");
            return;
        }

        const cJSON *word_item = cJSON_GetObjectItem(json_req, "word");
        const cJSON *trans_item = cJSON_GetObjectItem(json_req, "transcription");
        const cJSON *translation_item = cJSON_GetObjectItem(json_req, "translation");
        const cJSON *example_item = cJSON_GetObjectItem(json_req, "example");
        const cJSON *user_id_item = cJSON_GetObjectItem(json_req, "user_id");

        if (!word_item || !trans_item || !translation_item || !example_item || !user_id_item ||
            !cJSON_IsString(word_item) || !cJSON_IsString(trans_item) ||
            !cJSON_IsString(translation_item) || !cJSON_IsString(example_item) ||
            !cJSON_IsNumber(user_id_item)) {
            cJSON_Delete(json_req);
            http_send_response(conn, 400, "text/plain",
                               "Missing fields\n", strlen("Missing fields\n"));
            DBG("EXIT handle_cards POST: missing/invalid fields");
            return;
        }

        const char *word = word_item->valuestring;
        const char *transcription = trans_item->valuestring;
        const char *translation = translation_item->valuestring;
        const char *example = example_item->valuestring;
        int user_id = user_id_item->valueint;

        if (user_id <= 0) {
            cJSON_Delete(json_req);
            http_send_response(conn, 400, "text/plain",
                               "Invalid user_id\n", strlen("Invalid user_id\n"));
            DBG("EXIT handle_cards POST: invalid user_id");
            return;
        }

        if (card_service_add(word, transcription, translation, example, user_id) != CARD_SERVICE_OK) {
            cJSON_Delete(json_req);
            http_send_response(conn, 500, "text/plain",
                               "DB Error\n", strlen("DB Error\n"));
            DBG("EXIT handle_cards POST: DB error");
            return;
        }

        cJSON_Delete(json_req);
        http_send_response(conn, 201, "application/json", "{}", strlen("{}"));
        DBG("EXIT handle_cards POST: success");
        return;
    }

    http_send_response(conn, 405, "text/plain",
                       "Method not allowed\n", strlen("Method not allowed\n"));
    DBG("EXIT handle_cards: method not allowed");
}
