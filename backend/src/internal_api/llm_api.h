#ifndef INTERNAL_API_LLM_API_H
#define INTERNAL_API_LLM_API_H

enum {
    LLM_API_EXAMPLE_COUNT = 2
};

typedef struct {
    char *word;
    char *translation;
    char *transcription;
    char *examples[LLM_API_EXAMPLE_COUNT];
} llm_api_word_card_t;

enum {
    LLM_API_OK = 0,
    LLM_API_ERR_INVALID_ARGUMENT = -1,
    LLM_API_ERR_UPSTREAM = -2,
    LLM_API_ERR_SERVER = -3
};

int llm_api_generate_word_card(const char *word, llm_api_word_card_t *out_card);
void llm_api_free_word_card(llm_api_word_card_t *card);

#endif
