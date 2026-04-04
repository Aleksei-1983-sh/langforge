#include "services/generate_service.h"

#include <stdlib.h>
#include <string.h>

static void generate_service_reset_card(generate_service_card_t *card)
{
    if (!card) return;

    memset(card, 0, sizeof(*card));
}

int generate_service_generate(const generate_service_request_t *request,
                              generate_service_card_t *out_card)
{
    if (!request || !out_card) {
        return GENERATE_SERVICE_ERR_INVALID_ARGUMENT;
    }

    generate_service_reset_card(out_card);

    if (!request->word || request->word[0] == '\0') {
        return GENERATE_SERVICE_ERR_INVALID_WORD;
    }

    /*
     * TODO:
     * 1. Нормализовать и валидировать слово.
     * 2. Вызвать LLM adapter.
     * 3. Смапить ответ в generate_service_card_t.
     * 4. При request->persist_if_authenticated сохранить карточку в БД.
     */
    return GENERATE_SERVICE_ERR_NOT_IMPLEMENTED;
}

void generate_service_free_card(generate_service_card_t *card)
{
    int i;

    if (!card) return;

    free(card->word);
    free(card->translation);
    free(card->transcription);

    for (i = 0; i < GENERATE_SERVICE_EXAMPLE_COUNT; ++i) {
        free(card->examples[i]);
        card->examples[i] = NULL;
    }

    card->word = NULL;
    card->translation = NULL;
    card->transcription = NULL;
    card->owner_user_id = 0;
    card->was_persisted = 0;
}
