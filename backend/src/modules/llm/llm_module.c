#include "internal_api/llm_api.h"

#include "ollama/ollama.h"

#include <stdlib.h>
#include <string.h>

static char *llm_strdup(const char *value)
{
    if (!value) {
        return NULL;
    }

    size_t len = strlen(value) + 1;
    char *copy = malloc(len);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len);
    return copy;
}

void llm_api_free_word_card(llm_api_word_card_t *card)
{
    int i;

    if (!card) {
        return;
    }

    free(card->word);
    free(card->translation);
    free(card->transcription);

    for (i = 0; i < LLM_API_EXAMPLE_COUNT; ++i) {
        free(card->examples[i]);
        card->examples[i] = NULL;
    }

    card->word = NULL;
    card->translation = NULL;
    card->transcription = NULL;
}

int llm_api_generate_word_card(const char *word, llm_api_word_card_t *out_card)
{
    word_card_t *generated;
    int i;

    if (!word || !out_card || word[0] == '\0') {
        return LLM_API_ERR_INVALID_ARGUMENT;
    }

    memset(out_card, 0, sizeof(*out_card));

    generated = generate_word_card(word);
    if (!generated) {
        return LLM_API_ERR_UPSTREAM;
    }

    out_card->word = llm_strdup(generated->word ? generated->word : "");
    out_card->translation = llm_strdup(generated->translation ? generated->translation : "");
    out_card->transcription = llm_strdup(generated->transcription ? generated->transcription : "");

    for (i = 0; i < LLM_API_EXAMPLE_COUNT; ++i) {
        out_card->examples[i] = llm_strdup(generated->examples[i] ? generated->examples[i] : "");
    }

    free_word_card(generated);

    if (!out_card->word || !out_card->translation || !out_card->transcription ||
        !out_card->examples[0] || !out_card->examples[1]) {
        llm_api_free_word_card(out_card);
        return LLM_API_ERR_SERVER;
    }

    return LLM_API_OK;
}
