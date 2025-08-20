/* main.c */
#include <unistd.h>   /* для unlink() */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "dbug/dbug.h"
#include "router.h"
#include "db/db.h"
#include "libs/http.h"

#define LISTEN_PORT 1234

static volatile int keep_running = 1;

/* Обработчик сигнала для корректного завершения */
static void sigint_handler(int signo)
{
	(void)signo;
	keep_running = 0;
}

/* Удаляем лог-файл отладки */
void delete_debug_log(void)
{
	const char *filepath = "./src/dbug/debug.log";
	if (unlink(filepath) == 0) {
		printf("file deleted %s.\n", filepath);
	} else {
		if (errno == ENOENT) {
			printf("file %s does not exist.\n", filepath);
		} else {
			fprintf(stderr, "delete error %s: %s\n",
					filepath, strerror(errno));
		}
	}
}

int main(void)
{
	/* Установка обработчика SIGINT, чтобы можно было CTRL+C остановить сервер */
	struct sigaction sa;
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGINT, &sa, NULL) != 0)
	{
		perror("sigaction");
		/* Продолжаем без корректной обработки SIGINT, но предупредим */
	}

	/* Инициализация БД */
	const char *pguser = getenv("PGUSER");
	const char *pgpass = getenv("PGPASSWORD");
	const char *pgdb   = getenv("PGDATABASE");
	const char *pghost = getenv("PGHOST");

	char conninfo[256];
	snprintf(conninfo, sizeof(conninfo),
			 "host=%s dbname=%s user=%s password=%s",
			 pghost ? pghost : "localhost",
			 pgdb   ? pgdb   : "englearn",
			 pguser ? pguser : "enguser",
			 pgpass ? pgpass : "engpass");

	if (db_connect(conninfo) != 0) return 1;
	if (init_db(NULL) != 0)
	{
		fprintf(stderr, "Failed to initialize database\n");
		db_disconnect();
		return 1;
	}
	DEBUG_PRINT_MAIN("Initialize database OK!");
	delete_debug_log();

	/* Инициализация маршрутов.
	 * Внутри init_router() вы должны зарегистрировать все нужные пути:
	 *   http_register_handler("GET", "/foo", foo_handler);
	 *   http_register_handler("POST", "/bar", bar_handler);
	 * и т.д.
	 * Если нужны динамические пути (например /items/:id), см. предложения ниже.
	 */
	init_router();

	/* Запуск HTTP-сервера на LISTEN_PORT */
	if (http_server_start(LISTEN_PORT) != 0)
	{
		fprintf(stderr, "Error starting server on port %d\n", LISTEN_PORT);
		return 1;
	}

	printf("Starting server on port %d\n", LISTEN_PORT);

	/* Если нужна единая точка входа (catch-all), можно зарегистрировать generic_http_handler
	 * на "/" после init_router, но тогда роутер внутри должен разбирать req->path вручную.
	 * Иначе: предполагается, что init_router вызывает http_register_handler для конкретных путей.
	 *
	 * Пример регистрации catch-all (если библиотека поддерживает wildcard):
	 *   http_register_handler("GET", "/", generic_http_handler);
	 *
	 * Но если http_register_handler требует точного совпадения, лучше внутри init_router
	 * регистрировать все пути явно.
	 *
	 * Для отладки можно добавить:
	 */
#ifdef DEBUG
	/* Если хотите видеть все запросы даже без точной регистрации: */
	/* http_register_handler("GET", "/", generic_http_handler); */
	/* http_register_handler("POST", "/", generic_http_handler); */
#endif

	/* Основной цикл: опрашиваем сервер */
	while (keep_running)
	{
		http_server_poll();
		/* Можно добавить небольшую задержку или таймаут внутри http_server_poll */
	}

	printf("Shutting down server...\n");
	http_server_stop();

	return 0;
}

