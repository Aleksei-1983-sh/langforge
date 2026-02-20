
/* http.h */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HEADERS 64
#define MAX_HEADER_NAME 128
#define MAX_HEADER_VALUE 1024

	// ===================== Отладка =====================
// При компиляции с -DDEBUG=1 активируется этот макрос:
#if defined(DEBUG) && DEBUG == 1
#define DBG(fmt, ...) \
  fprintf(stderr, "[DEBUG_HTTP][%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

#if defined(DEBUG_REQUEST) && DEBUG_REQUEST == 1
#define DBG_REQUEST(fmt, ...) \
  fprintf(stderr, "[DEBUG_HTTP][%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DBG_REQUEST(fmt, ...) ((void)0)
#endif

// ===================== HTTP КЛИЕНТ =====================
// Возвращает HTTP статус-код или -1 при ошибке.
// Выделяет *response_out через malloc; освободить через http_free_response().
int http_get(const char *host, int port, const char *path,
             const char *headers[], char **response_out);

int http_post(const char *host, int port, const char *path,
              const char *body, const char *headers[], char **response_out);

void http_free_response(char *response);

// ===================== HTTP СЕРВЕР =====================

typedef struct http_request_s {
    char method[8];       /* "GET", "POST", ... */
    char path[256];       /* URI без query, например "/api/cards" */
    char query[256];      /* содержимое после '?', без '?' */
    int header_count;     // число заголовков (max 16)
    char headers[MAX_HEADERS][2][MAX_HEADER_VALUE]; // [i][0]=ключ, [i][1]=значение
    char *body;           // тело запроса (malloc), длина в body_len
    size_t body_len;
} http_request_t;

typedef struct http_connection_s http_connection_t;

// Обработчик запроса: принимает соединение и распарсенный запрос.
// Должен вызывать http_send_response().
typedef void (*http_handler_fn)(http_connection_t *conn, http_request_t *req);

// Запускает сервер на порту; возвращает 0 при успехе, иначе -1.
int http_server_start(int port);

// Обрабатывать поступившие соединения (вызывать polling). Обычно в цикле.
void http_server_poll(void);

// Останавливает сервер, освобождает ресурсы.
void http_server_stop(void);

// Регистрация обработчика: 
// method: "GET" или "POST", path: строка пути (точное совпадение).
// Возвращает 0 при успехе, -1 при ошибке.
int http_register_handler(const char *method, const char *path,
                          http_handler_fn handler);

// Отправляет HTTP-ответ клиенту:
// status_code (200,404,...), content_type ("application/json"), тело + длину.
// Возвращает 0 при успехе, -1 при ошибке.
int http_send_response(http_connection_t *conn, int status_code,
                       const char *content_type, const char *body,
                       size_t body_len);

int my_send_response_with_headers(http_connection_t *conn,
                                  int status,
                                  const char *mime,
                                  const char *body,
                                  size_t len,
                                  const char **headers,
                                  size_t headers_count);

// Распарсить raw-буфер длины raw_len в структуру http_request_t.
// Возвращает 0 при успехе, -1 при ошибки.
// Выделяет req->body через malloc; при неудаче body= NULL.
int http_parse_request(const char *raw, size_t raw_len,
                       http_request_t *req);

// Вернуть значение заголовка по ключу (или NULL, если не найден).
const char *http_get_header(http_request_t *req, const char *key);

#ifdef __cplusplus
}
#endif

#endif // HTTP_H
