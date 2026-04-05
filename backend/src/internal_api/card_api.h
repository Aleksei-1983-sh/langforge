#ifndef INTERNAL_API_CARD_API_H
#define INTERNAL_API_CARD_API_H

#include <stddef.h>

#include "models/word.h"

typedef struct {
    int user_id;
    const char *word;
    const char *transcription;
    const char *translation;
    const char *example_1;
    const char *example_2;
} card_api_create_input_t;

typedef struct {
    int user_id;
    const char *word;
} card_api_exists_query_t;

enum {
    CARD_API_OK = 0,
    CARD_API_ERR_SERVER = -1,
    CARD_API_ERR_INVALID_ARGUMENT = -2,
    CARD_API_ERR_NOT_FOUND = -3,
    CARD_API_ERR_CONFLICT = -4,
    CARD_API_ERR_NOT_IMPLEMENTED = -5
};

int card_api_list_words(int user_id, Word **out_words, size_t *out_count);
int card_api_add_word(const char *word, const char *transcription,
                      const char *translation, const char *example, int user_id);
void card_api_free_words(Word *words, size_t count);

int card_api_create(const card_api_create_input_t *input, int *out_card_id);
int card_api_exists(const card_api_exists_query_t *query, int *out_exists);

#endif
