
#include "router.h"
#include "handlers/text_handler.h"
#include "handlers/card_handler.h"
#include "dbug/dbug.h"
#include <stdio.h>
#include "libs/http.h"
#include <string.h>  // для strncmp

void init_router(void)
{
	// Корень — отдаём index.html
	if (http_register_handler("GET", "/", handle_static) != 0) {
		fprintf(stderr, "Failed to register handler for GET /\n");
	}

	// Отдаём CSS
	if (http_register_handler("GET", "/login/login.css", handle_static) != 0) {
		fprintf(stderr, "Failed to register handler for GET /login/login.css\n");
	}

	// Отдаём JS
	if (http_register_handler("GET", "/login/login.js", handle_static) != 0) {
		fprintf(stderr, "Failed to register handler for GET /login/login.js\n");
	}

    /* Регистрация API-эндпоинтов */
	/*
    if (http_register_handler("POST",   "/api/text",     handle_add_text)    != 0) {
        fprintf(stderr, "Failed to register handler for POST /api/text\n");
    }*/

    if (http_register_handler("POST",   "/api/login",    handle_login)       != 0) {
        fprintf(stderr, "Failed to register handler for POST /api/login\n");
    }
	/*
    if (http_register_handler("POST",   "/api/register", handle_register)    != 0) {
        fprintf(stderr, "Failed to register handler for POST /api/register\n");
    }
    if (http_register_handler("DELETE", "/api/register", handle_register)    != 0) {
        fprintf(stderr, "Failed to register handler for DELETE /api/register\n");
    }*/

	if (http_register_handler("GET", "/api/cards", handle_cards) != 0) {
        fprintf(stderr, "Failed to register handler for GET /api/cards\n");
    }
    if (http_register_handler("POST", "/api/cards", handle_cards) != 0) {
        fprintf(stderr, "Failed to register handler for POST /api/cards\n");
    }
    /* Другие маршруты, например GET /api/cards или GET /api/text, если нужно */
    /* if (http_register_handler("GET", "/api/cards", handle_get_cards) != 0) { ... } */
}

