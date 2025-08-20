//
#ifndef OLLAMA_H
#define OLLAMA_H

#define NUMBER_OF_EXAMPLES 2

typedef struct {
    char *word;
    char *translation;
    char *transcription;
    char *examples[NUMBER_OF_EXAMPLES];
} word_card_t;


word_card_t *generate_word_card(const char *word);
void print_word_card(const word_card_t *card);
void free_word_carads(word_card_t *card);

#endif // OLLAMA_H
