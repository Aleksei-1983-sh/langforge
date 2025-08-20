
#include "tokenizer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_WORDS 1024

// Удаляет пунктуацию и приводит к нижнему регистру
static void normalize_word(char *word) {
    for (char *p = word; *p; ++p) {
        if (ispunct((unsigned char)*p)) *p = '\0';
        else *p = tolower(*p);
    }
}

char **extract_unique_words(const char *text, int *count) {
    char **words = calloc(MAX_WORDS, sizeof(char *));
    int word_count = 0;
    char *copy = strdup(text);
    char *token = strtok(copy, " \t\r\n");

    while (token && word_count < MAX_WORDS) {
        normalize_word(token);
        if (strlen(token) == 0) {
            token = strtok(NULL, " \t\r\n");
            continue;
        }

        // Проверка на уникальность
        int is_unique = 1;
        for (int i = 0; i < word_count; ++i) {
            if (strcmp(words[i], token) == 0) {
                is_unique = 0;
                break;
            }
        }

        if (is_unique) {
            words[word_count++] = strdup(token);
        }

        token = strtok(NULL, " \t\r\n");
    }

    free(copy);
    *count = word_count;
    return words;
}

void free_word_list(char **words, int count) {
    for (int i = 0; i < count; ++i) {
        free(words[i]);
    }
    free(words);
}

