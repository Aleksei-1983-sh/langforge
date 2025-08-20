
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "http.h"
#include "cJSON.h"

#include "ollama.h"

/*
 * Макросы для отладки:
 * OL_DBG — печатает отладочные сообщения при DEBUG=1;
 * ERR — печатает ошибки всегда.
 */
#if defined(DEBUG) && DEBUG == 1
#define OL_DBG(fmt, ...) \
    fprintf(stderr, "\033[0;32m[DEBUG] \033[0m [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define OL_DBG(fmt, ...) ((void)0)
#endif

#define ERR(fmt, ...) \
    fprintf(stderr, "\033[0;31m[ERROR] \033[0m [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)


#define MODEL_NAME        "llama3:8b"
#define OLLAMA_HOST       "127.0.0.1"
#define OLLAMA_PORT       11434
#define API_PULL_PATH     "/api/pull"
#define API_GENERATE_PATH "/api/generate"


char *build_prompt_for_word(const char *word);
char *build_json_payload(const char *escaped_prompt);
void ensure_ollama_running(void);
void handle_response(const char *response);

/*
 * Экранирует кавычки, бэкслэши и управляющие символы, чтобы вставлять в JSON.
 * Возвращает malloc-строку или NULL.
 * Один выход через label end.
 */
char *escape_json(const char *input) {
    char *escaped = NULL;
    size_t len;
    char *buf = NULL;
    const char *src;
    char *dst;

    OL_DBG("enter: input=%p \"%s\"", input, input ? input : "");
    if (!input) {
        ERR("input is NULL");
        goto end;
    }

    len = strlen(input);
    /* worst-case: каждый символ превращается в 2 символа */
    buf = malloc(len * 2 + 1);
    if (!buf) {
        ERR("malloc failed for escape buffer");
        goto end;
    }

    dst = buf;
    for (src = input; *src; src++) {
        switch (*src) {
        case '\"':
            *dst++ = '\\';
            *dst++ = '\"';
            break;
        case '\\':
            *dst++ = '\\';
            *dst++ = '\\';
            break;
        case '\n':
            *dst++ = '\\';
            *dst++ = 'n';
            break;
        case '\r':
            *dst++ = '\\';
            *dst++ = 'r';
            break;
        case '\t':
            *dst++ = '\\';
            *dst++ = 't';
            break;
        default:
            *dst++ = *src;
            break;
        }
    }
    *dst = '\0';
    escaped = buf;
    buf = NULL; /* чтобы не освободить дважды */

end:
    if (buf) free(buf);
    OL_DBG("exit: escaped=%p \"%s\"", escaped, escaped ? escaped : "");
    return escaped;
}

/*
 * Проверяет, слушает ли кто-нибудь порт OLLAMA_PORT на localhost.
 * Возвращает 1, если успешно подключились, 0 иначе.
 * Один выход через label end.
 */
int is_ollama_running(void) {
    int ret = 0;
    int sockfd = -1;
    struct sockaddr_in serv_addr;

    OL_DBG("enter");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ERR("socket() failed");
        goto end;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(OLLAMA_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(OLLAMA_HOST);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
        OL_DBG("connect succeeded");
        ret = 1;
    } else {
        OL_DBG("connect failed or not listening");
        ret = 0;
    }

end:
    if (sockfd >= 0) close(sockfd);
    OL_DBG("exit: ret=%d", ret);
    return ret;
}

/*
 * Запускает «ollama serve» в фоне.
 * Void функция. Один выход через label end.
 */
void start_ollama_server(void) {
    pid_t pid;

    OL_DBG("enter");
    pid = fork();
    if (pid == 0) {
        /* дочерний процесс */
        OL_DBG("in child: about to execlp");
        execlp("ollama", "ollama", "serve", NULL);
        ERR("execlp failed to run `ollama serve`");
        _exit(1); /* в child сразу выходим */
    } else if (pid > 0) {
        /* родитель */
        OL_DBG("in parent: forked child pid=%d", pid);
        printf("Запускается `ollama serve` в фоне...\n");
        sleep(3); /* даём время подняться */
    } else {
        ERR("fork() failed");
    }

end:
    OL_DBG("exit");
    return;
}

/*
 * Находит начало тела HTTP-ответа (после "\r\n\r\n").
 * Если не найдено, возвращает NULL.
 * Один выход через label end.
 */
static char *find_http_body(const char *response) {
    char *body = NULL;
    char *p = NULL;

    OL_DBG("enter: response=%p", response);
    if (!response) {
        ERR("response is NULL");
        goto end;
    }
    p = strstr(response, "\r\n\r\n");
    if (p) {
        body = p + 4;
        OL_DBG("found header/body separator at %p", (void*)p);
    } else {
        OL_DBG("no header/body separator found");
    }

end:
    OL_DBG("exit: body=%p", body);
    return body;
}

/*
 * Декодирует chunked-encoded тело.
 * Если успешно, возвращает malloc-буфер с декодированными данными (null-terminated).
 * Если не удаётся или input NULL, возвращает NULL.
 * Один выход через label end.
 */
char *decode_chunked_body(const char *body) {
    char *out = NULL;
    char *result = NULL;
    char *dst = NULL;
    const char *p = NULL;
    unsigned long len = 0;
    char *endptr = NULL;
    size_t total = 0;

    OL_DBG("enter: body=%p", body);
    if (!body) {
        ERR("body is NULL");
        goto end;
    }

    // Первый проход: подсчёт общей длины
    p = body;
    while (1) {
        len = strtoul(p, &endptr, 16);
        if (endptr == p) {
            OL_DBG("no more chunk-size found, break");
            break;
        }
        OL_DBG("parsed chunk size: %lu", len);

        p = endptr;
        if (p[0] != '\r' || p[1] != '\n') {
            ERR("expected CRLF after chunk size, got: %.2s", p);
            goto end;
        }
        p += 2;

        total += len;
        OL_DBG("total so far: %zu", total);

        p += len;
        if (p[0] != '\r' || p[1] != '\n') {
            ERR("expected CRLF after chunk data, got: %.2s", p);
            goto end;
        }
        p += 2;

        if (len == 0) break;  // завершающий нулевой чанк
    }

    out = malloc(total + 1);
    if (!out) {
        ERR("malloc failed, size = %zu", total + 1);
        goto end;
    }

    // Второй проход: копирование данных
    p = body;
    dst = out;
    while (1) {
        len = strtoul(p, &endptr, 16);
        if (endptr == p) {
            OL_DBG("no more chunk-size in second pass, break");
            break;
        }
        OL_DBG("copy chunk size: %lu", len);

        p = endptr;
        if (p[0] != '\r' || p[1] != '\n') {
            ERR("expected CRLF after chunk size, got: %.2s", p);
            goto end;
        }
        p += 2;

        OL_DBG("copying chunk: %.20s", p); // Показать первые 20 символов чанка
        memcpy(dst, p, len);
        dst += len;
        p += len;

        if (p[0] != '\r' || p[1] != '\n') {
            ERR("expected CRLF after chunk data, got: %.2s", p);
            goto end;
        }
        p += 2;

        if (len == 0) {
            OL_DBG("final 0-length chunk reached");
            break;
        }
    }

    *dst = '\0';
    result = out;
    out = NULL;

end:
    if (out) free(out);
    OL_DBG("exit: result=%p", result);
    return result;
}

/*
 * Выполняет POST /api/pull для автозагрузки модели,
 * извлекает тело, декодирует chunked, печатает результат.
 * Void функция, один выход через label end.
 */
void pull_model_if_needed(void) {
    char post_data[256];
    const char *headers[] = {
        "Content-Type: application/json",
        NULL
    };
    char *response = NULL;
    int status;
    char *body = NULL;
    char *decoded = NULL;

    OL_DBG("enter");
    snprintf(post_data, sizeof(post_data), "{\"name\":\"%s\"}", MODEL_NAME);
    OL_DBG("post_data: %s", post_data);

    status = http_post(OLLAMA_HOST, OLLAMA_PORT, API_PULL_PATH, post_data, headers, &response);
    if (status < 0) {
        ERR("http_post for %s failed", API_PULL_PATH);
        goto end;
    }
    printf("HTTP status code: %d\n", status);
    if (!response) {
        ERR("empty response from http_post");
        goto end;
    }
    OL_DBG("raw HTTP response:\n%s", response);

    body = find_http_body(response);
    if (!body) {
        OL_DBG("treating entire response as body");
        body = response;
    }
    OL_DBG("body starts at %p: \"%.30s...\"", body, body);

    decoded = decode_chunked_body(body);
    if (decoded) {
        printf("Ответ /api/pull (декодированное тело):\n%s\n", decoded);
    } else {
        printf("Ответ /api/pull (сырое тело):\n%s\n", body);
    }

end:
    if (decoded) free(decoded);
    if (response) http_free_response(response);
    OL_DBG("exit");
    return;
}

/*
 * Разбирает JSON-ответ от Ollama и печатает «карту».
 * Void функция, один выход через label end.
 */
void print_card_from_response(const char *response_json) {
    cJSON *root = NULL;
    cJSON *response_field = NULL;
    cJSON *card_json = NULL;
    char *pretty = NULL;

    OL_DBG("enter: response_json=%p \"%.30s...\"", response_json, response_json ? response_json : "");
    if (!response_json) {
        ERR("response_json is NULL");
        goto end;
    }

    root = cJSON_Parse(response_json);
    if (!root) {
        ERR("cJSON_Parse failed");
        goto end;
    }
    OL_DBG("parsed root JSON");

    response_field = cJSON_GetObjectItem(root, "response");
    if (!cJSON_IsString(response_field)) {
        ERR("no 'response' field or not a string");
        goto end;
    }
    OL_DBG("found 'response' field: \"%s\"", response_field->valuestring);

    card_json = cJSON_Parse(response_field->valuestring);
    if (!card_json) {
        OL_DBG("nested JSON parse failed, printing raw string");
        printf("%s\n", response_field->valuestring);
        goto end;
    }
    OL_DBG("parsed nested JSON");

    pretty = cJSON_Print(card_json);
    if (pretty) {
        printf("%s\n", pretty);
        free(pretty);
    } else {
        ERR("cJSON_Print returned NULL");
    }

end:
    if (card_json) cJSON_Delete(card_json);
    if (root) cJSON_Delete(root);
    OL_DBG("exit");
    return;
}


char *build_prompt_for_word(const char *word) {
    char prompt[1024];
    snprintf(prompt, sizeof(prompt),
        "Please generate only the following JSON object for the English word: \"%s\" additional text, explanation, or commentary:\n"
        "{\n"
        "  \"word\": \"<the English word>\",\n"
        "  \"translation\": \"<Russian translation>\",\n"
        "  \"transcription\": \"<phonetic transcription>\",\n"
        "  \"example\": [\n"
        "    {\"text\": \"<simple example sentence 1>\"},\n"
        "    {\"text\": \"<simple example sentence 2>\"}\n"
        "  ]\n"
        "}\n"
        "Fill in all fields accurately and do not return anything beyond this JSON structure.",
        word
    );
    return strdup(prompt);
}


char *build_json_payload(const char *escaped_prompt) {
    char json_data[4096];
    snprintf(json_data, sizeof(json_data),
        "{ \"model\": \"%s\", \"prompt\": \"%s\", \"stream\": false }",
        MODEL_NAME, escaped_prompt);
    return strdup(json_data);
}


void ensure_ollama_running(void) {
    if (!is_ollama_running()) {
        OL_DBG("Ollama not running, starting server");
        start_ollama_server();
    } else {
        printf("Ollama уже запущен.\n");
    }
}


void handle_response(const char *response) {
    const char *body = find_http_body(response);
    char *decoded = NULL;

    if (!body) {
        body = response;
    }

    OL_DBG("body starts at %p: \"%.100s...\"", body, body);

    if (strstr(response, "Transfer-Encoding: chunked")) {
        decoded = decode_chunked_body(body);
    }

    if (decoded) {
        OL_DBG("decoded chunked body, first 30 chars: \"%.100s...\"", decoded);
        print_card_from_response(decoded);
        free(decoded);
    } else {
        OL_DBG("could not decode chunked, using raw body");
        print_card_from_response(body);
    }
}

word_card_t *parse_card_from_json(const char *json_response) {
    cJSON *root = cJSON_Parse(json_response);
    if (!root) return NULL;

    word_card_t *card = calloc(1, sizeof(word_card_t));
    if (!card) {
        cJSON_Delete(root);
        return NULL;
    }

    card->word = strdup(cJSON_GetObjectItem(root, "word")->valuestring);
    card->translation = strdup(cJSON_GetObjectItem(root, "translation")->valuestring);
    card->transcription = strdup(cJSON_GetObjectItem(root, "transcription")->valuestring);

    cJSON *examples = cJSON_GetObjectItem(root, "example");
    if (cJSON_IsArray(examples)) {
        for (int i = 0; i < NUMBER_OF_EXAMPLES && i < cJSON_GetArraySize(examples); ++i) {
            cJSON *ex = cJSON_GetArrayItem(examples, i);
            card->examples[i] = strdup(cJSON_GetObjectItem(ex, "text")->valuestring);
        }
    }

    cJSON_Delete(root);
    return card;
}

word_card_t *generate_word_card(const char *word) {
    char *prompt = NULL;
    char *escaped_prompt = NULL;
    char *json_data = NULL;
    char *response = NULL;
    char *body = NULL;  //не надо освобождать память
    char *decoded = NULL;
    word_card_t *card = NULL;
    const char *headers[] = {
        "Content-Type: application/json",
        NULL
    };

    // Ensure Ollama is running
    ensure_ollama_running();

    prompt = build_prompt_for_word(word);
    if (!prompt) goto cleanup;

    escaped_prompt = escape_json(prompt);
    if (!escaped_prompt) goto cleanup;

    json_data = build_json_payload(escaped_prompt);
    if (!json_data) goto cleanup;

    if (http_post(OLLAMA_HOST, OLLAMA_PORT, API_GENERATE_PATH, json_data, headers, &response) < 0 || !response)
        goto cleanup;

    body = find_http_body(response);
    if (!body) body = response;

    if (strstr(response, "Transfer-Encoding: chunked")) {
        decoded = decode_chunked_body(body);
    }

    const char *json_str = decoded ? decoded : body;

    // Ответ модели — это JSON с полем response, внутри которого — вложенный JSON.
    cJSON *full = cJSON_Parse(json_str);
    if (!full) goto cleanup;

    cJSON *inner = cJSON_GetObjectItem(full, "response");
    if (!inner || !cJSON_IsString(inner)) {
        cJSON_Delete(full);
        goto cleanup;
    }

    card = parse_card_from_json(inner->valuestring);
    cJSON_Delete(full);

cleanup:
    free(prompt);
    free(escaped_prompt);
    free(json_data);
    if (decoded) free(decoded);
    if (response) http_free_response(response);
    return card;
}

void print_word_card(const word_card_t *card) {
    if (!card) return;
    printf("Word: %s\n", card->word);
    printf("Translation: %s\n", card->translation);
    printf("Transcription: %s\n", card->transcription);
    printf("Examples:\n");
    for (int i = 0; i < NUMBER_OF_EXAMPLES; i++) {
        if (card->examples[i]) {
            printf("  - %s\n", card->examples[i]);
        }
    }
}

void free_word_carads(word_card_t *card)
{
	if (card == NULL)
		return;

	free(card->word);
	free(card->translation);
	free(card->transcription);
	for (int j = 0; j < NUMBER_OF_EXAMPLES; ++j)
		free(card->examples[j]);
	free(card);
	card = NULL;
}

/*
 * main: один выход через label end.
 */
/*
int main(void) {
    //const char *words[] = { "run", "eat", "speak", "write", "read", "sleep", "jump", "play" };
    const char *words[] = { "play" };
    size_t count = sizeof(words) / sizeof(words[0]);
    word_card_t *cards[count];

    for (size_t i = 0; i < count; ++i) {
        cards[i] = generate_word_card(words[i]);
        if (cards[i]) {
            print_word_card(cards[i]);
            printf("\n");
        } else {
            printf("Ошибка при генерации для слова: %s\n", words[i]);
        }
    }

    // очистка
    for (size_t i = 0; i < count; ++i) {
		free_word_carads(cards[i]);
    }

    return 0;
}*/
