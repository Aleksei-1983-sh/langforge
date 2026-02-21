// src/handlers/generate_handler.c
#include <stdlib.h>
#include <string.h>

#include "libs/cJSON.h"          /* у тебя cJSON лежит в src/libs */
#include "ollama/ollama.h"       /* word_card_t, generate_word_card, free_word_cards */
#include "libs/http.h"                /* http_connection_t, http_request_t, send_json_response, http_get_header */
#include "utils/debug.h"         /* DEBUG_PRINT_CARD_HANDLER, ERROR_PRINT — адаптируй include если у тебя другое имя */

/* Локальные вспомогательные функции */

/* Преобразует word_card_t в cJSON объект. Caller должен удалить cJSON объект. */
static cJSON *word_card_to_json(const word_card_t *card)
{
    if (!card) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "word", card->word ? card->word : "");
    cJSON_AddStringToObject(root, "translation", card->translation ? card->translation : "");
    cJSON_AddStringToObject(root, "transcription", card->transcription ? card->transcription : "");

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < NUMBER_OF_EXAMPLES; ++i) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) continue;
        cJSON_AddStringToObject(obj, "text", card->examples[i] ? card->examples[i] : "");
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "example", arr);

    return root;
}

/* Сгенерировать карточки для массива слов и вернуть cJSON массив (caller должен cJSON_Delete). */
static cJSON *generate_cards_json_from_words(const char **words, size_t words_count)
{
    if (!words || words_count == 0) return NULL;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;

    for (size_t i = 0; i < words_count; ++i) {
        const char *w = words[i];
        if (!w) continue;

        DEBUG_PRINT_CARD_HANDLER("Generating card for word='%s'", w);

        word_card_t *card = generate_word_card(w);
        if (!card) {
            DEBUG_PRINT_CARD_HANDLER("generate_word_card failed for word='%s'", w);
            cJSON *err = cJSON_CreateObject();
            cJSON_AddStringToObject(err, "word", w);
            cJSON_AddStringToObject(err, "error", "generation_failed");
            cJSON_AddItemToArray(arr, err);
            continue;
        }

        cJSON *j = word_card_to_json(card);
        free_word_cards(card);

        if (!j) {
            DEBUG_PRINT_CARD_HANDLER("word_card_to_json failed for word='%s'", w);
            cJSON *err = cJSON_CreateObject();
            cJSON_AddStringToObject(err, "word", w);
            cJSON_AddStringToObject(err, "error", "serialize_failed");
            cJSON_AddItemToArray(arr, err);
            continue;
        }

        cJSON_AddItemToArray(arr, j);
    }

    return arr;
}

/* --- HTTP handler в стиле твоего проекта --- */
void handle_generate_card(http_connection_t *conn, http_request_t *req)
{
    DEBUG_PRINT_CARD_HANDLER("ENTER path='%s'", req->path ? req->path : "-");

    if (!conn || !req) {
        DEBUG_PRINT_CARD_HANDLER("bad args");
        return;
    }

    /* Логирование заголовков (если нужно) */
    log_request_headers(req);

    /* Читаем тело запроса. В проекте у тебя, судя по другим примерам,
       тело хранится в req->body как NUL-терминованная строка. */
    const char *body = req->body;
    if (!body || strlen(body) == 0) {
        DEBUG_PRINT_CARD_HANDLER("empty request body");
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "empty body");
        send_json_response(conn, 400, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT");
        return;
    }

    /* Parse JSON body */
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        DEBUG_PRINT_CARD_HANDLER("invalid json");
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "invalid json");
        send_json_response(conn, 400, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT");
        return;
    }

    cJSON *jwords = cJSON_GetObjectItem(root, "words");
    cJSON *jword = cJSON_GetObjectItem(root, "word");

    cJSON *cards_arr = NULL;

    if (cJSON_IsArray(jwords)) {
        size_t count = cJSON_GetArraySize(jwords);
        if (count == 0) {
            cJSON_Delete(root);
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", 0);
            cJSON_AddStringToObject(resp, "message", "empty words array");
            send_json_response(conn, 400, resp);
            cJSON_Delete(resp);
            DEBUG_PRINT_CARD_HANDLER("EXIT");
            return;
        }

        /* Собирать список слов (pointer-ы на строки внутри cJSON) */
        const char **words = calloc(count, sizeof(char*));
        if (!words) {
            cJSON_Delete(root);
            ERROR_PRINT("calloc failed");
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", 0);
            cJSON_AddStringToObject(resp, "message", "server error");
            send_json_response(conn, 500, resp);
            cJSON_Delete(resp);
            DEBUG_PRINT_CARD_HANDLER("EXIT");
            return;
        }

        size_t idx = 0;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, jwords) {
            if (cJSON_IsString(it) && it->valuestring) {
                words[idx++] = it->valuestring;
            }
        }

        cards_arr = generate_cards_json_from_words(words, idx);
        free(words);
    }
    else if (cJSON_IsString(jword) && jword->valuestring) {
        const char *single = jword->valuestring;
        const char *arr[1] = { single };
        cards_arr = generate_cards_json_from_words(arr, 1);
    }
    else {
        /* Неправильный формат тела */
        cJSON_Delete(root);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "need 'word' or 'words' field");
        send_json_response(conn, 400, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT");
        return;
    }

    cJSON_Delete(root);

    if (!cards_arr) {
        DEBUG_PRINT_CARD_HANDLER("generation returned NULL");
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", 0);
        cJSON_AddStringToObject(resp, "message", "generation failed");
        send_json_response(conn, 500, resp);
        cJSON_Delete(resp);
        DEBUG_PRINT_CARD_HANDLER("EXIT");
        return;
    }

    /* Формируем объект ответа: { success: 1, cards: [...] } */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", 1);
    cJSON_AddItemToObject(resp, "cards", cards_arr); /* передаём ownership cards_arr в resp */

    /* Отправляем и освобождаем */
    send_json_response(conn, 200, resp);
    cJSON_Delete(resp);

    DEBUG_PRINT_CARD_HANDLER("EXIT");
}
