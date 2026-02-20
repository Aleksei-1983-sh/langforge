
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>
#include "db.h"
#include "models/word.h"
#include "dbug/dbug.h"

/* Глобальный указатель на соединение (реиспользуется) */
static PGconn *db_conn = NULL;

/* Параметры по умолчанию; при необходимости вынеси в конфиг / env */
static const char *DEFAULT_CONNINFO = "host=localhost dbname=englearn user=enguser password=engpass";

/* Connect to Postgres using connection string (libpq format). Returns 0 on success, -1 on error. */
int db_connect(const char *conninfo) {
    if (db_conn) {
        DEBUG_PRINT_DB("existing connection found, disconnecting first");
        PQfinish(db_conn);
        db_conn = NULL;
    }
    DEBUG_PRINT_DB("connecting with conninfo: %s", conninfo ? conninfo : "(null)");
    db_conn = PQconnectdb(conninfo);
    if (!db_conn) {
        ERROR_PRINT("PQconnectdb returned NULL");
        return -1;
    }
    if (PQstatus(db_conn) != CONNECTION_OK) {
        ERROR_PRINT("connection failed: %s", PQerrorMessage(db_conn));
        PQfinish(db_conn);
        db_conn = NULL;
        return -1;
    }
    DEBUG_PRINT_DB("connected to database");
    return 0;
}

/* Disconnect if connected. */
void db_disconnect(void) {
    if (!db_conn) return;
    DEBUG_PRINT_DB("disconnecting from database");
    PQfinish(db_conn);
    db_conn = NULL;
}

/* Генерирует `len` случайных байт и кодирует в hex.
 * out должен быть >= (len*2 + 1) символов.
 * Возвращает 0 при успехе, -1 при ошибке.
 */
int generate_random_hex(unsigned char *out, size_t out_len, size_t bytes)
{
    if (!out || out_len < bytes*2 + 1) return -1;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    unsigned char buf[64];
    if (bytes > sizeof(buf)) { close(fd); return -1; }
    ssize_t r = read(fd, buf, bytes);
    close(fd);
    if (r != (ssize_t)bytes) return -1;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
        out[i*2]   = hex[(buf[i] >> 4) & 0xF];
        out[i*2+1] = hex[buf[i] & 0xF];
    }
    out[bytes*2] = '\0';
    return 0;
}

/* Удобный wrapper: генерирует token длиной 64 hex-символа (32 байта) */
int gen_session_token(char *out, size_t outsz)
{
    return generate_random_hex((unsigned char*)out, outsz, 32); /* 64 hex chars + NUL */
}
/* helper: вычисляет SHA256(input) и записывает hex в out_hex.
 * out_hex должен быть >=65 байт (64 hex + NUL).
 * Возвращает 0 при успехе, -1 при ошибке.
 */

int sha256_hex(const char *input, char *out_hex, size_t out_hex_len)
{
    if (!input || !out_hex || out_hex_len < 65) return -1;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (!SHA256((const unsigned char*)input, strlen(input), digest)) return -1;
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out_hex[i*2]   = hex[(digest[i] >> 4) & 0xF];
        out_hex[i*2+1] = hex[digest[i] & 0xF];
    }
    out_hex[SHA256_DIGEST_LENGTH*2] = '\0';
    return 0;
}

/* Возвращает malloc'ed raw token (T) при успехе; NULL при ошибке */
char *db_create_session(int user_id, int ttl_seconds)
{
    if (user_id <= 0) {
        ERROR_PRINT("db_create_session: invalid user_id=%d", user_id);
        return NULL;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_create_session: db_connect failed");
        return NULL;
    }

    /* 1) сгенерировать raw token (hex string, 64 chars) */
    char raw_token[65];
    if (gen_session_token(raw_token, sizeof(raw_token)) != 0) {
        ERROR_PRINT("db_create_session: gen_session_token failed");
        db_disconnect();
        return NULL;
    }

    /* 2) посчитать sha256(raw_token) -> token_hash (hex 64) */
    char token_hash[65];
    if (sha256_hex(raw_token, token_hash, sizeof(token_hash)) != 0) {
        ERROR_PRINT("db_create_session: sha256_hex failed");
        db_disconnect();
        return NULL;
    }

    /* 3) compute expires_at ISO string (UTC) */
    time_t now = time(NULL);
    time_t ex = now + ttl_seconds;
    struct tm gm;
    gmtime_r(&ex, &gm);
    char expires_iso[64];
    /* Используем ISO-like формат удобный для Postgres cast */
    strftime(expires_iso, sizeof(expires_iso), "%Y-%m-%d %H:%M:%S%z", &gm);

    const char *paramValues[3];
    paramValues[0] = token_hash; /* сохраняем в БД хеш */
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "%d", user_id);
    paramValues[1] = idbuf;
    paramValues[2] = expires_iso;

    Oid paramTypes[3] = {25, 23, 25}; /* token text, user_id int4, expires text (will be cast) */

    DEBUG_PRINT_DB("db_create_session: inserting session for user_id=%s expires='%s'", idbuf, expires_iso);

    PGresult *res = PQexecParams(db_conn,
        "INSERT INTO sessions (token, user_id, created_at, last_access, expires_at, user_agent, ip_addr) "
        "VALUES ($1, $2, now(), now(), $3::timestamptz, NULL, NULL) RETURNING token;",
        3, paramTypes, paramValues, NULL, NULL, 0);

    if (!res) {
        ERROR_PRINT("db_create_session: PQexecParams returned NULL");
        db_disconnect();
        return NULL;
    }

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1) {
        /* успех — возвращаем raw_token (malloc'ed) для передачи клиенту */
        char *ret = strdup(raw_token);
        PQclear(res);
        db_disconnect();
        DEBUG_PRINT_DB("db_create_session: created session (hash=%s) for user_id=%s", token_hash, idbuf);
        return ret;
    } else {
        ERROR_PRINT("db_create_session: INSERT failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return NULL;
    }
}

/* Возвращает user_id >=0 при валидной сессии, -1 при не найдено/истекла, -2 при ошибке */
int db_userid_by_session(const char *raw_token)
{
    if (!raw_token) return -1;

    /* вычислить sha256(raw_token) */
    char token_hash[65];
    if (sha256_hex(raw_token, token_hash, sizeof(token_hash)) != 0) {
        ERROR_PRINT("sha256_hex failed");
        return -2;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_connect failed");
        return -2;
    }

    const char *paramValues[1] = { token_hash };
    Oid paramTypes[1] = {25};

    PGresult *res = PQexecParams(db_conn,
        "SELECT user_id FROM sessions WHERE token = $1 AND expires_at > now();",
        1, paramTypes, paramValues, NULL, NULL, 0);

    if (!res) {
        ERROR_PRINT("PQexecParams returned NULL");
        db_disconnect();
        return -2;
    }

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        ERROR_PRINT("SELECT failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -2;
    }

    if (PQntuples(res) == 0) {
        DEBUG_PRINT_DB("token not found or expired (hash=%s)", token_hash);
        PQclear(res);
        db_disconnect();
        return -1;
    }

    int uid = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    /* Опционально: обновить last_access (commented — включите при желании)
       PGresult *ru = PQexecParams(db_conn, "UPDATE sessions SET last_access = now() WHERE token = $1;", 1, paramTypes, paramValues, NULL, NULL, 0);
       if (ru) PQclear(ru);
    */

    db_disconnect();
    DEBUG_PRINT_DB("token ok -> user_id=%d", uid);
    return uid;
}

/* Удалить сессию по raw token (хешируем перед удалением)
   Возвращает 0 при успехе, -1 при ошибке/не найдено */
int db_delete_session(const char *raw_token)
{
    if (!raw_token) return -1;

    char token_hash[65];
    if (sha256_hex(raw_token, token_hash, sizeof(token_hash)) != 0) {
        ERROR_PRINT("db_delete_session: sha256_hex failed");
        return -1;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_delete_session: db_connect failed");
        return -1;
    }

    const char *paramValues[1] = { token_hash };
    Oid paramTypes[1] = {25};

    PGresult *res = PQexecParams(db_conn,
        "DELETE FROM sessions WHERE token = $1;",
        1, paramTypes, paramValues, NULL, NULL, 0);

    if (!res) {
        ERROR_PRINT("db_delete_session: PQexecParams returned NULL");
        db_disconnect();
        return -1;
    }

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_COMMAND_OK) {
        ERROR_PRINT("db_delete_session: DELETE failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -1;
    }

    PQclear(res);
    db_disconnect();
    DEBUG_PRINT_DB("db_delete_session: deleted session (hash=%s)", token_hash);
    return 0;
}


/* Read whole file into a malloc'd buffer. Caller must free. */
static char *read_file_to_string(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ERROR_PRINT("failed to open file '%s'", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ERROR_PRINT("fseek failed");
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        ERROR_PRINT("ftell failed");
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        ERROR_PRINT("malloc failed for size %ld", size);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

/* Initialize DB schema by reading SQL file and executing it.
 * If 'schema_path' is NULL or empty, uses "src/db/schema.sql" by default.
 * Returns 0 on success, -1 on failure.
 */
int init_db(const char *schema_path) {
    const char *path = (schema_path && schema_path[0]) ? schema_path : "/build/src/db/schema.sql";
    if (!db_conn) {
        ERROR_PRINT("no database connection; call db_connect() first");
        return -1;
    }
    char *sql = read_file_to_string(path);
    if (!sql) {
        ERROR_PRINT("failed to read schema file '%s'", path);
        return -1;
    }

    DEBUG_PRINT_DB("executing schema from '%s'", path);

    /* Execute the whole file. libpq allows multiple commands in one string.
       We run it inside a transaction so either everything applies or nothing. */
    PGresult *res = PQexec(db_conn, "BEGIN");
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ERROR_PRINT("BEGIN failed: %s", PQerrorMessage(db_conn));
        if (res) PQclear(res);
        free(sql);
        return -1;
    }
    PQclear(res);

    res = PQexec(db_conn, sql);
    if (!res) {
        ERROR_PRINT("PQexec returned NULL: %s", PQerrorMessage(db_conn));
        free(sql);
        /* attempt to rollback */
        PGresult *r2 = PQexec(db_conn, "ROLLBACK"); if (r2) PQclear(r2);
        return -1;
    }

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        ERROR_PRINT("schema execution failed: %s", PQresultErrorMessage(res));
        PQclear(res);
        free(sql);
        PGresult *r2 = PQexec(db_conn, "ROLLBACK"); if (r2) PQclear(r2);
        return -1;
    }
    PQclear(res);

    res = PQexec(db_conn, "COMMIT");
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ERROR_PRINT("COMMIT failed: %s", PQerrorMessage(db_conn));
        if (res) PQclear(res);
        free(sql);
        return -1;
    }
    PQclear(res);

    free(sql);
    DEBUG_PRINT_DB("schema executed successfully");
    return 0;
}


/* Удалить пользователя по id.
   Возвращает 0 при успехе, -1 при ошибке. */
int db_delete_user(int user_id)
{
    if (user_id <= 0) {
        ERROR_PRINT("invalid user_id=%d", user_id);
        return -1;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_connect failed");
        return -1;
    }

    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%d", user_id);
    const char *paramValues[1] = { id_buf };
    Oid paramTypes[1] = { 23 }; /* 23 = int4 in Postgres */

    DEBUG_PRINT_DB("executing DELETE for id=%s", id_buf);

    PGresult *res = PQexecParams(db_conn,
                                 "DELETE FROM users WHERE id = $1;",
                                 1,        /* number of params */
                                 paramTypes,
                                 paramValues,
                                 NULL,     /* param lengths */
                                 NULL,     /* param formats */
                                 0);       /* result format text */

    if (!res) {
        ERROR_PRINT("PQexecParams returned NULL");
        db_disconnect();
        return -1;
    }

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_COMMAND_OK) {
        ERROR_PRINT("DELETE failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -1;
    }

    DEBUG_PRINT_DB("DELETE succeeded");
    PQclear(res);
    db_disconnect();
    return 0;
}

/*
 db_register_user:
  - возвращает >0 : id нового пользователя (успех)
  - возвращает 0  : вставка выполнена, но id не возвращён (маловероятно)
  - возвращает -2 : конфликт (пользователь уже существует) — полезно для HTTP 409
  - возвращает -1 : ошибка выполнения/соединения
*/
int db_register_user(const char *username, const char *email, const char *password_hash)
{
    if (!username || !email || !password_hash) {
        ERROR_PRINT("missing arguments");
        return -1;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_connect failed");
        return -1;
    }

    DEBUG_PRINT_DB("username='%s' email='%s'", username, email);

    const char *paramValues[3] = { username, email, password_hash };
    Oid paramTypes[3] = { 25, 25, 25 }; /* 25 = text */

    /* Попытаться вставить и вернуть id. Если конфликт по unique (username/email) — вернём 0 строк. */
    PGresult *res = PQexecParams(db_conn,
        "INSERT INTO users (username, email, password_hash) "
        "VALUES ($1, $2, $3) "
        "ON CONFLICT (username) DO NOTHING "
        "RETURNING id;",
        3, /* nparams */
        paramTypes,
        paramValues,
        NULL, NULL,
        0); /* text result */

    if (!res) {
        ERROR_PRINT("PQexecParams returned NULL");
        db_disconnect();
        return -1;
    }

    ExecStatusType st = PQresultStatus(res);
    if (st == PGRES_TUPLES_OK && PQntuples(res) == 1) {
        const char *idstr = PQgetvalue(res, 0, 0);
        int new_id = atoi(idstr);
        DEBUG_PRINT_DB("created user id=%d", new_id);
        PQclear(res);
        db_disconnect();
        return new_id;
    } else if (st == PGRES_TUPLES_OK && PQntuples(res) == 0) {
        /* ON CONFLICT DO NOTHING -> 0 rows => конфликт */
        DEBUG_PRINT_DB("insert resulted in 0 rows -> conflict (user exists?)");
        PQclear(res);
        db_disconnect();
        return -2;
    } else {
        ERROR_PRINT("INSERT failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -1;
    }
}

/*
 db_login_user:
  - возвращает >=0 : user_id (успешная аутентификация)
  - возвращает -1  : неверные учётные данные (not found)
  - возвращает -2  : ошибка выполнения/соединения
*/
int db_login_user(const char *username, const char *password_hash)
{
    if (!username || !password_hash) {
        ERROR_PRINT("missing arguments");
        return -2;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_connect failed");
        return -2;
    }

    DEBUG_PRINT_DB("username='%s'", username);

    const char *paramValues[2] = { username, password_hash };
    Oid paramTypes[2] = { 25, 25 }; /* both text */

    PGresult *res = PQexecParams(db_conn,
        "SELECT id FROM users WHERE username = $1 AND password_hash = $2;",
        2, paramTypes, paramValues, NULL, NULL, 0);

    if (!res) {
        ERROR_PRINT("PQexecParams returned NULL");
        db_disconnect();
        return -2;
    }

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        ERROR_PRINT("SELECT failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -2;
    }

    if (PQntuples(res) == 0) {
        DEBUG_PRINT_DB("no matching user");
        PQclear(res);
        db_disconnect();
        return -1; /* not found / invalid credentials */
    }

    const char *idstr = PQgetvalue(res, 0, 0);
    int user_id = atoi(idstr);
    DEBUG_PRINT_DB("success, user_id=%d", user_id);

    PQclear(res);
    db_disconnect();
    return user_id;
}

/**
 * Проверяет, существует ли слово (word) у пользователя (user_id).
 * Если найдено — возвращает его id; иначе — 0.
 */
int db_word_exists(const char *word, int user_id) {

	const char *conninfo = "host=localhost dbname=englearn user=enguser password=engpass";
	if (db_connect(conninfo) != 0) return 0;

    if (PQstatus(db_conn) != CONNECTION_OK) {
        db_disconnect();
        return 0;
    }

    // Экранируем слово для безопасного запроса
    char *esc_word = PQescapeLiteral(db_conn, word, strlen(word));
    if (!esc_word) {
        db_disconnect();
        return 0;
    }

    char query[1024];
    snprintf(query, sizeof(query),
             "SELECT id FROM words WHERE user_id = %d AND word = %s;",
             user_id, esc_word);

    PGresult *res = PQexec(db_conn, query);
    int word_id = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        word_id = atoi(PQgetvalue(res, 0, 0));
    }

    PQclear(res);
    PQfreemem(esc_word);
    db_disconnect();
    return word_id;
}

int db_add_word(const char *word, const char *transcription,
                const char *translation, const char *example, int user_id) {
    const char *paramValues[5] = { word, transcription, translation, example, NULL };
    char user_id_str[16];
    snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
    paramValues[4] = user_id_str;

    PGresult *res = PQexecParams(db_conn,
        "INSERT INTO words (word, transcription, translation, example, user_id) "
        "VALUES ($1, $2, $3, $4, $5) ON CONFLICT DO NOTHING;",
        5, NULL, paramValues, NULL, NULL, 0);
    int success = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return success ? 0 : -1;
}

int db_get_all_words(Word **out_list, size_t *out_count, int user_id) {
    const char *paramValues[1];
    char user_id_str[16];
    snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
    paramValues[0] = user_id_str;

    PGresult *res = PQexecParams(db_conn,
        "SELECT word, transcription, translation, example FROM words WHERE user_id = $1;",
        1, NULL, paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    Word *arr = malloc(rows * sizeof(Word));
    for (int i = 0; i < rows; ++i) {
        arr[i].word = strdup(PQgetvalue(res, i, 0));
        arr[i].transcription = strdup(PQgetvalue(res, i, 1));
        arr[i].translation = strdup(PQgetvalue(res, i, 2));
        arr[i].example = strdup(PQgetvalue(res, i, 3));
    }
    PQclear(res);

    *out_list = arr;
    *out_count = rows;
    return 0;
}

/* --- db_get_user_profile: заполняет username, words_learned и active_lessons.
     Возвращает 0 при успехе, -1 при ошибке, -2 если пользователя нет. --- */
int db_get_user_profile(int user_id, char *username_out, size_t uname_sz,
                        int *words_learned_out, int *active_lessons_out)
{
    if (user_id <= 0 || !username_out || uname_sz == 0 || !words_learned_out || !active_lessons_out) {
        ERROR_PRINT("db_get_user_profile: invalid args");
        return -1;
    }

    if (db_connect(DEFAULT_CONNINFO) != 0) {
        ERROR_PRINT("db_get_user_profile: db_connect failed");
        return -1;
    }

    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "%d", user_id);
    const char *paramValues[1] = { idbuf };
    Oid paramTypes[1] = { 23 }; /* int4 */

    /* 1) username */
    PGresult *res = PQexecParams(db_conn,
        "SELECT username FROM users WHERE id = $1;",
        1, paramTypes, paramValues, NULL, NULL, 0);
    if (!res) {
        ERROR_PRINT("db_get_user_profile: SELECT username returned NULL");
        db_disconnect();
        return -1;
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        ERROR_PRINT("db_get_user_profile: SELECT username failed: %s", PQerrorMessage(db_conn));
        PQclear(res);
        db_disconnect();
        return -1;
    }
    if (PQntuples(res) == 0) {
        PQclear(res);
        db_disconnect();
        return -2; /* user not found */
    }
    const char *uname = PQgetvalue(res, 0, 0);
    strncpy(username_out, uname, uname_sz - 1);
    username_out[uname_sz - 1] = '\0';
    PQclear(res);

    /* 2) words_learned -> COUNT(*) FROM words WHERE user_id = $1 */
    res = PQexecParams(db_conn,
        "SELECT COUNT(*) FROM words WHERE user_id = $1;",
        1, paramTypes, paramValues, NULL, NULL, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        ERROR_PRINT("db_get_user_profile: COUNT words failed");
        if (res) PQclear(res);
        db_disconnect();
        return -1;
    }
    int words = atoi(PQgetvalue(res, 0, 0));
    *words_learned_out = words;
    PQclear(res);

    /* 3) active_lessons -> COUNT(*) FROM texts WHERE user_id = $1 */
    res = PQexecParams(db_conn,
        "SELECT COUNT(*) FROM texts WHERE user_id = $1;",
        1, paramTypes, paramValues, NULL, NULL, 0);
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        ERROR_PRINT("db_get_user_profile: COUNT texts failed");
        if (res) PQclear(res);
        db_disconnect();
        return -1;
    }
    int lessons = atoi(PQgetvalue(res, 0, 0));
    *active_lessons_out = lessons;
    PQclear(res);

    db_disconnect();
    return 0;
}
