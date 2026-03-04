//src/libs/http.h
#ifndef OLLAMA_H
#define OLLAMA_H

#define NUMBER_OF_EXAMPLES 2

typedef struct {
    char *word;
    char *translation;
    char *transcription;
    char *examples[NUMBER_OF_EXAMPLES];
} word_card_t;

void ollama_init(void);
word_card_t *generate_word_card(const char *word);
void print_word_card(const word_card_t *card);
void free_word_card(word_card_t *card);

#endif // OLLAMA_H


/*
 * main: один выход через label end.
 */
/*
int main(void) {
    //const char *words[] = { "run", "eat", "speak", "write", "read", "sleep", "jump", "play" };
    const char *words[] = { "play" };
    size_t count = sizeof(words) / sizeof(words[0]);
    word_card_t *cards[count];

    for (size_t i = 0; i < count; ++i) {
        cards[i] = generate_word_card(words[i]);
        if (cards[i]) {
            print_word_card(cards[i]);
            printf("\n");
        } else {
            printf("Ошибка при генерации для слова: %s\n", words[i]);
        }
    }

    // очистка
    for (size_t i = 0; i < count; ++i) {
		free_word_carads(cards[i]);
    }

    return 0;
}*/
