#include <string.h>
#include <stdlib.h>
#include "libs/cJSON.h"
#include "utils/tokenizer.h"
#include "db/db.h"


/**
 * Обрабатывает HTTP-запрос на добавление текста:
 * 1. Парсит JSON из тела (title, text, user_id)
 * 2. Вставляет запись в texts, получает text_id
 * 3. Токенизует текст в уникальные слова
 * 4. Для каждого слова:
 *    a) проверяет через db_word_exists(), есть ли уже в words
 *    b) если нет – вставляет в words и получает новый word_id
 *    c) связывает word_id и text_id в text_words (ON CONFLICT DO NOTHING)
 */
/*
static void handle_add_text(struct mg_connection *c, struct mg_http_message *hm) {
    // 1. Подключение к БД
    PGconn *conn = db_connect();
    if (PQstatus(conn) != CONNECTION_OK) {
        mg_http_reply(c, 500, "", "DB connection error\n");
        db_disconnect(conn);
        return;
    }

    // 2. Разбор JSON из тела запроса
    // Ожидается формат: {"title": "...", "text": "...", "user_id": <id>}
    cJSON *json = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
    if (!json) {
        mg_http_reply(c, 400, "", "Invalid JSON\n");
        db_disconnect(conn);
        return;
    }

    cJSON *title_item   = cJSON_GetObjectItem(json, "title");
    cJSON *text_item    = cJSON_GetObjectItem(json, "text");
    cJSON *user_id_item = cJSON_GetObjectItem(json, "user_id");
    if (!cJSON_IsString(title_item) || !cJSON_IsString(text_item) || !cJSON_IsNumber(user_id_item)) {
        mg_http_reply(c, 400, "", "Missing or invalid fields\n");
        cJSON_Delete(json);
        db_disconnect(conn);
        return;
    }

    const char *title   = title_item->valuestring;
    const char *text    = text_item->valuestring;
    int         user_id = user_id_item->valueint;

    // 3. Вставка текста в таблицу texts и получение text_id
    // Экранируем title и text для SQL
    char *esc_title = PQescapeLiteral(conn, title, strlen(title));
    char *esc_text  = PQescapeLiteral(conn, text, strlen(text));
    if (!esc_title || !esc_text) {
        mg_http_reply(c, 500, "", "Escape failed\n");
        cJSON_Delete(json);
        if (esc_title) PQfreemem(esc_title);
        if (esc_text)  PQfreemem(esc_text);
        db_disconnect(conn);
        return;
    }

    char insert_text_query[16384];
    snprintf(insert_text_query, sizeof(insert_text_query),
             "INSERT INTO texts (user_id, title, content) VALUES (%d, %s, %s) RETURNING id;",
             user_id, esc_title, esc_text);

    PGresult *res_text = PQexec(conn, insert_text_query);
    if (PQresultStatus(res_text) != PGRES_TUPLES_OK) {
        mg_http_reply(c, 500, "", "Failed to insert text\n");
        PQclear(res_text);
        PQfreemem(esc_title);
        PQfreemem(esc_text);
        cJSON_Delete(json);
        db_disconnect(conn);
        return;
    }

    int text_id = atoi(PQgetvalue(res_text, 0, 0));
    PQclear(res_text);
    PQfreemem(esc_title);
    PQfreemem(esc_text);

    // 4. Токенизация текста на уникальные слова
    int   word_count = 0;
    char **words     = extract_unique_words(text, &word_count);

    // 5. Для каждого слова: проверить существование, вставить (если надо) и связать
    for (int i = 0; i < word_count; ++i) {
        const char *word = words[i];

        // 5.1 Проверяем, существует ли слово у пользователя
        int existing_word_id = db_word_exists(word, user_id);

        // 5.2 Если слово не существует, вставляем его в таблицу words
        if (existing_word_id == 0) {
            // Экранируем слово для SQL
            char *esc_word = PQescapeLiteral(conn, word, strlen(word));
            if (esc_word) {
                char insert_word_query[2048];
                snprintf(insert_word_query, sizeof(insert_word_query),
                         "INSERT INTO words (user_id, word) VALUES (%d, %s) RETURNING id;",
                         user_id, esc_word);

                PGresult *res_word = PQexec(conn, insert_word_query);
                if (PQresultStatus(res_word) == PGRES_TUPLES_OK) {
                    existing_word_id = atoi(PQgetvalue(res_word, 0, 0));
                }
                PQclear(res_word);
                PQfreemem(esc_word);
            }
        }

        // 5.3 Если получили корректный word_id, связываем с текстом
        if (existing_word_id > 0) {
            char link_query[1024];
            snprintf(link_query, sizeof(link_query),
                     "INSERT INTO text_words (text_id, word_id) VALUES (%d, %d) ON CONFLICT DO NOTHING;",
                     text_id, existing_word_id);

            PGresult *res_link = PQexec(conn, link_query);
            PQclear(res_link);
        }
    }

    // 6. Освобождение ресурсов
    free_word_list(words, word_count);
    cJSON_Delete(json);
    db_disconnect(conn);

    // 7. Отправка ответа клиенту
    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"text_id\": %d}", text_id);
}
*/

