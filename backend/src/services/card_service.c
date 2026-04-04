#include "services/card_service.h"

#include "db/db.h"

#include <stdlib.h>

int card_service_list(int user_id, Word **out_words, size_t *out_count)
{
    if (!out_words || !out_count) return CARD_SERVICE_ERR_SERVER;

    *out_words = NULL;
    *out_count = 0;

    if (db_get_all_words(out_words, out_count, user_id) != 0) {
        return CARD_SERVICE_ERR_SERVER;
    }

    return CARD_SERVICE_OK;
}

int card_service_add(const char *word, const char *transcription,
                     const char *translation, const char *example,
                     int user_id)
{
    if (!word || !transcription || !translation || !example) {
        return CARD_SERVICE_ERR_SERVER;
    }

    if (db_add_word(word, transcription, translation, example, user_id) != 0) {
        return CARD_SERVICE_ERR_SERVER;
    }

    return CARD_SERVICE_OK;
}

void card_service_free_words(Word *words, size_t count)
{
    if (!words) return;

    for (size_t i = 0; i < count; ++i) {
        free(words[i].word);
        free(words[i].transcription);
        free(words[i].translation);
        free(words[i].example);
    }

    free(words);
}
