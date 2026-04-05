
#ifndef DB_H
#define DB_H

#include <stddef.h>
#include "models/word.h"
#include <libpq-fe.h>

typedef struct {
    int user_id;
    const char *word;
    const char *transcription;
    const char *translation;
    const char *example_1;
    const char *example_2;
} db_word_create_input_t;

typedef struct {
    int user_id;
    int word_id;
} db_word_get_query_t;

typedef struct {
    int user_id;
    int word_id;
    const char *word;
    const char *transcription;
    const char *translation;
    const char *example_1;
    const char *example_2;
} db_word_update_input_t;

typedef struct {
    int user_id;
    int word_id;
} db_word_delete_input_t;

enum {
    DB_OK = 0,
    DB_ERR_SERVER = -1,
    DB_ERR_INVALID_ARGUMENT = -2,
    DB_ERR_NOT_FOUND = -3,
    DB_ERR_CONFLICT = -4,
    DB_ERR_NOT_IMPLEMENTED = -5
};

int init_db(const char *conninfo);
/* Connect to Postgres using connection string (libpq format). Returns 0 on success, -1 on error. */
void db_disconnect(void);
/* Disconnect if connected. */
int db_connect(const char *conninfo);
void db_init_conninfo(void);

int db_delete_user(int user_id);
int db_login_user(const char *username, const char *password_hash);
int db_register_user(const char *username, const char *email, const char *password_hash);

char *db_create_session(int user_id, int ttl_seconds);
int db_userid_by_session(const char *raw_token, int ttl_seconds);

int db_get_user_profile(int user_id, char *username_out, size_t uname_sz,
                        int *words_learned_out, int *active_lessons_out);
int db_word_exists(const char *word, int user_id);

/*
 * Legacy word API.
 */
int db_add_word(const char *word, const char *transcription,
                const char *translation, const char *example, int user_id);
int db_get_all_words(Word **out_list, size_t *out_count, int user_id);

/*
 * Target word/card API.
 */
int db_create_word(const db_word_create_input_t *input, int *out_word_id);
int db_get_word(const db_word_get_query_t *query, Word *out_word);
int db_update_word(const db_word_update_input_t *input);
int db_delete_word(const db_word_delete_input_t *input);
void db_free_word(Word *word);
void db_free_word_list(Word *words, size_t count);

#endif

