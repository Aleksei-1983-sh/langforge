
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include "db.h"
#include "models/word.h"
#include "dbug/dbug.h"

/* Global connection (as requested) */
static PGconn *conn = NULL;

/* Connect to Postgres using connection string (libpq format). Returns 0 on success, -1 on error. */
int db_connect(const char *conninfo) {
    if (conn) {
        DEBUG_PRINT_DB("existing connection found, disconnecting first");
        PQfinish(conn);
        conn = NULL;
    }
    DEBUG_PRINT_DB("connecting with conninfo: %s", conninfo ? conninfo : "(null)");
    conn = PQconnectdb(conninfo);
    if (!conn) {
        ERROR_PRINT_DB("PQconnectdb returned NULL");
        return -1;
    }
    if (PQstatus(conn) != CONNECTION_OK) {
        ERROR_PRINT_DB("connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        conn = NULL;
        return -1;
    }
    DEBUG_PRINT_DB("connected to database");
    return 0;
}

/* Disconnect if connected. */
void db_disconnect(void) {
    if (!conn) return;
    DEBUG_PRINT_DB("disconnecting from database");
    PQfinish(conn);
    conn = NULL;
}

/* Read whole file into a malloc'd buffer. Caller must free. */
static char *read_file_to_string(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ERROR_PRINT_DB("failed to open file '%s'", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ERROR_PRINT_DB("fseek failed");
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        ERROR_PRINT_DB("ftell failed");
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        ERROR_PRINT_DB("malloc failed for size %ld", size);
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
    const char *path = (schema_path && schema_path[0]) ? schema_path : "src/db/schema.sql";
    if (!conn) {
        ERROR_PRINT_DB("no database connection; call db_connect() first");
        return -1;
    }
    char *sql = read_file_to_string(path);
    if (!sql) {
        ERROR_PRINT_DB("failed to read schema file '%s'", path);
        return -1;
    }

    DEBUG_PRINT_DB("executing schema from '%s'", path);

    /* Execute the whole file. libpq allows multiple commands in one string.
       We run it inside a transaction so either everything applies or nothing. */
    PGresult *res = PQexec(conn, "BEGIN");
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ERROR_PRINT_DB("BEGIN failed: %s", PQerrorMessage(conn));
        if (res) PQclear(res);
        free(sql);
        return -1;
    }
    PQclear(res);

    res = PQexec(conn, sql);
    if (!res) {
        ERROR_PRINT_DB("PQexec returned NULL: %s", PQerrorMessage(conn));
        free(sql);
        /* attempt to rollback */
        PGresult *r2 = PQexec(conn, "ROLLBACK"); if (r2) PQclear(r2);
        return -1;
    }

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        ERROR_PRINT_DB("schema execution failed: %s", PQresultErrorMessage(res));
        PQclear(res);
        free(sql);
        PGresult *r2 = PQexec(conn, "ROLLBACK"); if (r2) PQclear(r2);
        return -1;
    }
    PQclear(res);

    res = PQexec(conn, "COMMIT");
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ERROR_PRINT_DB("COMMIT failed: %s", PQerrorMessage(conn));
        if (res) PQclear(res);
        free(sql);
        return -1;
    }
    PQclear(res);

    free(sql);
    DEBUG_PRINT_DB("schema executed successfully");
    return 0;
}

int db_delete_user(int user_id) {

	const char *conninfo = "host=localhost dbname=englearn user=enguser password=engpass";
	if (db_connect(conninfo) != 0) return 1;

	const char *param_values[1];
    char id_buf[12];
    snprintf(id_buf, sizeof(id_buf), "%d", user_id);
    param_values[0] = id_buf;

    PGresult *res = PQexecParams(conn,
        "DELETE FROM users WHERE id = $1",
        1, NULL, param_values, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        PQclear(res);
        db_disconnect();
        return -1;
    }

    PQclear(res);
    db_disconnect();
    return 0;
}

int db_register_user(const char *username, const char *email, const char *password_hash) {
    const char *paramValues[3] = { username, email, password_hash };
    PGresult *res = PQexecParams(conn,
        "INSERT INTO users (username, email, password_hash) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING;",
        3, NULL, paramValues, NULL, NULL, 0);

    int success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    return success ? 0 : -1;
}

//поверено работает 
int db_login_user(const char *username, const char *password_hash) {
	int status;
	DEBUG_PRINT_DB("ENTER username: '%s' password_hash: '%s'", username, password_hash);
	Oid paramTypes[2] = { 25, 25 }; // 25 = OID для text в db строке в конкретной таблици текстто надо это явно указать для каждого параметра

	const char *paramValues[2] = { username, password_hash };
    PGresult *res = PQexecParams(conn,
        "SELECT id FROM users WHERE username = $1 AND password_hash = $2;",
        2, paramTypes, paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        status = -1;
		goto exit;
    }

    status = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

exit:
	DEBUG_PRINT_DB("EXIT static: %d", status);
    return status; 

}

/**
 * Проверяет, существует ли слово (word) у пользователя (user_id).
 * Если найдено — возвращает его id; иначе — 0.
 */
int db_word_exists(const char *word, int user_id) {

	const char *conninfo = "host=localhost dbname=englearn user=enguser password=engpass";
	if (db_connect(conninfo) != 0) return 0;

    if (PQstatus(conn) != CONNECTION_OK) {
        db_disconnect();
        return 0;
    }

    // Экранируем слово для безопасного запроса
    char *esc_word = PQescapeLiteral(conn, word, strlen(word));
    if (!esc_word) {
        db_disconnect();
        return 0;
    }

    char query[1024];
    snprintf(query, sizeof(query),
             "SELECT id FROM words WHERE user_id = %d AND word = %s;",
             user_id, esc_word);

    PGresult *res = PQexec(conn, query);
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

    PGresult *res = PQexecParams(conn,
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

    PGresult *res = PQexecParams(conn,
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
