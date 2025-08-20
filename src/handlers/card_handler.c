#include "libs/cJSON.h"
#include "dbug/dbug.h"
#include "db/db.h"
#include "libs/http.h"

#include <string.h> /* strcmp, strchr, strncmp, memcpy */
#include <stdlib.h> /* atoi, free */
#include <sys/stat.h>

/*
 * Вспомогательная функция: извлечь значение параметра key из query-параметра.
 * path: строка вида "/api/cards?user_id=123&other=...".
 * key: имя параметра, например "user_id".
 * out: буфер для значения, включая завершающий '\0'.
 * out_size: размер буфера out.
 * Возвращает 0 при успешном извлечении, -1 если параметр не найден.
 */
static int
parse_query_param(const char *path, const char *key, char *out, size_t out_size)
{
    const char *q = strchr(path, '?');
    if (!q)
        return -1;
    q++; /* пропускаем '?' */
    size_t key_len = strlen(key);
    while (*q) {
        /* найти конец этой пары key=value */
        const char *amp = strchr(q, '&');
        size_t seg_len = amp ? (size_t)(amp - q) : strlen(q);
        /* проверяем, начинается ли сегмент с key= */
        if (seg_len > key_len && strncmp(q, key, key_len) == 0 && q[key_len] == '=') {
            size_t vlen = seg_len - key_len - 1;
            if (vlen >= out_size)
                vlen = out_size - 1;
            memcpy(out, q + key_len + 1, vlen);
            out[vlen] = '\0';
            return 0;
        }
        if (!amp)
            break;
        q = amp + 1;
    }
    return -1;
}

/*
 * Обработчик /api/cards:
 *   GET  /api/cards?user_id=...
 *   POST /api/cards  (body JSON)
 */
void
handle_cards(http_connection_t *conn, http_request_t *req)
{
    DBG("ENTER handle_cards: %s %s", req->method, req->path);

    if (strcmp(req->method, "GET") == 0) {
        /* GET /api/cards?user_id=... */
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
        if (db_get_all_words(&words, &count, user_id) != 0) {
            http_send_response(conn, 500, "text/plain",
                               "DB Error\n", strlen("DB Error\n"));
            DBG("EXIT handle_cards GET: DB error");
            return;
        }

        cJSON *json_array = cJSON_CreateArray();
        if (!json_array) {
            /* Ошибка аллокации */
            /* Освобождаем полученные слова */
            for (size_t i = 0; i < count; i++) {
                free(words[i].word);
                free(words[i].transcription);
                free(words[i].translation);
                free(words[i].example);
            }
            free(words);
            http_send_response(conn, 500, "text/plain",
                               "Server error\n", strlen("Server error\n"));
            DBG("EXIT handle_cards GET: JSON alloc error");
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            cJSON *item = cJSON_CreateObject();
            if (item) {
                cJSON_AddStringToObject(item, "word", words[i].word ? words[i].word : "");
                cJSON_AddStringToObject(item, "transcription", words[i].transcription ? words[i].transcription : "");
                cJSON_AddStringToObject(item, "translation", words[i].translation ? words[i].translation : "");
                cJSON_AddStringToObject(item, "example", words[i].example ? words[i].example : "");
                cJSON_AddItemToArray(json_array, item);
            }
            /* Освобождаем память из db_get_all_words */
            free(words[i].word);
            free(words[i].transcription);
            free(words[i].translation);
            free(words[i].example);
        }
        free(words);

        char *out = cJSON_PrintUnformatted(json_array);
        cJSON_Delete(json_array);
        if (out) {
            http_send_response(conn, 200, "application/json", out, strlen(out));
            free(out);
            DBG("EXIT handle_cards GET: success");
        } else {
            http_send_response(conn, 500, "text/plain",
                               "Server error\n", strlen("Server error\n"));
            DBG("EXIT handle_cards GET: JSON print error");
        }

    } else if (strcmp(req->method, "POST") == 0) {
        /* POST /api/cards */
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

        const cJSON *word_item         = cJSON_GetObjectItem(json_req, "word");
        const cJSON *trans_item        = cJSON_GetObjectItem(json_req, "transcription");
        const cJSON *translation_item  = cJSON_GetObjectItem(json_req, "translation");
        const cJSON *example_item      = cJSON_GetObjectItem(json_req, "example");
        const cJSON *user_id_item      = cJSON_GetObjectItem(json_req, "user_id");

        if (!word_item || !trans_item || !translation_item || !example_item || !user_id_item
            || !cJSON_IsString(word_item) 
            || !cJSON_IsString(trans_item)
            || !cJSON_IsString(translation_item)
            || !cJSON_IsString(example_item)
            || !cJSON_IsNumber(user_id_item))
        {
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

        if (db_add_word(word, transcription, translation, example, user_id) != 0) {
            cJSON_Delete(json_req);
            http_send_response(conn, 500, "text/plain",
                               "DB Error\n", strlen("DB Error\n"));
            DBG("EXIT handle_cards POST: DB error");
            return;
        }

        cJSON_Delete(json_req);
        /* Успешно создано */
        http_send_response(conn, 201, "application/json", "{}", strlen("{}"));
        DBG("EXIT handle_cards POST: success");

    } else {
        /* Метод не поддерживается */
        http_send_response(conn, 405, "text/plain",
                           "Method not allowed\n", strlen("Method not allowed\n"));
        DBG("EXIT handle_cards: method not allowed");
    }
}

void handle_login(http_connection_t *conn, http_request_t *req) {
    DBG("ENTER");

    if (req->body == NULL || req->body_len == 0) {
        http_send_response(conn, 400, "text/plain", "Empty body\n", strlen("Empty body\n"));
        goto exit;
    }

    cJSON *json_req = cJSON_Parse(req->body);
    if (!json_req) {
        http_send_response(conn, 400, "text/plain", "Invalid JSON\n", strlen("Invalid JSON\n"));
        goto exit;
    }

    const cJSON *username_item = cJSON_GetObjectItem(json_req, "username");
    const cJSON *password_item = cJSON_GetObjectItem(json_req, "password");

    const char *username = (username_item && cJSON_IsString(username_item)) ? username_item->valuestring : NULL;
    const char *password = (password_item && cJSON_IsString(password_item)) ? password_item->valuestring : NULL;

    if (!username || !password) {
        cJSON_Delete(json_req);
        http_send_response(conn, 400, "text/plain", "Missing fields\n", strlen("Missing fields\n"));
        goto exit;
    }

    int user_id = db_login_user(username, password);
    cJSON_Delete(json_req);

    DBG("user_id: %d", user_id);

    if (user_id < 0) {
        const char *err_json = "{ \"success\": false, \"message\": \"Invalid credentials\" }";
        http_send_response(conn, 401, "application/json", err_json, strlen(err_json));
        goto exit;
    }

    const char *ok_json = "{ \"success\": true }";
    http_send_response(conn, 200, "application/json", ok_json, strlen(ok_json));

    /*
    // Альтернативный вариант с динамическим JSON-ответом
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "user_id", user_id);
    char *out = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);

    http_send_response(conn, 200, "application/json", out, strlen(out));
    free(out);
    */

exit:
    DBG("EXIT");
}

/* Функция для определения MIME-типа по расширению */
const char* get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
    if (strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".json") == 0) return "application/json";
    // можно добавить другие типы по необходимости

    return "application/octet-stream";
}

/* Функция для чтения файла */

static char* read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0) { // проверяем корректность размера файла
        fclose(f);
        return NULL;
    }

    rewind(f);

    char *buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, f);
    fclose(f);

    if (read_bytes != (size_t)size) {
        free(buffer);
        return NULL;
    }

    if (out_size) *out_size = read_bytes;

    return buffer;
}


/* Проверка, существует ли файл */
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Главный обработчик статических файлов */
void handle_static(http_connection_t *conn, http_request_t *req)
{
    const char *base_dir = "www";
    char file_path[1024];

    const char *url_path = req->path;  // предположим, что в запросе есть поле path с URL

    // если запрос на корень "/", отдадим index.html
    if (strcmp(url_path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/login/index.html", base_dir);
    } else {
        // убираем ведущий слеш
        if (*url_path == '/')
            url_path++;
        snprintf(file_path, sizeof(file_path), "%s/%s", base_dir, url_path);
    }

    if (!file_exists(file_path)) {
        const char *notfound = "404 Not Found";
        http_send_response(conn, 404, "text/plain", notfound, strlen(notfound));
        return;
    }

    size_t size;
    char *body = read_file(file_path, &size);
    if (!body) {
        const char *err = "500 Internal Server Error";
        http_send_response(conn, 500, "text/plain", err, strlen(err));
        return;
    }

    const char *mime = get_mime_type(file_path);
    http_send_response(conn, 200, mime, body, size);
    free(body);
}


