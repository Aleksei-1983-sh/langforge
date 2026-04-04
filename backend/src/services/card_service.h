#ifndef CARD_SERVICE_H
#define CARD_SERVICE_H

#include <stddef.h>

#include "models/word.h"

enum {
    CARD_SERVICE_OK = 0,
    CARD_SERVICE_ERR_SERVER = -1
};

int card_service_list(int user_id, Word **out_words, size_t *out_count);
int card_service_add(const char *word, const char *transcription,
                     const char *translation, const char *example,
                     int user_id);
void card_service_free_words(Word *words, size_t count);

#endif
