#include "internal_api/card_api.h"

#include "db/db.h"

int card_api_list_words(int user_id, Word **out_words, size_t *out_count)
{
    if (user_id <= 0 || !out_words || !out_count) {
        return CARD_API_ERR_INVALID_ARGUMENT;
    }

    *out_words = NULL;
    *out_count = 0;

    if (db_get_all_words(out_words, out_count, user_id) != 0) {
        return CARD_API_ERR_SERVER;
    }

    return CARD_API_OK;
}

int card_api_add_word(const char *word, const char *transcription,
                      const char *translation, const char *example, int user_id)
{
    if (!word || !transcription || !translation || !example || user_id <= 0) {
        return CARD_API_ERR_INVALID_ARGUMENT;
    }

    if (db_add_word(word, transcription, translation, example, user_id) != 0) {
        return CARD_API_ERR_SERVER;
    }

    return CARD_API_OK;
}

void card_api_free_words(Word *words, size_t count)
{
    db_free_word_list(words, count);
}

int card_api_create(const card_api_create_input_t *input, int *out_card_id)
{
    db_word_create_input_t db_input;

    if (!input || !input->word || !input->transcription ||
        !input->translation || !input->example_1 || input->user_id <= 0) {
        return CARD_API_ERR_INVALID_ARGUMENT;
    }

    db_input.user_id = input->user_id;
    db_input.word = input->word;
    db_input.transcription = input->transcription;
    db_input.translation = input->translation;
    db_input.example_1 = input->example_1;
    db_input.example_2 = input->example_2;

    switch (db_create_word(&db_input, out_card_id)) {
    case DB_OK:
        return CARD_API_OK;
    case DB_ERR_INVALID_ARGUMENT:
        return CARD_API_ERR_INVALID_ARGUMENT;
    case DB_ERR_CONFLICT:
        return CARD_API_ERR_CONFLICT;
    default:
        return CARD_API_ERR_SERVER;
    }
}

int card_api_exists(const card_api_exists_query_t *query, int *out_exists)
{
    if (!query || !out_exists || !query->word || query->user_id <= 0) {
        return CARD_API_ERR_INVALID_ARGUMENT;
    }

    *out_exists = db_word_exists(query->word, query->user_id) ? 1 : 0;
    return CARD_API_OK;
}
