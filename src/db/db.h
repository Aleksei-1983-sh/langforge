
#ifndef DB_H
#define DB_H

#include <stddef.h>
#include "models/word.h"
#include <libpq-fe.h>
/*
 Схема базы данных (ER-диаграмма)
┌──────────┐          ┌─────────┐          ┌─────────┐           
|  users   |          |  texts  |          |  words  |
|----------|          |---------|          |---------|
| id    PK |<---+  +--| id   PK |      +-->| id   PK |
| username |    |     | user_id |      |   | user_id |
| email    |    |     | title   |      |   | word    |
| password |    |     | content |      |   | ...     |
| created  |    |     | created |      |   | ...     |
└──────────┘    |     └---------┘      |   └---------┘
                |                     /     
                +-----------------+--+   
                                  |       
                            ┌─────────────┐
                            | text_words  |
                            |-------------|
                            | text_id     |
                            | word_id     |
                            └─────────────┘
*/
int init_db(const char *conninfo);
/* Connect to Postgres using connection string (libpq format). Returns 0 on success, -1 on error. */
void db_disconnect(void);
/* Disconnect if connected. */
int db_connect(const char *conninfo);

int db_delete_user(int user_id);
int db_login_user(const char *username, const char *password_hash);
int db_register_user(const char *username, const char *email, const char *password_hash);
int db_word_exists(const char *word, int user_id);
int db_add_word(const char *word, const char *transcription,
                const char *translation, const char *example, int user_id);
int db_get_all_words(Word **out_list, size_t *out_count, int user_id);

#endif

