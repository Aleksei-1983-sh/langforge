/* ./main.c */
#include <unistd.h>   /* для unlink() */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "ollama/ollama.h"
#include "dbug/dbug.h"
#include "router.h"
#include "db/db.h"
#include "libs/http.h"
#include "libs/redis/redis.h"
#include "card_handler.h"

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
	const char *filepath = "/app/debug.log";
//	const char *filepath = "/home/di/projects_С/git_progect/langforge/debug.log";
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
	delete_debug_log();

	DEBUG_PRINT_MAIN("START SERVER !!!!!!!!!!!!!!!!!");
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

	ollama_init();
	redis_init();
	/* Инициализация db conninfo */
	db_init_conninfo();

	resolve_www_dir();

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

	DEBUG_PRINT_MAIN("Starting server on port %d\n", LISTEN_PORT);

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

	/* Основной цикл: опрашиваем сервер */
	while (keep_running)
	{
		http_server_poll();
		/* Можно добавить небольшую задержку или таймаут внутри http_server_poll */
	}

	DEBUG_PRINT_MAIN("Shutting down server...\n");
	http_server_stop();

	return 0;
}

