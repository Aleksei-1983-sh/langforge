#ifndef GENERATE_SERVICE_H
#define GENERATE_SERVICE_H

enum {
    GENERATE_SERVICE_EXAMPLE_COUNT = 2
};

typedef struct {
    const char *word;
    int user_id;
    int persist_if_authenticated;
} generate_service_request_t;

typedef struct {
    char *word;
    char *translation;
    char *transcription;
    char *examples[GENERATE_SERVICE_EXAMPLE_COUNT];
    int owner_user_id;
    int was_persisted;
} generate_service_card_t;

enum {
    GENERATE_SERVICE_OK = 0,
    GENERATE_SERVICE_ERR_INVALID_ARGUMENT = -1,
    GENERATE_SERVICE_ERR_INVALID_WORD = -2,
    GENERATE_SERVICE_ERR_UNAUTHORIZED = -3,
    GENERATE_SERVICE_ERR_UPSTREAM = -4,
    GENERATE_SERVICE_ERR_PERSISTENCE = -5,
    GENERATE_SERVICE_ERR_SERVER = -6,
    GENERATE_SERVICE_ERR_NOT_IMPLEMENTED = -7
};

/*
 * Главный use case:
 * - принимает уже разобранный input без HTTP-деталей;
 * - генерирует карточку;
 * - при необходимости сохраняет её для авторизованного пользователя.
 */
int generate_service_generate(const generate_service_request_t *request,
                              generate_service_card_t *out_card);

/*
 * DTO boundary:
 * - наружу generate_service возвращает generate_service_card_t;
 * - внутрь persistence-слоя service сам маппит его в card_service_create_input_t.
 */

/*
 * Освобождает heap-поля результата, полученного из generate_service_generate().
 */
void generate_service_free_card(generate_service_card_t *card);

#endif
