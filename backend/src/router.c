
#include "router.h"
#include "handlers/card_handler.h"
#include "handlers/generate_handler.h"
#include "handlers/profile_handler.h"
#include "handlers/user_handler.h"
#include "dbug/dbug.h"
#include "libs/http.h"
#include "modules/realtime/realtime_ws.h"

void init_router(void)
{
	if (http_register_handler("POST", "/api/v1/login", handle_login) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/v1/login\n");
	}

	if (http_register_handler("POST", "/api/v1/register", handle_register) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/v1/register\n");
	}

	if (http_register_handler("POST", "/api/v1/me", handle_me) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/v1/me\n");
	}

	if (http_register_handler("GET", "/api/v1/me", handle_me) != 0) {
		ERROR_PRINT("Failed to register handler for GET /api/v1/me\n");
	}

	if (http_register_handler("POST", "/api/v1/generate_card", handle_generate_card) != 0) {
		ERROR_PRINT("Failed to register handler for POST /api/v1/generate_card\n");
	}

	if (http_register_handler("GET", "/ws", handle_realtime_ws) != 0) {
		ERROR_PRINT("Failed to register handler for GET /ws\n");
	}

	if (http_register_handler("GET", "/events", handle_realtime_sse) != 0) {
		ERROR_PRINT("Failed to register handler for GET /events\n");
	}
}

