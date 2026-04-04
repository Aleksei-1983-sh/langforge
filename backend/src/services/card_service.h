#ifndef CARD_SERVICE_H
#define CARD_SERVICE_H

#include <stddef.h>

#include "models/word.h"

typedef struct {
    int card_id;
    int user_id;
    char *word;
    char *transcription;
    char *translation;
    char *example;
} card_service_card_t;

/*
 * DTO граница для create/persist.
 * Сюда generate_service передаёт уже готовые данные карточки,
 * не зависящие от HTTP и не зависящие от ollama.h.
 */
typedef struct {
    int user_id;
    const char *word;
    const char *transcription;
    const char *translation;
    const char *example;
} card_service_create_input_t;

typedef struct {
    int user_id;
    int card_id;
} card_service_get_query_t;

typedef struct {
    int user_id;
    int card_id;
    const char *word;
    const char *transcription;
    const char *translation;
    const char *example;
} card_service_update_input_t;

typedef struct {
    int user_id;
    int card_id;
} card_service_delete_input_t;

typedef struct {
    int user_id;
    const char *word;
} card_service_exists_query_t;

typedef struct {
    int user_id;
    size_t limit;
    size_t offset;
    const char *search_term;
} card_service_list_query_t;

enum {
    CARD_SERVICE_OK = 0,
    CARD_SERVICE_ERR_SERVER = -1,
    CARD_SERVICE_ERR_INVALID_ARGUMENT = -2,
    CARD_SERVICE_ERR_NOT_FOUND = -3,
    CARD_SERVICE_ERR_CONFLICT = -4,
    CARD_SERVICE_ERR_UNAUTHORIZED = -5,
    CARD_SERVICE_ERR_NOT_IMPLEMENTED = -6
};

/*
 * Legacy API. Оставлен для текущего кода и постепенной миграции.
 */
int card_service_list(int user_id, Word **out_words, size_t *out_count);
int card_service_add(const char *word, const char *transcription,
                     const char *translation, const char *example,
                     int user_id);
void card_service_free_words(Word *words, size_t count);

/*
 * Целевой API card_service.
 */
int card_service_create(const card_service_create_input_t *input,
                        int *out_card_id);
int card_service_get(const card_service_get_query_t *query,
                     card_service_card_t *out_card);
int card_service_list_cards(const card_service_list_query_t *query,
                            card_service_card_t **out_cards,
                            size_t *out_count);
int card_service_update(const card_service_update_input_t *input);
int card_service_delete(const card_service_delete_input_t *input);
int card_service_exists(const card_service_exists_query_t *query,
                        int *out_exists);
void card_service_free_card(card_service_card_t *card);
void card_service_free_card_list(card_service_card_t *cards, size_t count);

#endif
