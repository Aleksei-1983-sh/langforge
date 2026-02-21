
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
		ERROR_PRINT("Failed to register handler for GET /\n");
	}

	if (http_register_handler("GET", "/assets/*", handle_static) != 0) {
		ERROR_PRINT("Failed to register handler for GET /assets/*\n");
	}

	if (http_register_handler("GET", "/pages/*", handle_static) != 0) {
		ERROR_PRINT("Failed to register handler for GET /pages/*\n");
	}

	if (http_register_handler("POST", "/api/login",    handle_login) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/login\n");
	}

	if (http_register_handler("POST", "/api/register",    handle_register) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/register\n");
	}

	if (http_register_handler("POST", "/api/me",    handle_me) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/me\n");
	}

	if (http_register_handler("POST", "/api/generate-card", handle_generate_card) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/generate-card\n");
	}
}

