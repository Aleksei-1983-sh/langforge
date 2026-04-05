#include "services/generate_service.h"

#include "internal_api/llm_api.h"
#include "services/card_service.h"

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

    llm_api_word_card_t generated;
    memset(&generated, 0, sizeof(generated));

    int llm_rc = llm_api_generate_word_card(request->word, &generated);
    if (llm_rc == LLM_API_ERR_INVALID_ARGUMENT) {
        return GENERATE_SERVICE_ERR_INVALID_WORD;
    }
    if (llm_rc != LLM_API_OK) {
        return GENERATE_SERVICE_ERR_UPSTREAM;
    }

    out_card->word = generated.word;
    out_card->translation = generated.translation;
    out_card->transcription = generated.transcription;
    out_card->examples[0] = generated.examples[0];
    out_card->examples[1] = generated.examples[1];
    out_card->owner_user_id = request->user_id;
    out_card->was_persisted = 0;

    if (request->persist_if_authenticated) {
        if (request->user_id <= 0) {
            generate_service_free_card(out_card);
            return GENERATE_SERVICE_ERR_UNAUTHORIZED;
        }

        card_service_create_input_t create_input;
        create_input.user_id = request->user_id;
        create_input.word = out_card->word;
        create_input.transcription = out_card->transcription;
        create_input.translation = out_card->translation;
        create_input.example = out_card->examples[0] ? out_card->examples[0] : "";

        int card_rc = card_service_create(&create_input, NULL);
        if (card_rc != CARD_SERVICE_OK) {
            generate_service_free_card(out_card);
            return GENERATE_SERVICE_ERR_PERSISTENCE;
        }

        out_card->was_persisted = 1;
    }

    return GENERATE_SERVICE_OK;
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
