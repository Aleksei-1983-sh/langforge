#include "services/card_service.h"

#include "db/db.h"

#include <stdlib.h>
#include <string.h>

int card_service_list(int user_id, Word **out_words, size_t *out_count)
{
    if (!out_words || !out_count) return CARD_SERVICE_ERR_SERVER;

    *out_words = NULL;
    *out_count = 0;

    if (db_get_all_words(out_words, out_count, user_id) != 0) {
        return CARD_SERVICE_ERR_SERVER;
    }

    return CARD_SERVICE_OK;
}

int card_service_add(const char *word, const char *transcription,
                     const char *translation, const char *example,
                     int user_id)
{
    if (!word || !transcription || !translation || !example) {
        return CARD_SERVICE_ERR_SERVER;
    }

    if (db_add_word(word, transcription, translation, example, user_id) != 0) {
        return CARD_SERVICE_ERR_SERVER;
    }

    return CARD_SERVICE_OK;
}

void card_service_free_words(Word *words, size_t count)
{
    db_free_word_list(words, count);
}

int card_service_create(const card_service_create_input_t *input,
                        int *out_card_id)
{
    if (!input) return CARD_SERVICE_ERR_INVALID_ARGUMENT;

    if (!input->word || !input->transcription ||
        !input->translation || !input->example) {
        return CARD_SERVICE_ERR_INVALID_ARGUMENT;
    }

    if (db_add_word(input->word,
                    input->transcription,
                    input->translation,
                    input->example,
                    input->user_id) != 0) {
        return CARD_SERVICE_ERR_SERVER;
    }

    if (out_card_id) {
        /*
         * Текущий DB contract не возвращает id созданной карточки.
         * После расширения db.h здесь должен появиться реальный card_id.
         */
        *out_card_id = 0;
    }

    return CARD_SERVICE_OK;
}

int card_service_get(const card_service_get_query_t *query,
                     card_service_card_t *out_card)
{
    (void)query;
    (void)out_card;
    return CARD_SERVICE_ERR_NOT_IMPLEMENTED;
}

int card_service_list_cards(const card_service_list_query_t *query,
                            card_service_card_t **out_cards,
                            size_t *out_count)
{
    (void)query;
    (void)out_cards;
    (void)out_count;
    return CARD_SERVICE_ERR_NOT_IMPLEMENTED;
}

int card_service_update(const card_service_update_input_t *input)
{
    (void)input;
    return CARD_SERVICE_ERR_NOT_IMPLEMENTED;
}

int card_service_delete(const card_service_delete_input_t *input)
{
    (void)input;
    return CARD_SERVICE_ERR_NOT_IMPLEMENTED;
}

int card_service_exists(const card_service_exists_query_t *query,
                        int *out_exists)
{
    if (!query || !out_exists || !query->word) {
        return CARD_SERVICE_ERR_INVALID_ARGUMENT;
    }

    *out_exists = db_word_exists(query->word, query->user_id) ? 1 : 0;
    return CARD_SERVICE_OK;
}

void card_service_free_card(card_service_card_t *card)
{
    if (!card) return;

    free(card->word);
    free(card->transcription);
    free(card->translation);
    free(card->example);

    memset(card, 0, sizeof(*card));
}

void card_service_free_card_list(card_service_card_t *cards, size_t count)
{
    if (!cards) return;

    for (size_t i = 0; i < count; ++i) {
        card_service_free_card(&cards[i]);
    }

    free(cards);
}
