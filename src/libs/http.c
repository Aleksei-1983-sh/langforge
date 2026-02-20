/* http.c */
#define _GNU_SOURCE

#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>

#define MAX_HANDLER  16
#define MAX_CLIENTS  32
#define MAX_BUFFER   8192

// ================ ВНУТРЕННИЕ СТРУКТУРЫ ================

struct http_connection_s {
    int      fd;                // клиентский сокет
    char     buffer[MAX_BUFFER];
    size_t   buf_len;
    int      state;             // 0 = ждём запрос, 1 = отправили ответ
    http_request_t req;         // распарсенный запрос
};

typedef struct {
    char          method[8];
    char          path[256];
    http_handler_fn handler;
} handler_entry_t;

// Список зарегистрированных обработчиков
static handler_entry_t handlers[MAX_HANDLER];
static int handler_count = 0;

// Серверный слушающий сокет и клиенты
static int server_fd = -1;
static http_connection_t *clients[MAX_CLIENTS];

// ================ УТИЛИТЫ ================

static void close_connection(http_connection_t *conn) {
    if (!conn) return;
    close(conn->fd);
    if (conn->req.body) free(conn->req.body);
    free(conn);
}

static http_connection_t *alloc_connection(int fd) {
    http_connection_t *c = malloc(sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;
    c->buf_len = 0;
    c->state = 0;
    c->req.body = NULL;
    c->req.header_count = 0;
    return c;
}

// ================ HTTP-ПАРСЕР ================

// Очень упрощённый парсер: разбивает заголовки до пустой строки.
// Возвращает 0 при успехе, -1 при ошибке (неполный запрос или слишком большой).
int http_parse_request(const char *raw, size_t raw_len, http_request_t *req) {
    // защитный быстрый чек
    if (!raw || raw_len == 0 || !req) return -1;

    // создаём временную нуль-терминированную копию raw для безопасной работы со строковыми функциями
    char *tmp = malloc(raw_len + 1);
    if (!tmp) return -1;
    memcpy(tmp, raw, raw_len);
    tmp[raw_len] = '\0';

    // безопасная печать сырых данных (ограничиваем длину)
    DBG_REQUEST("\n\n%.*s\n\n", (int)raw_len, tmp);
	memset(req, 0, sizeof(*req));
	// 1) Найти конец первых \r\n
	const char *p = strstr(raw, "\r\n");
	if (!p || (size_t)(p - raw) > raw_len) return -1;
	// Пример: "POST /path HTTP/1.1"
	if (sscanf(raw, "%7s %255s", req->method, req->path) != 2) return -1;
	DBG("Parsed method='%s', path='%s'", req->method, req->path);

	// 2) Идём по строкам заголовков до \r\n\r\n
	const char *hdr_start = p + 2;
	const char *end_headers = strstr(hdr_start, "\r\n\r\n");
	if (!end_headers || (size_t)(end_headers - raw) > raw_len) return -1;
	const char *line = hdr_start;
	int hcount = 0;
	while (line < end_headers) {
		if (hcount >= MAX_HEADERS) {
			DBG("Header limit reached (%d). Further headers will be ignored.", MAX_HEADERS);
			break;
		}
		const char *line_end = strstr(line, "\r\n");
		if (!line_end || line_end > end_headers) break;
		size_t llen = line_end - line;
		const char *colon = memchr(line, ':', llen);
		if (colon) {
			size_t key_len = colon - line;
			size_t val_len = llen - key_len - 1;
			const char *vstart = colon + 1;
			while (val_len && (*vstart == ' ')) { vstart++; val_len--; }
			// trim trailing spaces in value
			while (val_len && (vstart[val_len-1] == ' ')) val_len--;
			if (key_len < MAX_HEADER_NAME && val_len < MAX_HEADER_VALUE) {
				size_t copy_key = key_len < (MAX_HEADER_NAME-1) ? key_len : (MAX_HEADER_NAME-1);
				size_t copy_val = val_len < (MAX_HEADER_VALUE-1) ? val_len : (MAX_HEADER_VALUE-1);
				memcpy(req->headers[hcount][0], line, copy_key);
				req->headers[hcount][0][copy_key] = '\0';
				memcpy(req->headers[hcount][1], vstart, copy_val);
				req->headers[hcount][1][copy_val] = '\0';
				DBG("Header[%d]: '%s' = '%s'", hcount, req->headers[hcount][0], req->headers[hcount][1]);
				hcount++;
			} else {
				DBG("Header too long: key_len=%zu val_len=%zu (skipping)", key_len, val_len);
			}
		}
		line = line_end + 2;
	}
	req->header_count = hcount;

	// 3) Если есть Content-Length, читать тело
	const char *cl = NULL;
	for (int i = 0; i < hcount; i++) {
		if (strcasecmp(req->headers[i][0], "Content-Length") == 0) {
			cl = req->headers[i][1];
			break;
		}
	}
	if (cl) {
		size_t body_len = strtoul(cl, NULL, 10);
		const char *body_start = end_headers + 4;
		if ((size_t)(raw + raw_len - body_start) < body_len) return -1; // неполный
		req->body = malloc(body_len + 1);
		if (!req->body) return -1;
		memcpy(req->body, body_start, body_len);
		req->body[body_len] = '\0';
		req->body_len = body_len;
		DBG("Parsed body_len=%zu, body='%.*s'", body_len, (int)body_len, req->body);
	}
	return 0;
}

const char *http_get_header(http_request_t *req, const char *key) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i][0], key) == 0) {
            return req->headers[i][1];
        }
    }
    return NULL;
}

// ================ HTTP СЕРВЕР ================

int http_register_handler(const char *method, const char *path, http_handler_fn handler) {
    if (handler_count >= MAX_HANDLER) return -1;
    strncpy(handlers[handler_count].method, method, 7);
    handlers[handler_count].method[7] = '\0';
    strncpy(handlers[handler_count].path, path, 255);
    handlers[handler_count].path[255] = '\0';
    handlers[handler_count].handler = handler;
    handler_count++;
    DBG("Registered handler %s %s", method, path);
    return 0;
}

int http_server_start(int port) {
    DBG("Запуск сервера на порту %d", port);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, 8) < 0) { perror("listen"); close(server_fd); return -1; }
    // Инициализируем список клиентов
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i] = NULL;
    DBG("Сервер запущен");
    return 0;
}

void http_server_poll() {
	fd_set readfds;
	int maxfd = server_fd;
	FD_ZERO(&readfds);
	FD_SET(server_fd, &readfds);
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (clients[i]) {
			FD_SET(clients[i]->fd, &readfds);
			if (clients[i]->fd > maxfd) maxfd = clients[i]->fd;
		}
	}
	struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
	int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
	if (ret < 0) { perror("select"); return; }
	// Новое соединение?
	if (FD_ISSET(server_fd, &readfds)) {
		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd >= 0) {
			// Найти свободное место
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (!clients[i]) {
					clients[i] = alloc_connection(client_fd);
					if (!clients[i]) close(client_fd);
					break;
				}
			}
		}
	}
	// Обработка клиентов
	for (int i = 0; i < MAX_CLIENTS; i++) {
		http_connection_t *c = clients[i];
		if (!c) continue;
		if (FD_ISSET(c->fd, &readfds)) {
			ssize_t n = recv(c->fd, c->buffer + c->buf_len, MAX_BUFFER - c->buf_len, 0);
			if (n <= 0) {
				close_connection(c);
				clients[i] = NULL;
			} else {
				c->buf_len += n;
				// Если полный запрос (ищем "\r\n\r\n")
				if (!c->state && memmem(c->buffer, c->buf_len, "\r\n\r\n", 4)) {
					// Распарсить
					if (http_parse_request(c->buffer, c->buf_len, &c->req) == 0) {
						// Найти обработчик
						http_handler_fn fn = NULL;
/*
						for (int h = 0; h < handler_count; h++) {
							if (strcasecmp(handlers[h].method, c->req.method) == 0 &&
								strcmp(handlers[h].path, c->req.path) == 0) {
								fn = handlers[h].handler;
								break;
							}
						}*/

					for (int h = 0; h < handler_count; h++) {
						if (strcasecmp(handlers[h].method, c->req.method) != 0) continue;

						const char *hp = handlers[h].path;
						if (strcmp(hp, c->req.path) == 0) {
							fn = handlers[h].handler;
							break;
						}

						// поддержка шаблона "/assets/*"
						size_t hp_len = strlen(hp);
						if (hp_len >= 2 && hp[hp_len-1] == '*' && hp[hp_len-2] == '/') {
							/* сравниваем префикс без последнего '*' */
							if (strncmp(hp, c->req.path, hp_len-1) == 0) {
								fn = handlers[h].handler;
								break;
							}
						}

						/* поддержка "префиксной" записи "/assets/" (если хотите) */
						if (hp_len > 0 && hp[hp_len-1] == '/') {
							if (strncmp(hp, c->req.path, hp_len) == 0) {
								fn = handlers[h].handler;
								break;
							}
						}
					}
						if (fn) {
							fn(c, &c->req);
						} else {
							// Нет обработчика → 404
							const char *msg = "Not Found";
							http_send_response(c, 404, "text/plain", msg, strlen(msg));
						}
					} else {
						// Невалидный запрос → 400
						const char *msg = "Bad Request";
						http_send_response(c, 400, "text/plain", msg, strlen(msg));
					}
					c->state = 1;
				}
			}
		}
		// Если после ответа соединение нужно закрыть
		if (c && c->state == 1) {
			close_connection(c);
			clients[i] = NULL;
		}
	}
}

void http_server_stop() {
    DBG("Останов сервера");
    if (server_fd >= 0) close(server_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) close_connection(clients[i]);
    }
    handler_count = 0;
}

/* Небольшая вспомогательная функция для текста статуса */
static const char *
reason_phrase(int status)
{
	switch (status) {
	case 200: return "OK";
	case 201: return "Created";
	case 204: return "No Content";
	case 301: return "Moved Permanently";
	case 302: return "Found";
	case 303: return "See Other";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 413: return "Payload Too Large";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable";
	default:  return "OK";
	}
}

int http_send_response(http_connection_t *conn, int status_code,
					   const char *content_type, const char *body,
					   size_t body_len) {
	DBG("http_send_response: status=%d, body_len=%zu", status_code, body_len);
	if (!conn) return -1;
	char header[512];

	const char *reason = reason_phrase(status_code);

	int hlen = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		status_code, reason, content_type, body_len);
	// Отправляем заголовок + тело
	if (send(conn->fd, header, hlen, 0) < 0) return -1;
	if (body_len > 0 && send(conn->fd, body, body_len, 0) < 0) return -1;
	return 0;
}

/* Реализация */

int my_send_response_with_headers(http_connection_t *conn,
                                  int status,
                                  const char *mime,
                                  const char *body,
                                  size_t len,
                                  const char **headers,
                                  size_t headers_count)
{
	char *hdr = NULL;
	size_t alloc = 0;
	int hdr_len = 0;
	const char *reason;
	size_t i;

	if (!conn) return -1;

	/* Защита от NULL */
	if (!mime) mime = "application/octet-stream";
	if (!body) body = "";
	if (headers_count && !headers) headers_count = 0;

	reason = reason_phrase(status);

	/* Оценим требуемый размер буфера для заголовков:
	   базовый размер + длина каждого дополнительного заголовка */
	alloc = 256; /* базовый запас */
	for (i = 0; i < headers_count; ++i) {
		if (headers[i]) alloc += strlen(headers[i]) + 2; /* + CRLF */
	}
	/* немного дополнительного резерва */
	alloc += 64;

	hdr = (char *)malloc(alloc);
	if (!hdr) return -1;

	/* Собираем начальные стандартные заголовки */
	hdr_len = snprintf(hdr, alloc,
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n",
		status, reason, mime, len);

	if (hdr_len < 0 || (size_t)hdr_len >= alloc) {
		/* странная ошибка snprintf или недостаточно места */
		free(hdr);
		return -1;
	}

	/* Добавляем пользовательские заголовки (если есть) */
	for (i = 0; i < headers_count; ++i) {
		const char *h = headers[i];
		if (!h) continue;
		size_t need = strlen(h) + 2; /* заголовок + "\r\n" */
		if ((size_t)hdr_len + need + 4 >= alloc) {
			/* расширяем буфер */
			size_t new_alloc = alloc * 2 + need + 128;
			char *p = (char *)realloc(hdr, new_alloc);
			if (!p) {
				free(hdr);
				return -1;
			}
			hdr = p;
			alloc = new_alloc;
		}
		int n = snprintf(hdr + hdr_len, alloc - hdr_len, "%s\r\n", h);
		if (n < 0) { free(hdr); return -1; }
		hdr_len += n;
	}

	/* Разделительная пустая строка перед телом */
	if ((size_t)hdr_len + 2 >= alloc) {
		char *p = (char *)realloc(hdr, alloc + 4);
		if (!p) { free(hdr); return -1; }
		hdr = p;
		alloc += 4;
	}
	hdr[hdr_len++] = '\r';
	hdr[hdr_len++] = '\n';

	/* Отправляем заголовок */
	ssize_t s = send(conn->fd, hdr, (size_t)hdr_len, 0);
	if (s < 0 || (size_t)s != (size_t)hdr_len) {
		free(hdr);
		return -1;
	}

	/* Отправляем тело (если есть длина > 0) */
	if (len > 0) {
		ssize_t t = send(conn->fd, body, len, 0);
		if (t < 0 || (size_t)t != len) {
			free(hdr);
			return -1;
		}
	}

	free(hdr);
	return 0;
}

// ================ HTTP КЛИЕНТ ====================

static ssize_t read_all(int sock, char **out_buf) {
    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;
    for (;;) {
        ssize_t n = recv(sock, buf + len, cap - len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return -1;
        } else if (n == 0) {
            break;
        } else {
            len += n;
            if (len + 1 >= cap) {
                cap *= 2;
                char *tmp = realloc(buf, cap);
                if (!tmp) { free(buf); return -1; }
                buf = tmp;
            }
        }
    }
    buf[len] = '\0';
    *out_buf = buf;
    return len;
}

static int http_request_internal(const char *host, int port,
                                 const char *path, const char *method,
                                 const char *body, const char *headers[],
                                 char **response_out) {
    int sock = -1, rc = -1;
    struct sockaddr_in serv;
    char *request = NULL;
    size_t req_len = 0;

    DBG("http_request_internal: %s %s", method, path);

    // 1. Создать сокет
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); goto cleanup; }
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &serv.sin_addr) <= 0) {
        perror("inet_pton"); goto cleanup;
    }
    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect"); goto cleanup;
    }

    // 2. Сформировать HTTP-запрос
    request = malloc(MAX_BUFFER);
    if (!request) goto cleanup;
    size_t offset = 0;
    offset += snprintf(request + offset, MAX_BUFFER - offset,
                       "%s %s HTTP/1.1\r\nHost: %s:%d\r\n",
                       method, path, host, port);
    if (headers) {
        for (int i = 0; headers[i]; i++) {
            offset += snprintf(request + offset,
                               MAX_BUFFER - offset, "%s\r\n", headers[i]);
        }
    }
    if (body) {
        offset += snprintf(request + offset, MAX_BUFFER - offset,
                           "Content-Length: %zu\r\n", strlen(body));
    }
    offset += snprintf(request + offset, MAX_BUFFER - offset, "Connection: close\r\n\r\n");
    if (body) {
        offset += snprintf(request + offset, MAX_BUFFER - offset, "%s", body);
    }
    req_len = offset;
    DBG("Сформирован запрос длиной %zu", req_len);

    // 3. Отправить
    if (send(sock, request, req_len, 0) < 0) { perror("send"); goto cleanup; }

    // 4. Прочитать весь ответ
    {
        char *resp = NULL;
        ssize_t rlen = read_all(sock, &resp);
        if (rlen < 0) {
            goto cleanup;
        }
        *response_out = resp;
        rc = 0; // ОК
    }

cleanup:
    if (request) free(request);
    if (sock >= 0) close(sock);
    return rc;
}

int http_get(const char *host, int port, const char *path,
             const char *headers[], char **response_out) {
    return http_request_internal(host, port, path, "GET", NULL, headers, response_out);
}

int http_post(const char *host, int port, const char *path,
              const char *body, const char *headers[], char **response_out) {
    return http_request_internal(host, port, path, "POST", body, headers, response_out);
}

void http_free_response(char *response) {
    if (response) free(response);
}
