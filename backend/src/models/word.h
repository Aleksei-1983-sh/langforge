#ifndef WORD_H
#define WORD_H

#include <stddef.h>

typedef struct {
    int id;
    int user_id;
    char *word;
    char *transcription;
    char *translation;
    char *example_1;
    char *example_2;
    /*
     * Backward-compatible alias for legacy code paths.
     * Обычно указывает на example_1 и отдельно не освобождается.
     */
    char *example;
} Word;

#endif // WORD_H
