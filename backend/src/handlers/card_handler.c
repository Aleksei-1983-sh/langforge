#include "libs/cJSON.h"
#include "dbug/dbug.h"
#include "libs/http.h"
#include "services/user_service.h"
#include "services/card_service.h"

#include <string.h> /* strcmp, strchr, strncmp, memcpy */
#include <stdlib.h> /* atoi, free */
#include <sys/stat.h>
#include <stdbool.h>
#include <stdarg.h>
#include <strings.h> /* for strcasecmp */

#include <unistd.h>     // readlink, getcwd
#include <libgen.h>     // dirname
#include <errno.h>      // errno
#include <limits.h>     // PATH_MAX

#define PATH_MAX_SAFE PATH_MAX
#define SIZ_PATH 128

static char g_abs_www[PATH_MAX_SAFE] = "";

/* Вспомогательная: проверить что путь существует и это директория */
static int is_dir(const char *path)
{
    struct stat st;

    if (!path || !*path) {
        DEBUG_PRINT_CARD_HANDLER("is_dir: empty path");
        return 0;
    }

    if (stat(path, &st) != 0) {
        DEBUG_PRINT_CARD_HANDLER("is_dir: stat('%s') failed: %s",
                                 path, strerror(errno));
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        DEBUG_PRINT_CARD_HANDLER("is_dir: '%s' exists but is NOT directory",
                                 path);
        return 0;
    }

    return 1;
}

/*
 * resolve_www_dir()
 *
 * Алгоритм:
 *   1) realpath("www")
 *   2) cwd/../www
 *   3) fallback — использовать относительный "www"
 *
 * Должна вызываться один раз при старте сервера.
 */
void resolve_www_dir(void)
{
    char cwd[PATH_MAX_SAFE];
    char candidate[PATH_MAX_SAFE];
    char resolved[PATH_MAX_SAFE];

    DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: === START ===");
    DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: PATH_MAX=%d",
                             (int)PATH_MAX_SAFE);

    /* --- Шаг 1: realpath("www") --- */
    DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: trying realpath(\"www\")");

    errno = 0;
    if (realpath("www", resolved) != NULL) {
        DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: realpath success: '%s'",
                                 resolved);

        if (is_dir(resolved)) {
            strncpy(g_abs_www, resolved, sizeof(g_abs_www) - 1);
            g_abs_www[sizeof(g_abs_www) - 1] = '\0';

            DEBUG_PRINT_CARD_HANDLER(
                "resolve_www_dir: using absolute www: '%s' (len=%zu)",
                g_abs_www, strlen(g_abs_www));
            return;
        } else {
            DEBUG_PRINT_CARD_HANDLER(
                "resolve_www_dir: realpath resolved but not directory");
        }
    } else {
        DEBUG_PRINT_CARD_HANDLER(
            "resolve_www_dir: realpath(\"www\") failed: %s",
            strerror(errno));
    }

    /* --- Шаг 2: cwd/../www --- */
    DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: trying cwd/../www");

    errno = 0;
    if (!getcwd(cwd, sizeof(cwd))) {
        DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: getcwd failed: %s",
                                 strerror(errno));
    } else {
        DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: cwd='%s' (len=%zu)",
                                 cwd, strlen(cwd));

        int written = snprintf(candidate, sizeof(candidate),
                               "%s/../www", cwd);

        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            DEBUG_PRINT_CARD_HANDLER(
                "resolve_www_dir: snprintf overflow while building candidate");
        } else {
            DEBUG_PRINT_CARD_HANDLER(
                "resolve_www_dir: candidate='%s'", candidate);

            errno = 0;
            if (realpath(candidate, resolved) != NULL) {
                DEBUG_PRINT_CARD_HANDLER(
                    "resolve_www_dir: realpath(candidate) success: '%s'",
                    resolved);

                if (is_dir(resolved)) {
                    strncpy(g_abs_www, resolved,
                            sizeof(g_abs_www) - 1);
                    g_abs_www[sizeof(g_abs_www) - 1] = '\0';

                    DEBUG_PRINT_CARD_HANDLER(
                        "resolve_www_dir: using cwd-based absolute www: '%s'",
                        g_abs_www);
                    return;
                } else {
                    DEBUG_PRINT_CARD_HANDLER(
                        "resolve_www_dir: candidate resolved but not directory");
                }
            } else {
                DEBUG_PRINT_CARD_HANDLER(
                    "resolve_www_dir: realpath(candidate) failed: %s",
                    strerror(errno));
            }
        }
    }

    /* --- fallback --- */
    g_abs_www[0] = '\0';

    DEBUG_PRINT_CARD_HANDLER(
        "resolve_www_dir: absolute www not found, fallback to relative 'www'");
    DEBUG_PRINT_CARD_HANDLER("resolve_www_dir: === END ===");
}

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


static int send_json_response(http_connection_t *conn, int status, cJSON *obj)
{
    if (!conn || !obj) return -1;
    char *out = cJSON_PrintUnformatted(obj);
    if (!out) return -1;

    const char *hdrs[] = {
        "Cache-Control: no-store",
        "X-Content-Type-Options: nosniff"
    };
    int rc = my_send_response_with_headers(conn, status, "application/json", out, strlen(out),
                                           hdrs, sizeof(hdrs) / sizeof(hdrs[0]));
    free(out);
    return rc;
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

/* Проверка, существует ли файл */
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Чтение файла в буфер. Возвращает malloc'ный указатель (нужно free). При ошибке возвращает NULL. */
static char *read_file_to_buffer(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long s = ftell(f);
    if (s < 0) { fclose(f); return NULL; }
    rewind(f);

    /* Ограничение — защитим сервер от чтения гигантов без контроля.
       Здесь 32 MB — пример, отрегулируйте для вашего окружения. */
    const long MAX_LOAD = 32L * 1024L * 1024L;
    if (s > MAX_LOAD) {
        /* либо можно читать потоково, либо отказать */
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)s);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)s, f);
    fclose(f);
    if (read != (size_t)s) { free(buf); return NULL; }

    *out_size = (size_t)s;
    return buf;
}

/* Убираем query-string и фрагмент из URL */
static void strip_query_and_fragment(const char *in, char *out, size_t outlen)
{
    size_t i = 0;
    if (!in || !out) return;
    while (in[i] != '\0' && in[i] != '?' && in[i] != '#') {
        if (i + 1 >= outlen) break;
        out[i] = in[i];
        i++;
    }
    out[i] = '\0';
}

/* Проверка на наличие '..' сегментов (простая, но эффективная) */
static bool contains_dot_dot(const char *p)
{
    if (!p) return false;
    const char *s = p;
    while ((s = strstr(s, "..")) != NULL) {
        /* убедимся, что это сегмент (границы / или начало/конец) */
        bool left_ok = (s == p) || (*(s - 1) == '/');
        bool right_ok = (*(s + 2) == '\0') || (*(s + 2) == '/');
        if (left_ok && right_ok) return true;
        s++; /* продолжить поиск */
    }
    return false;
}

/* Проверяем наличие расширения у последнего сегмента */
static bool last_segment_has_extension(const char *p)
{
    if (!p) return false;
    const char *last_slash = strrchr(p, '/');
    const char *segment = last_slash ? last_slash + 1 : p;
    return (strchr(segment, '.') != NULL);
}

/* Собираем путь безопасно в buffer: base + '/' + rel (предполагается rel без ведущего '/').
   Возвращаем false если результат превышает buflen-1. */
static bool join_path(const char *base, const char *rel, char *buffer, size_t buflen)
{
    if (!base || !rel || !buffer) return false;
    if (snprintf(buffer, buflen, "%s/%s", base, rel) >= (int)buflen) return false;
    return true;
}

/* --- вспомогательная функция для безопасного добавления в буфер --- */
static int _appendf(char **pbuf, size_t *psize, size_t *plen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return -1;

    /* убедиться, что в буфере достаточно места */
    if (*plen + (size_t)need + 1 >= *psize) {
        size_t newsize = (*psize == 0) ? 1024 : *psize * 2;
        while (newsize <= *plen + (size_t)need) newsize *= 2;
        char *p = realloc(*pbuf, newsize);
        if (!p) return -1;
        *pbuf = p;
        *psize = newsize;
    }

    va_start(ap, fmt);
    int written = vsnprintf(*pbuf + *plen, *psize - *plen, fmt, ap);
    va_end(ap);
    if (written < 0) return -1;
    *plen += (size_t)written;
    return 0;
}

/* --- helper: проверить, чувствительный ли заголовок (case-insensitive) --- */
static int _is_sensitive_header(const char *name)
{
    if (!name) return 0;
    /* сравнение без учёта регистра */
    if (strcasecmp(name, "authorization") == 0) return 1;
    if (strcasecmp(name, "cookie") == 0) return 1;
    if (strcasecmp(name, "set-cookie") == 0) return 1;
    return 0;
}

/* --- Функция 1: логирование заголовков запроса --- */
static void log_request_headers(const http_request_t *req)
{
    if (!req) {
        DEBUG_PRINT_CARD_HANDLER("Request: <NULL>");
        return;
    }

    char *buf = NULL;
    size_t bufsize = 0, buflen = 0;

    /* Первая строка: метод, путь и query */
    if (req->query[0] != '\0') {
        _appendf(&buf, &bufsize, &buflen, "HTTP Request: %s %s?%s\n", req->method[0] ? req->method : "-", 
                 req->path[0] ? req->path : "-", req->query);
    } else {
        _appendf(&buf, &bufsize, &buflen, "HTTP Request: %s %s\n", req->method[0] ? req->method : "-", 
                 req->path[0] ? req->path : "-");
    }

    /* Заголовки */
    _appendf(&buf, &bufsize, &buflen, "Headers (%d):\n", req->header_count);
    for (int i = 0; i < req->header_count && i < 16; ++i) {
        const char *name = req->headers[i][0];
        const char *value = req->headers[i][1];
        if (!name) name = "(null)";
        if (!value) value = "";

        if (_is_sensitive_header(name)) {
            _appendf(&buf, &bufsize, &buflen, "  %s: <redacted>\n", name);
        } else {
            _appendf(&buf, &bufsize, &buflen, "  %s: %s\n", name, value);
        }
    }

    /* тело — печатаем только длину (и первые N байт если нужно) */
    _appendf(&buf, &bufsize, &buflen, "Body-Length: %zu\n", req->body_len);
    if (req->body && req->body_len > 0) {
        /* показываем первые 256 байт тела (без бинарных символов можно заменить) */
        size_t show = req->body_len < 256 ? req->body_len : 256;
        /* текстовая безопасная копия (вырезаем NULs) */
        char tmp[257];
        memcpy(tmp, req->body, show);
        tmp[show] = '\0';
        /* если есть управляющие символы, их можно заменить, но для простоты выведем */
        _appendf(&buf, &bufsize, &buflen, "Body-Preview: %.256s\n", tmp);
    }

    /* один вызов макроса логирования (он добавляет файл/строку/перевод строки) */
    if (buf) {
        DEBUG_PRINT_CARD_HANDLER("%s", buf);
        free(buf);
    } else {
        DEBUG_PRINT_CARD_HANDLER("HTTP Request: <empty>");
    }
}

/* --- Функция 2: логирование заголовков ответа --- */
/* headers: массив строк формата "Name: value" или просто "Name" (может быть NULL) */
static void log_response_headers(int status, const char *content_type, const char **headers, size_t headers_count)
{
    char *buf = NULL;
    size_t bufsize = 0, buflen = 0;

    _appendf(&buf, &bufsize, &buflen, "HTTP Response: %d %s\n", status, content_type ? content_type : "(none)");
    _appendf(&buf, &bufsize, &buflen, "Headers (%zu):\n", headers_count);

    for (size_t i = 0; i < headers_count; ++i) {
        const char *h = headers[i] ? headers[i] : "";
        /* Попробуем выделить имя до ':' */
        const char *colon = strchr(h, ':');
        if (colon) {
            size_t namelen = (size_t)(colon - h);
            char name[128];
            if (namelen >= sizeof(name)) namelen = sizeof(name)-1;
            memcpy(name, h, namelen);
            name[namelen] = '\0';
            /* пропускаем ": " */
            const char *val = colon + 1;
            while (*val == ' ') ++val;

            if (_is_sensitive_header(name)) {
                _appendf(&buf, &bufsize, &buflen, "  %s: <redacted>\n", name);
            } else {
                _appendf(&buf, &bufsize, &buflen, "  %s: %s\n", name, val);
            }
        } else {
            /* нет разделителя, просто выводим строку */
            _appendf(&buf, &bufsize, &buflen, "  %s\n", h);
        }
    }

    if (buf) {
        DEBUG_PRINT_CARD_HANDLER("%s", buf);
        free(buf);
    } else {
        DEBUG_PRINT_CARD_HANDLER("HTTP Response: <empty>");
    }
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
        if (card_service_list(user_id, &words, &count) != CARD_SERVICE_OK) {
            http_send_response(conn, 500, "text/plain",
                               "DB Error\n", strlen("DB Error\n"));
            DBG("EXIT handle_cards GET: DB error");
            return;
        }

        cJSON *json_array = cJSON_CreateArray();
        if (!json_array) {
            /* Ошибка аллокации */
            /* Освобождаем полученные слова */
            card_service_free_words(words, count);
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
        }
        card_service_free_words(words, count);

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

        if (card_service_add(word, transcription, translation, example, user_id) != CARD_SERVICE_OK) {
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

void handle_login(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER path='%s' body_len=%zu",
                             req->path ? req->path : "-", req->body_len);

    if (!req || !conn) {
        DEBUG_PRINT_CARD_HANDLER("bad args");
        return;
    }

    /* --- Логирование запроса, но без предварительного показа тела (чтобы не логировать пароли) --- */
    size_t saved_body_len = req->body_len;
    req->body_len = 0;                 /* временно скрываем тело для логирования */
    log_request_headers(req);
    req->body_len = saved_body_len;    /* восстанавливаем */

    if (req->body == NULL || req->body_len == 0) {
        DEBUG_PRINT_CARD_HANDLER("empty body");

        /* логируем ответ (plain text) */
        log_response_headers(400, "text/plain", NULL, 0);

        http_send_response(conn, 400, "text/plain", "Empty body\n", strlen("Empty body\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    cJSON *json_req = cJSON_ParseWithLength(req->body, (int)req->body_len);
    if (!json_req) {
        DEBUG_PRINT_CARD_HANDLER("invalid JSON");

        /* логируем ответ (plain text) */
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
        DEBUG_PRINT_CARD_HANDLER("missing username/password");
        cJSON_Delete(json_req);

        /* JSON error response headers for logging */
        const char *json_hdrs[] = { "Cache-Control: no-store", "X-Content-Type-Options: nosniff" };
        log_response_headers(400, "application/json", json_hdrs, sizeof(json_hdrs)/sizeof(json_hdrs[0]));

        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", 0);
        cJSON_AddStringToObject(err, "message", "Missing fields");
        send_json_response(conn, 400, err);
        cJSON_Delete(err);

        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    DEBUG_PRINT_CARD_HANDLER("attempt login username='%s'", username);

    int user_id = 0;
    char *cookie_hdr = NULL;
    int service_rc = user_service_login(username, password, &user_id, &cookie_hdr);

    DEBUG_PRINT_CARD_HANDLER("user_service_login -> %d, user_id=%d", service_rc, user_id);

    if (service_rc == USER_SERVICE_ERR_SERVER) {
        ERROR_PRINT("user_service_login returned server error");

        const char *json_hdrs[] = { "Cache-Control: no-store", "X-Content-Type-Options: nosniff" };
        log_response_headers(500, "application/json", json_hdrs, sizeof(json_hdrs)/sizeof(json_hdrs[0]));

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
        DEBUG_PRINT_CARD_HANDLER("invalid credentials for username='%s'", username);

        const char *json_hdrs[] = { "Cache-Control: no-store", "X-Content-Type-Options: nosniff" };
        log_response_headers(401, "application/json", json_hdrs, sizeof(json_hdrs)/sizeof(json_hdrs[0]));

        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", 0);
        cJSON_AddStringToObject(err, "message", "Invalid credentials");
        send_json_response(conn, 401, err);
        cJSON_Delete(err);

        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
        return;
    }

    /* Prepare JSON response and headers for success */
    cJSON *out_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(out_obj, "success", 1);
    cJSON_AddNumberToObject(out_obj, "user_id", user_id);
    char *out_text = cJSON_PrintUnformatted(out_obj);

    const char *hdrs[] = {
        cookie_hdr,
        "Cache-Control: no-store",
        "X-Content-Type-Options: nosniff"
    };

    /* Log response headers (what we'll send) */
    log_response_headers(200, "application/json", hdrs, sizeof(hdrs)/sizeof(hdrs[0]));

    /* Send response with headers */
    my_send_response_with_headers(conn, 200, "application/json", out_text, strlen(out_text),
                                  hdrs, sizeof(hdrs) / sizeof(hdrs[0]));

    /* Cleanup */
    free(out_text);
    cJSON_Delete(out_obj);
    free(cookie_hdr);
    cJSON_Delete(json_req);

    DEBUG_PRINT_CARD_HANDLER("success user_id=%d (session issued)", user_id);
    DEBUG_PRINT_CARD_HANDLER("EXIT handle_login");
}

void handle_register(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER handle_register: path='%s' body_len=%zu", req->path ? req->path : "-", req->body_len);

    if (!req || !conn) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: bad args");
        return;
    }

    if (req->body == NULL || req->body_len == 0) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: empty body");
        http_send_response(conn, 400, "text/plain", "Empty body\n", strlen("Empty body\n"));
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    cJSON *json_req = cJSON_ParseWithLength(req->body, (int)req->body_len);
    if (!json_req) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: invalid JSON");
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
        DEBUG_PRINT_CARD_HANDLER("handle_register: missing fields");
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Missing fields");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    DEBUG_PRINT_CARD_HANDLER("handle_register: registering username='%s' email='%s'", username, email);

    int new_user_id = 0;
    int service_rc = user_service_register(username, email, password, &new_user_id);

    DEBUG_PRINT_CARD_HANDLER("handle_register: user_service_register returned %d user_id=%d",
                             service_rc, new_user_id);

    if (service_rc == USER_SERVICE_ERR_PASSWORD_TOO_SHORT) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: password too short");
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Password too short (min 6 chars)");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    } else if (service_rc == USER_SERVICE_ERR_INVALID_EMAIL) {
        DEBUG_PRINT_CARD_HANDLER("handle_register: invalid email '%s'", email);
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Invalid email");
        send_json_response(conn, 400, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    } else if (service_rc == USER_SERVICE_OK && new_user_id > 0) {
        /* Успешно создан пользователь, возвращаем 201 + id */
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 1);
        cJSON_AddNumberToObject(res, "user_id", new_user_id);
        send_json_response(conn, 201, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("success user_id=%d", new_user_id);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    } else if (service_rc == USER_SERVICE_OK && new_user_id == 0) {
        /* Вставка прошла, но id не возвращён — редкий случай.
           Считаем это успешной регистрацией, отдаём 201 без user_id, но логируем. */
        DEBUG_PRINT_CARD_HANDLER("insert OK but no id returned (new_user_id==0)");
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 1);
        /* можно добавить предупреждение/notice */
        cJSON_AddStringToObject(res, "message", "Registered (id not returned)");
        send_json_response(conn, 201, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    } else if (service_rc == USER_SERVICE_ERR_CONFLICT) {
        /* Конфликт — пользователь уже существует */
        DEBUG_PRINT_CARD_HANDLER("conflict (user exists)");
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "User already exists");
        send_json_response(conn, 409, res); /* 409 Conflict */
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    } else {
        ERROR_PRINT("handle_register: user_service_register failed with code=%d", service_rc);
        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "Registration failed due to server error");
        send_json_response(conn, 500, res);
        cJSON_Delete(res);
        cJSON_Delete(json_req);
        DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
        return;
    }

    /* Успешная регистрация */
    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "success", 1);
    cJSON_AddNumberToObject(res, "user_id", new_user_id);

    send_json_response(conn, 201, res); /* 201 Created */
    cJSON_Delete(res);
    cJSON_Delete(json_req);

    DEBUG_PRINT_CARD_HANDLER("handle_register: success user_id=%d", new_user_id);
    DEBUG_PRINT_CARD_HANDLER("EXIT handle_register");
}

/* --- Основной handler для /api/me (POST) --- */
void handle_me(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER path='%s'", req->path ? req->path : "-");

    if (!conn || !req) {
        DEBUG_PRINT_CARD_HANDLER("bad args");
        return;
    }

    log_request_headers(req);

    /* Получаем Cookie header */
    const char *cookie_hdr = http_get_header(req, "Cookie");
    if (!cookie_hdr) {
        DEBUG_PRINT_CARD_HANDLER("no Cookie header");
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Unauthorized");
        send_json_response(conn, 401, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT");
        return;
    }

    user_profile_t profile;
    int service_rc = user_service_get_profile(cookie_hdr, &profile);
    if (service_rc == USER_SERVICE_ERR_UNAUTHORIZED) {
        DEBUG_PRINT_CARD_HANDLER("session cookie not found or invalid");
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Unauthorized");
        send_json_response(conn, 401, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT ");
        return;
    }

    if (service_rc != USER_SERVICE_OK) {
        ERROR_PRINT("user_service_get_profile failed with code=%d", service_rc);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "Server error");
        send_json_response(conn, 500, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT ");
        return;
    }

    /* Формируем JSON-ответ */
    cJSON *resp = cJSON_CreateObject();
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "username", profile.username);
    cJSON_AddNumberToObject(user, "words_learned", profile.words_learned);
    cJSON_AddNumberToObject(user, "active_lessons", profile.active_lessons);
    cJSON_AddItemToObject(resp, "user", user);
    cJSON_AddBoolToObject(resp, "success", 1);

    send_json_response(conn, 200, resp);

    cJSON_Delete(resp);

    DEBUG_PRINT_CARD_HANDLER("success username='%s'", profile.username);
    DEBUG_PRINT_CARD_HANDLER("EXIT");
}

/* --- адаптированный handle_static (использует g_abs_www если задан) --- */
void handle_static(http_connection_t *conn, http_request_t *req)
{
    /* base_dir: абсолютный если определён, иначе относительный "www" */
    const char *base_dir = (g_abs_www[0] != '\0') ? g_abs_www : "www";

    char url_clean[SIZ_PATH];
	const char *url_path = (req->path[0] != '\0') ? req->path : "/";

    strip_query_and_fragment(url_path, url_clean, sizeof(url_clean));

    char rel_path[SIZ_PATH];
    if (url_clean[0] == '/') {
        strncpy(rel_path, url_clean + 1, sizeof(rel_path) - 1);
        rel_path[sizeof(rel_path) - 1] = '\0';
    } else {
        strncpy(rel_path, url_clean, sizeof(rel_path) - 1);
        rel_path[sizeof(rel_path) - 1] = '\0';
    }

    /* Диагностика: где сервер смотрит */
    {
        char cwd[PATH_MAX] = "<unknown>";
        if (getcwd(cwd, sizeof(cwd))) {
            DEBUG_PRINT_CARD_HANDLER("handle_static: cwd='%s'", cwd);
        }
        DEBUG_PRINT_CARD_HANDLER("handle_static: using base_dir='%s' requested url='%s' -> rel='%s'",
                                base_dir, url_path, rel_path);
    }

    if (contains_dot_dot(rel_path)) {
        const char *msg = "400 Bad Request\n";
        http_send_response(conn, 400, "text/plain", msg, strlen(msg));
        return;
    }

    size_t rl = strlen(rel_path);
    while (rl > 0 && (rel_path[rl-1] == ' ' || rel_path[rl-1] == '\t')) {
        rel_path[--rl] = '\0';
    }

    char candidate[PATH_MAX];
    char file_path[PATH_MAX];
    bool found = false;
    const char *ext = strrchr(rel_path, '.');

    /* root -> pages/login/index.html */
    if (rel_path[0] == '\0') {
        snprintf(candidate, sizeof(candidate), "%s/pages/login/index.html", base_dir);
        DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
        if (file_exists(candidate)) {
            strncpy(file_path, candidate, sizeof(file_path)-1);
            file_path[sizeof(file_path)-1] = '\0';
            found = true;
        }
    } else {
        if (rl > 0 && rel_path[rl-1] != '/' ) {
            if (strcmp(rel_path, "login") == 0 ||
                strcmp(rel_path, "register") == 0 ||
                strcmp(rel_path, "dashboard") == 0) {
                char loc_header[128];
                if (strcmp(rel_path, "login") == 0) {
                    snprintf(loc_header, sizeof(loc_header), "Location: /login/");
                } else if (strcmp(rel_path, "register") == 0) {
                    snprintf(loc_header, sizeof(loc_header), "Location: /register/");
                } else {
                    snprintf(loc_header, sizeof(loc_header), "Location: /dashboard/");
                }
                const char *hdrs2[] = { loc_header, "Cache-Control: no-cache, must-revalidate" };
                const char *body = "<html><body>Redirect</body></html>";
                my_send_response_with_headers(conn, 301, "text/html", body, strlen(body), hdrs2, 2);
                return;
            }
        }

        if (strncmp(rel_path, "assets/", 7) == 0 || strncmp(rel_path, "static/", 7) == 0) {
            if (join_path(base_dir, rel_path, candidate, sizeof(candidate))) {
                DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
                if (file_exists(candidate)) {
                    strncpy(file_path, candidate, sizeof(file_path)-1);
                    file_path[sizeof(file_path)-1] = '\0';
                    found = true;
                }
            }
        }
    }

    if (!found && ext && strcmp(ext, ".css") == 0) {
        if (strncmp(rel_path, "assets/", 7) != 0) {
            const char *base = strrchr(rel_path, '/');
            base = base ? base + 1 : rel_path;
            snprintf(candidate, sizeof(candidate), "%s/assets/css/%s", base_dir, base);
            DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
            if (file_exists(candidate)) {
                strncpy(file_path, candidate, sizeof(file_path)-1);
                file_path[sizeof(file_path)-1] = '\0';
                found = true;
            }
        }
    } else if (!found && ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".mjs") == 0)) {
        if (strncmp(rel_path, "assets/", 7) != 0) {
            const char *base = strrchr(rel_path, '/');
            base = base ? base + 1 : rel_path;
            snprintf(candidate, sizeof(candidate), "%s/assets/js/%s", base_dir, base);
            DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
            if (file_exists(candidate)) {
                strncpy(file_path, candidate, sizeof(file_path)-1);
                file_path[sizeof(file_path)-1] = '\0';
                found = true;
            }
        }
    }

    if (!found && !last_segment_has_extension(rel_path)) {
        char tmp[PATH_MAX];
        char no_slash[SIZ_PATH];
        strncpy(no_slash, rel_path, sizeof(no_slash)-1);
        no_slash[sizeof(no_slash)-1] = '\0';
        size_t n = strlen(no_slash);
        if (n > 0 && no_slash[n-1] == '/') no_slash[n-1] = '\0';

        snprintf(tmp, sizeof(tmp), "pages/%s/index.html", no_slash);
        if (join_path(base_dir, tmp, candidate, sizeof(candidate))) {
            DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
            if (file_exists(candidate)) {
                strncpy(file_path, candidate, sizeof(file_path)-1);
                file_path[sizeof(file_path)-1] = '\0';
                found = true;
            }
        }

        if (!found) {
            snprintf(tmp, sizeof(tmp), "pages/%s.html", no_slash);
            if (join_path(base_dir, tmp, candidate, sizeof(candidate))) {
                DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
                if (file_exists(candidate)) {
                    strncpy(file_path, candidate, sizeof(file_path)-1);
                    file_path[sizeof(file_path)-1] = '\0';
                    found = true;
                }
            }
        }
    }

    if (!found) {
        if (join_path(base_dir, rel_path, candidate, sizeof(candidate))) {
            DEBUG_PRINT_CARD_HANDLER("trying candidate: %s", candidate);
            if (file_exists(candidate)) {
                strncpy(file_path, candidate, sizeof(file_path)-1);
                file_path[sizeof(file_path)-1] = '\0';
                found = true;
            } else {
                DEBUG_PRINT_CARD_HANDLER("candidate not found: %s", candidate);
            }
        } else {
            DEBUG_PRINT_CARD_HANDLER("join_path failed for base_dir='%s', rel='%s'", base_dir, rel_path);
        }
    }

    DEBUG_PRINT_CARD_HANDLER("final file_path='%s' (found=%d)", file_path, found);

    if (!found) {
        const char *notfound = "404 Not Found\n";
        http_send_response(conn, 404, "text/plain", notfound, strlen(notfound));
        return;
    }

    size_t size = 0;
    char *body = read_file_to_buffer(file_path, &size);
    if (!body) {
        const char *err = "500 Internal Server Error\n";
        http_send_response(conn, 500, "text/plain", err, strlen(err));
        return;
    }

    const char *mime = get_mime_type(file_path);
    if (!mime) mime = "application/octet-stream";

    const char *cache_hdr = NULL;
    if (strncmp(file_path, base_dir, strlen(base_dir)) == 0) {
        const char *sub = file_path + strlen(base_dir) + 1;
        if (strncmp(sub, "assets/", 7) == 0 || strstr(file_path, "/assets/")) {
            cache_hdr = "Cache-Control: public, max-age=3600";
        }
    }
    if (strcmp(mime, "text/html") == 0) cache_hdr = "Cache-Control: no-cache, must-revalidate";
    if (!cache_hdr && (strcmp(mime, "text/css") == 0 || strcmp(mime, "application/javascript") == 0))
        cache_hdr = "Cache-Control: public, max-age=3600";

    const char *extra1 = "X-Content-Type-Options: nosniff";
    const char *extra2 = "Referrer-Policy: no-referrer-when-downgrade";

    const char *hdrs[4];
    size_t hdr_count = 0;
    if (cache_hdr) hdrs[hdr_count++] = cache_hdr;
    hdrs[hdr_count++] = extra1;
    hdrs[hdr_count++] = extra2;

    if (hdr_count > 0)
        my_send_response_with_headers(conn, 200, mime, body, size, hdrs, hdr_count);
    else
        http_send_response(conn, 200, mime, body, size);

    free(body);
}
