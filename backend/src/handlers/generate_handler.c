/* handlers/generate_handler.c
 *
 * POST /api/generate_card
 * Вход: JSON {"word":"..."}  (utf-8)
 * Вызывает локальный LLM через HTTP (http_post) и возвращает JSON-карточку клиенту.
 *
 * Стиль — практичный, K&R-like.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http.h"
#include "db.h"
#include "dbug.h"
#include "ollama.h"


/* Простая утилита: извлечь значение строки из простого JSON {"key":"value", ...}
 * Возвращает malloc'ed строку (strdup) или NULL.
 * Парсинг выполняется через cJSON.
 */
static char *extract_json_string(const char *json, const char *key)
{
    if (!json || !key)
        return NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) {
        cJSON_Delete(root);
        return NULL;
    }

    char *out = strdup(item->valuestring);

    cJSON_Delete(root);
    return out;
}

static void print_word_card_to_gebug(const word_card_t *card) {
	if (!card) return;
	DEBUG_PRINT_GENERATE_HANDLER("Word: %s", card->word);
	DEBUG_PRINT_GENERATE_HANDLER("Translation: %s", card->translation);
	DEBUG_PRINT_GENERATE_HANDLER("Transcription: %s", card->transcription);
	DEBUG_PRINT_GENERATE_HANDLER("Examples:");

	for (int i = 0; i < NUMBER_OF_EXAMPLES; i++) {
		if (card->examples[i]) {
			DEBUG_PRINT_GENERATE_HANDLER("  - %s", card->examples[i]);
		}
	}
}

/* Handler */
void handle_generate_card(http_connection_t *conn, http_request_t *req)
{
	if (!req || !req->body) {
		http_send_response(conn, 400,
						   "application/json",
						   "{\"error\":\"empty request body\"}",
						   strlen("{\"error\":\"empty request body\"}"));
		return;
	}

	/* Извлекаем слово из JSON {"word":"..."} */
	char *word = extract_json_string(req->body, "word");
	if (!word || strlen(word) == 0) {
		if (word) free(word);

		http_send_response(conn, 400,
						   "application/json",
						   "{\"error\":\"missing 'word'\"}",
						   strlen("{\"error\":\"missing 'word'\"}"));
		return;
	}

	DEBUG_PRINT_GENERATE_HANDLER("generate_card: word='%s'", word);

	/* Генерация карточки через LLM */
	word_card_t *card = generate_word_card(word);

	if (!card) {
		free(word);

		http_send_response(conn, 502,
						   "application/json",
						   "{\"error\":\"llm request failed\"}",
						   strlen("{\"error\":\"llm request failed\"}"));
		return;
	}

	print_word_card_to_gebug(card);

#if 0
	/* 4) опционально — сохраняем в БД (user_id = 0 — guest) */
	/* TODO: извлечь из сессии, если нужно */
	int user_id = 0; 
	int dbres = db_add_word(card->word,
						   card->translation,
						   card->transcription,
						   card->example,
						   user_id);

	if (dbres != 0) {
		// Ошибка при сохранении — логируем, но всё ещё можем вернуть карточку клиенту
		ERROR_PRINT("db_add_word failed for word='%s' code=%d", word, dbres);
	}
#endif

	/* Безопасные значения (если LLM вернул NULL поля) */
	const char *c_word  = card->word          ? card->word          : "";
	const char *c_trans = card->transcription ? card->transcription : "";
	const char *c_tranl = card->translation   ? card->translation   : "";
	const char *ex1     = card->examples[0]   ? card->examples[0]   : "";
	const char *ex2     = card->examples[1]   ? card->examples[1]   : "";

	/*
	 * Формируем JSON.
	 * Добавляем массив examples.
	 */
	const char *resp_fmt =
		"{"
		"\"word\":\"%s\","
		"\"transcription\":\"%s\","
		"\"translation\":\"%s\","
		"\"examples\":[\"%s\",\"%s\"]"
		"}";

	size_t resp_sz =
		strlen(resp_fmt) +
		strlen(c_word) +
		strlen(c_trans) +
		strlen(c_tranl) +
		strlen(ex1) +
		strlen(ex2) +
		64;

	char *out = malloc(resp_sz);
	if (!out) {
		free_word_card(card);
		free(word);

		http_send_response(conn, 500,
						   "application/json",
						   "{\"error\":\"internal\"}",
						   strlen("{\"error\":\"internal\"}"));
		return;
	}

	snprintf(out, resp_sz, resp_fmt,
			 c_word,
			 c_trans,
			 c_tranl,
			 ex1,
			 ex2);

	http_send_response(conn, 200,
					   "application/json",
					   out,
					   strlen(out));

	/* cleanup */
	free(out);
	free_word_card(card);
	free(word);
}
