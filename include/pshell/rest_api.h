#ifndef PSHELL_REST_API_H
#define PSHELL_REST_API_H

#include "pshell/http_server.h"
#include "pshell/session.h"
#include <stdbool.h>

/**
 * REST API for pshell
 * Provides HTTP endpoints for session management and control
 */

typedef struct rest_api rest_api_t;

/**
 * Create REST API instance
 * @param server HTTP server to attach to
 * @param session_mgr Session manager
 * @return API instance or NULL on error
 */
rest_api_t* rest_api_create(http_server_t *server, session_manager_t *session_mgr);

/**
 * Destroy REST API instance
 * @param api API instance
 */
void rest_api_destroy(rest_api_t *api);

/**
 * Register all REST API routes
 * @param api API instance
 * @return true on success
 */
bool rest_api_register_routes(rest_api_t *api);

/**
 * Set API key for authentication
 * @param api API instance
 * @param api_key API key string
 * @return true on success
 */
bool rest_api_set_key(rest_api_t *api, const char *api_key);

/* API Endpoints:
 *
 * Session Management:
 * POST   /api/sessions          - Create new session
 * GET    /api/sessions          - List all sessions
 * GET    /api/sessions/:id      - Get session info
 * DELETE /api/sessions/:id      - Destroy session
 *
 * Session Control:
 * POST   /api/sessions/:id/send - Send input to session
 * POST   /api/sessions/:id/expect - Wait for pattern
 * POST   /api/sessions/:id/resize - Resize terminal
 * GET    /api/sessions/:id/output - Get output (long poll)
 *
 * System:
 * GET    /api/status            - Server status
 * GET    /api/health            - Health check
 */

#endif /* PSHELL_REST_API_H */
