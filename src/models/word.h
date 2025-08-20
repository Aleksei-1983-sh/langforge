#ifndef WORD_H
#define WORD_H

#include <stddef.h>

typedef struct {
    char *word;
    char *transcription;
    char *translation;
    char *example;
} Word;

#endif // WORD_H
