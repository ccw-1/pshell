#include "pshell/rest_api.h"
#include "pshell/logger.h"
#include "pshell/json_simple.h"
#include "pshell/string_utils.h"
#include "pshell/session.h"
#include "pshell/expect_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* REST API structure */
struct rest_api {
    http_server_t *server;
    char *api_key;
    session_manager_t *session_mgr;
};

/* Forward declarations */
static void handle_create_session(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_list_sessions(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_get_session(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_destroy_session(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_send_input(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_expect_pattern(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_resize_terminal(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_get_output(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_get_screen(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_status(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_health(http_request_t *req, http_response_t *resp, void *user_data);

/* Helper: Check API key authentication */
static bool check_auth(rest_api_t *api, http_request_t *req, http_response_t *resp) {
    if (!api->api_key) {
        return true;  /* No auth required */
    }
    
    const char *auth = http_request_get_header(req, "Authorization");
    if (!auth) {
        http_response_send_error(resp, HTTP_STATUS_UNAUTHORIZED, "Missing Authorization header");
        return false;
    }
    
    /* Check for "Bearer <token>" format */
    if (strncmp(auth, "Bearer ", 7) != 0) {
        http_response_send_error(resp, HTTP_STATUS_UNAUTHORIZED, "Invalid Authorization format");
        return false;
    }
    
    const char *token = auth + 7;
    if (strcmp(token, api->api_key) != 0) {
        http_response_send_error(resp, HTTP_STATUS_UNAUTHORIZED, "Invalid API key");
        return false;
    }
    
    return true;
}

/* Helper: Extract session ID from path */
static char* extract_session_id(const char *path) {
    /* Path format: /api/sessions/:id or /api/sessions/:id/action */
    const char *sessions = strstr(path, "/api/sessions/");
    if (!sessions) return NULL;
    
    const char *id_start = sessions + strlen("/api/sessions/");
    const char *id_end = strchr(id_start, '/');
    
    size_t id_len = id_end ? (size_t)(id_end - id_start) : strlen(id_start);
    if (id_len == 0) return NULL;
    
    char *id = malloc(id_len + 1);
    if (!id) return NULL;
    
    memcpy(id, id_start, id_len);
    id[id_len] = '\0';
    
    return id;
}

rest_api_t* rest_api_create(http_server_t *server, session_manager_t *session_mgr) {
    if (!server) {
        log_error("Invalid HTTP server");
        return NULL;
    }
    
    rest_api_t *api = calloc(1, sizeof(rest_api_t));
    if (!api) {
        log_error("Failed to allocate REST API");
        return NULL;
    }
    
    api->server = server;
    api->session_mgr = session_mgr;
    
    log_info("Created REST API");
    return api;
}

void rest_api_destroy(rest_api_t *api) {
    if (!api) return;
    
    free(api->api_key);
    free(api);
    
    log_info("Destroyed REST API");
}

bool rest_api_register_routes(rest_api_t *api) {
    if (!api || !api->server) return false;
    
    /* Session management */
    http_server_register_route(api->server, HTTP_METHOD_POST,
                               "/api/sessions", handle_create_session, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions", handle_list_sessions, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id", handle_get_session, api);
    http_server_register_route(api->server, HTTP_METHOD_DELETE,
                               "/api/sessions/:id", handle_destroy_session, api);
    
    /* Session control */
    http_server_register_route(api->server, HTTP_METHOD_POST,
                               "/api/sessions/:id/send", handle_send_input, api);
    http_server_register_route(api->server, HTTP_METHOD_POST,
                               "/api/sessions/:id/expect", handle_expect_pattern, api);
    http_server_register_route(api->server, HTTP_METHOD_POST,
                               "/api/sessions/:id/resize", handle_resize_terminal, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id/output", handle_get_output, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id/screen", handle_get_screen, api);
    
    /* System */
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/status", handle_status, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/health", handle_health, api);
    
    log_info("Registered REST API routes");
    return true;
}

bool rest_api_set_key(rest_api_t *api, const char *api_key) {
    if (!api) return false;
    
    free(api->api_key);
    api->api_key = api_key ? strdup(api_key) : NULL;
    
    log_info("Set API key: %s", api->api_key ? "enabled" : "disabled");
    return true;
}

/* Handler: Create session */
static void handle_create_session(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    /* Parse request body */
    size_t body_size;
    const char *body = http_request_get_body(req, &body_size);
    
    if (!body || body_size == 0) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing request body");
        return;
    }
    
    json_value_t *json = json_parse(body);
    if (!json) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid JSON");
        return;
    }
    
    /* Extract parameters */
    const char *command = json_object_get_string(json, "command");
    int rows = json_object_get_int(json, "rows", 24);
    int cols = json_object_get_int(json, "cols", 80);
    
    if (!command) {
        json_free(json);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing 'command' field");
        return;
    }
    
    /* Create session using session manager */
    const char *session_id = session_manager_create_session(api->session_mgr, command, rows, cols);
    if (!session_id) {
        json_free(json);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to create session");
        return;
    }
    
    /* Build response */
    char response[512];
    snprintf(response, sizeof(response),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"status\":\"active\"}",
             session_id, command, rows, cols);
    
    json_free(json);
    http_response_send_json(resp, HTTP_STATUS_CREATED, response);
    
    log_info("Created session: id=%s, command=%s, rows=%d, cols=%d", session_id, command, rows, cols);
}

/* Handler: List sessions */
static void handle_list_sessions(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    /* Get sessions from session manager */
    session_info_t infos[50];
    int count = session_manager_list(api->session_mgr, infos, 50);
    
    /* Build JSON response */
    char response[8192];
    int len = snprintf(response, sizeof(response), "{\"sessions\":[");
    
    for (int i = 0; i < count; i++) {
        if (i > 0) len += snprintf(response + len, sizeof(response) - len, ",");
        len += snprintf(response + len, sizeof(response) - len,
                       "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
                       infos[i].id, infos[i].command, infos[i].rows, infos[i].cols, infos[i].pid,
                       infos[i].state == SESSION_STATE_ACTIVE ? "active" : "dead");
    }
    
    len += snprintf(response + len, sizeof(response) - len, "],\"count\":%d}", count);
    
    http_response_send_json(resp, HTTP_STATUS_OK, response);
    
    log_debug("Listed %d sessions", count);
}

/* Handler: Get session info */
static void handle_get_session(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Get session from session manager */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Get session info */
    session_info_t info;
    if (!session_get_info(session, &info)) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to get session info");
        return;
    }
    
    /* Build response */
    char response[512];
    snprintf(response, sizeof(response),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
             info.id, info.command, info.rows, info.cols, info.pid,
             info.state == SESSION_STATE_ACTIVE ? "active" : "dead");
    
    log_debug("Got session info: %s", session_id);
    free(session_id);
    http_response_send_json(resp, HTTP_STATUS_OK, response);
}

/* Handler: Destroy session */
static void handle_destroy_session(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Destroy session using session manager */
    if (!session_manager_destroy_session(api->session_mgr, session_id)) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    log_info("Destroyed session: %s", session_id);
    free(session_id);
    http_response_send_json(resp, HTTP_STATUS_NO_CONTENT, "{}");
}

/* Handler: Send input */
static void handle_send_input(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Parse request body */
    size_t body_size;
    const char *body = http_request_get_body(req, &body_size);
    
    if (!body || body_size == 0) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing request body");
        return;
    }
    
    json_value_t *json = json_parse(body);
    if (!json) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid JSON");
        return;
    }
    
    const char *text = json_object_get_string(json, "text");
    if (!text) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing 'text' field");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Send input to session */
    ssize_t written = session_send_input(session, text, strlen(text));
    if (written < 0) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to send input");
        return;
    }
    
    log_debug("Sent %zd bytes to session: %s", written, session_id);
    
    char response[256];
    snprintf(response, sizeof(response), "{\"status\":\"sent\",\"bytes\":%zd}", written);
    
    json_free(json);
    free(session_id);
    http_response_send_json(resp, HTTP_STATUS_OK, response);
}

/* Handler: Expect pattern */
static void handle_expect_pattern(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Parse request body */
    size_t body_size;
    const char *body = http_request_get_body(req, &body_size);
    
    if (!body || body_size == 0) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing request body");
        return;
    }
    
    json_value_t *json = json_parse(body);
    if (!json) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid JSON");
        return;
    }
    
    const char *pattern = json_object_get_string(json, "pattern");
    int timeout = json_object_get_int(json, "timeout", 30000); /* Default 30 seconds in ms */
    bool case_sensitive = json_object_get_bool(json, "case_sensitive", true);
    
    if (!pattern) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing 'pattern' field");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Create expect engine */
    expect_engine_t *engine = expect_engine_create(session, timeout, case_sensitive);
    if (!engine) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to create expect engine");
        return;
    }
    
    /* Add pattern */
    if (expect_engine_add_pattern(engine, pattern) < 0) {
        expect_engine_destroy(engine);
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid pattern");
        return;
    }
    
    /* Wait for pattern match */
    expect_match_t match;
    expect_result_t result = expect_engine_wait(engine, &match);
    
    /* Build response */
    char response[4096];
    if (result == EXPECT_MATCHED) {
        /* Build submatches array */
        char submatches_json[2048] = "[";
        for (int i = 0; i < match.nmatch && i < 10; i++) {
            if (i > 0) strcat(submatches_json, ",");
            if (match.submatches[i]) {
                strcat(submatches_json, "\"");
                /* TODO: Escape JSON string properly */
                strncat(submatches_json, match.submatches[i], 200);
                strcat(submatches_json, "\"");
            } else {
                strcat(submatches_json, "null");
            }
        }
        strcat(submatches_json, "]");
        
        snprintf(response, sizeof(response),
                 "{\"matched\":true,\"pattern_index\":%d,\"matched_text\":\"%s\",\"before\":\"%s\",\"after\":\"%s\",\"submatches\":%s}",
                 match.pattern_index,
                 match.matched_text ? match.matched_text : "",
                 match.before ? match.before : "",
                 match.after ? match.after : "",
                 submatches_json);
    } else if (result == EXPECT_TIMEOUT) {
        snprintf(response, sizeof(response),
                 "{\"matched\":false,\"result\":\"timeout\",\"timeout\":%d}", timeout);
    } else if (result == EXPECT_EOF) {
        snprintf(response, sizeof(response),
                 "{\"matched\":false,\"result\":\"eof\"}");
    } else {
        snprintf(response, sizeof(response),
                 "{\"matched\":false,\"result\":\"error\"}");
    }
    
    log_debug("Expect pattern in session %s: result=%d", session_id, result);
    
    expect_match_free(&match);
    expect_engine_destroy(engine);
    json_free(json);
    free(session_id);
    
    http_response_send_json(resp, HTTP_STATUS_OK, response);
}

/* Handler: Resize terminal */
static void handle_resize_terminal(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Parse request body */
    size_t body_size;
    const char *body = http_request_get_body(req, &body_size);
    
    if (!body || body_size == 0) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing request body");
        return;
    }
    
    json_value_t *json = json_parse(body);
    if (!json) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid JSON");
        return;
    }
    
    int rows = json_object_get_int(json, "rows", 0);
    int cols = json_object_get_int(json, "cols", 0);
    
    if (rows <= 0 || cols <= 0) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid rows or cols");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Resize session */
    if (!session_resize(session, rows, cols)) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to resize session");
        return;
    }
    
    log_debug("Resized session %s to %dx%d", session_id, rows, cols);
    
    char response[256];
    snprintf(response, sizeof(response), "{\"status\":\"resized\",\"rows\":%d,\"cols\":%d}", rows, cols);
    
    json_free(json);
    free(session_id);
    http_response_send_json(resp, HTTP_STATUS_OK, response);
}

/* Handler: Get output (long poll) */
static void handle_get_output(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Read output from session */
    char buffer[4096];
    ssize_t n = session_read_output(session, buffer, sizeof(buffer) - 1);
    
    if (n < 0) {
        free(session_id);
        http_response_send_json(resp, HTTP_STATUS_OK, "{\"output\":\"\",\"available\":false}");
        return;
    }
    
    buffer[n] = '\0';
    
    /* Escape output for JSON */
    char escaped[8192];
    size_t j = 0;
    for (ssize_t i = 0; i < n && j < sizeof(escaped) - 2; i++) {
        switch (buffer[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            default:
                if ((unsigned char)buffer[i] < 32) {
                    /* Control character - skip or escape as unicode */
                    j += snprintf(escaped + j, sizeof(escaped) - j, "\\u%04x", (unsigned char)buffer[i]);
                } else {
                    escaped[j++] = buffer[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    /* Build JSON response */
    char response[16384];
    int len = snprintf(response, sizeof(response),
                      "{\"output\":\"%s\",\"available\":%s,\"bytes\":%d}",
                      escaped, n > 0 ? "true" : "false", (int)n);
    
    if (len > 0 && len < (int)sizeof(response)) {
        http_response_send_json(resp, HTTP_STATUS_OK, response);
    } else {
        http_response_send_json(resp, HTTP_STATUS_OK, "{\"output\":\"\",\"available\":false}");
    }
    
    log_debug("Read %zd bytes from session: %s", n, session_id);
    free(session_id);
}

/* Handler: Status */
static void handle_status(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    /* TODO: Get actual status from session manager */
    const char *response = "{\"status\":\"running\",\"sessions\":0,\"uptime\":0}";
    
    http_response_send_json(resp, HTTP_STATUS_OK, response);
    
    log_debug("Status requested");
}

/* Handler: Health check */
static void handle_health(http_request_t *req, http_response_t *resp, void *user_data) {
    (void)req;
    (void)user_data;
    
    /* No auth required for health check */
    const char *response = "{\"status\":\"healthy\"}";
    
    http_response_send_json(resp, HTTP_STATUS_OK, response);
}



/* Handler: Get screen state */
static void handle_get_screen(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Get screen text */
    char screen_text[8192];
    size_t text_len = session_get_screen_text(session, screen_text, sizeof(screen_text));
    
    /* Get cursor position */
    int cursor_row, cursor_col;
    session_get_cursor_position(session, &cursor_row, &cursor_col);
    
    /* Build JSON response */
    char response[16384];
    int len = snprintf(response, sizeof(response),
                      "{\"text\":\"%s\",\"cursor_row\":%d,\"cursor_col\":%d,\"bytes\":%d}",
                      screen_text, cursor_row, cursor_col, (int)text_len);
    
    if (len > 0 && len < (int)sizeof(response)) {
        http_response_send_json(resp, HTTP_STATUS_OK, response);
    } else {
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to build response");
    }
    
    log_debug("Get screen from session: %s (cursor: %d,%d)", session_id, cursor_row, cursor_col);
    free(session_id);
}
