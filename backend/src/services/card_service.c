#include "services/card_service.h"

#include "internal_api/card_api.h"

#include <stdlib.h>
#include <string.h>

int card_service_list(int user_id, Word **out_words, size_t *out_count)
{
    if (!out_words || !out_count) return CARD_SERVICE_ERR_SERVER;

    *out_words = NULL;
    *out_count = 0;

    if (card_api_list_words(user_id, out_words, out_count) != CARD_API_OK) {
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

    if (card_api_add_word(word, transcription, translation, example, user_id) != CARD_API_OK) {
        return CARD_SERVICE_ERR_SERVER;
    }

    return CARD_SERVICE_OK;
}

void card_service_free_words(Word *words, size_t count)
{
    card_api_free_words(words, count);
}

int card_service_create(const card_service_create_input_t *input,
                        int *out_card_id)
{
    if (!input) return CARD_SERVICE_ERR_INVALID_ARGUMENT;

    if (!input->word || !input->transcription ||
        !input->translation || !input->example) {
        return CARD_SERVICE_ERR_INVALID_ARGUMENT;
    }

    card_api_create_input_t create_input;
    create_input.user_id = input->user_id;
    create_input.word = input->word;
    create_input.transcription = input->transcription;
    create_input.translation = input->translation;
    create_input.example_1 = input->example;
    create_input.example_2 = NULL;

    switch (card_api_create(&create_input, out_card_id)) {
    case CARD_API_OK:
        return CARD_SERVICE_OK;
    case CARD_API_ERR_INVALID_ARGUMENT:
        return CARD_SERVICE_ERR_INVALID_ARGUMENT;
    case CARD_API_ERR_CONFLICT:
        return CARD_SERVICE_ERR_CONFLICT;
    default:
        return CARD_SERVICE_ERR_SERVER;
    }
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

    card_api_exists_query_t exists_query;
    exists_query.user_id = query->user_id;
    exists_query.word = query->word;

    switch (card_api_exists(&exists_query, out_exists)) {
    case CARD_API_OK:
        return CARD_SERVICE_OK;
    case CARD_API_ERR_INVALID_ARGUMENT:
        return CARD_SERVICE_ERR_INVALID_ARGUMENT;
    default:
        return CARD_SERVICE_ERR_SERVER;
    }
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
