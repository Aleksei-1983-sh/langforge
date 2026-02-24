
#ifndef DB_H
#define DB_H

#include <stddef.h>
#include "models/word.h"
#include <libpq-fe.h>

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
int db_userid_by_session(const char *raw_token);

int db_get_user_profile(int user_id, char *username_out, size_t uname_sz,
                        int *words_learned_out, int *active_lessons_out);
int db_word_exists(const char *word, int user_id);
int db_add_word(const char *word, const char *transcription,
                const char *translation, const char *example, int user_id);
int db_get_all_words(Word **out_list, size_t *out_count, int user_id);

#endif

