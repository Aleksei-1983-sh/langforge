#include <stdlib.h>
#include <string.h>

#include "dbug/dbug.h"
#include "handlers/generate_handler.h"
#include "libs/cJSON.h"
#include "services/generate_service.h"


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

static void print_word_card_to_debug(const generate_service_card_t *card)
{
	if (!card) return;
	DEBUG_PRINT_GENERATE_HANDLER("Word: %s", card->word);
	DEBUG_PRINT_GENERATE_HANDLER("Translation: %s", card->translation);
	DEBUG_PRINT_GENERATE_HANDLER("Transcription: %s", card->transcription);
	DEBUG_PRINT_GENERATE_HANDLER("Examples:");

	for (int i = 0; i < GENERATE_SERVICE_EXAMPLE_COUNT; i++) {
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

	generate_service_request_t service_request;
	service_request.word = word;
	service_request.user_id = 0;
	service_request.persist_if_authenticated = 0;

	generate_service_card_t card;
	int service_rc = generate_service_generate(&service_request, &card);
	if (service_rc != GENERATE_SERVICE_OK) {
		free(word);

		http_send_response(conn, 502,
						   "application/json",
						   "{\"error\":\"llm request failed\"}",
						   strlen("{\"error\":\"llm request failed\"}"));
		return;
	}

	print_word_card_to_debug(&card);

	/* Безопасные значения (если LLM вернул NULL поля) */
	const char *c_word  = card.word          ? card.word          : "";
	const char *c_trans = card.transcription ? card.transcription : "";
	const char *c_tranl = card.translation   ? card.translation   : "";
	const char *ex1     = card.examples[0]   ? card.examples[0]   : "";
	const char *ex2     = card.examples[1]   ? card.examples[1]   : "";

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
		generate_service_free_card(&card);
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
	generate_service_free_card(&card);
	free(word);
}
