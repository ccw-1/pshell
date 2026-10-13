#include "pshell/rest_api.h"
#include "pshell/logger.h"
#include "pshell/json_simple.h"
#include "pshell/string_utils.h"
#include "pshell/session.h"
#include "pshell/expect_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* REST expects run synchronously on the event-loop thread, so the timeout
 * caps how long the entire daemon stalls. Socket expects run on a worker
 * thread and are capped separately (SOCKET_EXPECT_TIMEOUT_MAX_MS). */
#define REST_EXPECT_TIMEOUT_MAX_MS 30000

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
static void handle_get_raw(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_get_scrollback(http_request_t *req, http_response_t *resp, void *user_data);

static void handle_get_lines(http_request_t *req, http_response_t *resp, void *user_data);


static void handle_status(http_request_t *req, http_response_t *resp, void *user_data);
static void handle_health(http_request_t *req, http_response_t *resp, void *user_data);

/* Constant-time string comparison to avoid leaking key material via timing. */
static bool constant_time_equals(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t max = (la > lb) ? la : lb;
    unsigned char diff = (la == lb) ? 0 : 1;
    for (size_t i = 0; i < max; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

/* Check a token against a comma-separated key list (whitespace tolerated).
 * Every entry is compared even after a match so timing does not depend on
 * which key matched. */
static bool api_key_matches(const char *key_list, const char *token) {
    const char *p = key_list;
    size_t tlen = strlen(token);
    bool matched = false;
    
    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        /* Trim surrounding whitespace */
        while (len > 0 && (*p == ' ' || *p == '\t')) { p++; len--; }
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) { len--; }
        if (len > 0 && len == tlen) {
            unsigned char d = 0;
            for (size_t i = 0; i < len; i++) {
                d |= (unsigned char)p[i] ^ (unsigned char)token[i];
            }
            if (d == 0) matched = true;
        }
        if (!end) break;
        p = end + 1;
    }
    return matched;
}

/* Helper: Check API key authentication.
 * Accepts either header form:
 *   X-API-Key: <key>                (documented in README/examples)
 *   Authorization: Bearer <key>     (legacy form)
 * A single generic error is returned regardless of the failure reason so
 * responses do not reveal whether a key is configured. */
static bool check_auth(rest_api_t *api, http_request_t *req, http_response_t *resp) {
    if (!api->api_key) {
        return true;  /* No auth configured */
    }
    
    const char *token = NULL;
    const char *x_key = http_request_get_header(req, "X-API-Key");
    if (x_key && *x_key) {
        token = x_key;
    } else {
        const char *auth = http_request_get_header(req, "Authorization");
        if (auth && strncmp(auth, "Bearer ", 7) == 0) {
            token = auth + 7;
        }
    }
    
    if (!token || !api_key_matches(api->api_key, token)) {
        http_response_send_error(resp, HTTP_STATUS_UNAUTHORIZED, "Invalid or missing API key");
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
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id/raw", handle_get_raw, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id/scrollback", handle_get_scrollback, api);
    http_server_register_route(api->server, HTTP_METHOD_GET,
                               "/api/sessions/:id/lines", handle_get_lines, api);

    
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

/* External function to get default shell */
extern const char* get_default_shell(void);

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
    int scrollback_lines = json_object_get_int(json, "scrollback_lines", 1000);
    const char *output_file = json_object_get_string(json, "output_file");
    int rotate_count = json_object_get_int(json, "rotate_count", 0);
    int rotate_lines = json_object_get_int(json, "rotate_lines", 0);
    
    /* Validate dimensions: unbounded values overflow the int multiplication
     * in screen_buffer_create (rows*cols), producing a tiny allocation and
     * heap corruption, or request gigabytes of memory. */
    if (rows < 1 || rows > 512 || cols < 1 || cols > 1024 ||
        scrollback_lines < 0 || scrollback_lines > 100000) {
        json_free(json);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST,
                                 "Invalid rows (1-512), cols (1-1024), or scrollback_lines (0-100000)");
        return;
    }
    
    /* Use default shell if command not specified */
    if (!command) {
        command = get_default_shell();
        log_debug("Using default shell: %s", command);
    }
    
    /* Create session using session manager */
    const char *session_id = session_manager_create_session(api->session_mgr, command, rows, cols, scrollback_lines, output_file, rotate_count, rotate_lines);
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
    /* Log before json_free: 'command' points into the JSON tree and would
     * be a dangling pointer after free. */
    log_info("Created session: id=%s, command=%s, rows=%d, cols=%d", session_id, command, rows, cols);
    
    json_free(json);
    http_response_send_json(resp, HTTP_STATUS_CREATED, response);
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
    size_t len = 0;
    size_t cap = sizeof(response);
    int n;
    
    n = snprintf(response, cap, "{\"sessions\":[");
    len = (n > 0) ? (size_t)n : 0;
    
    for (int i = 0; i < count; i++) {
        /* snprintf returns the would-be length; once it >= remaining space
         * the output is truncated and len must not grow further (otherwise
         * cap - len underflows and the next write goes out of bounds). */
        size_t rem = (len < cap) ? cap - len : 0;
        n = snprintf(response + len, rem, "%s{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
                     i > 0 ? "," : "",
                     infos[i].id, infos[i].command, infos[i].rows, infos[i].cols, infos[i].pid,
                     infos[i].state == SESSION_STATE_ACTIVE ? "active" : "dead");
        if (n < 0 || (size_t)n >= rem) {
            /* Response full: report truncation instead of emitting invalid JSON */
            log_warn("list_sessions response truncated at %d sessions", i);
            break;
        }
        len += (size_t)n;
    }
    
    {
        size_t rem = (len < cap) ? cap - len : 0;
        n = snprintf(response + len, rem, "],\"count\":%d}", count);
        if (n > 0 && (size_t)n < rem) {
            len += (size_t)n;
        } else {
            /* No room for the terminator: force a valid (truncated) close.
             * "]}" + NUL needs 3 bytes, so the tail must start at cap-3. */
            static const char close_json[] = "]}";
            size_t tail = (cap - 3 < len) ? cap - 3 : len;
            memcpy(response + tail, close_json, 3);
            len = tail + 2;
        }
    }
    response[len < cap ? len : cap - 1] = '\0';
    
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
    
    /* Get total lines written */
    session_line_no_t total_lines = session_get_total_lines(session);
    
    /* Build response */
    char response[512];
    snprintf(response, sizeof(response),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\",\"total_lines\":%llu}",
             info.id, info.command, info.rows, info.cols, info.pid,
             info.state == SESSION_STATE_ACTIVE ? "active" : "dead",
             (unsigned long long)total_lines);
    
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
    
    /* Distinguish "not found" from "destroy refused": destroy_session now
     * refuses while an expect worker still holds the session pointer. */
    if (!session_manager_get(api->session_mgr, session_id)) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    if (!session_manager_destroy_session(api->session_mgr, session_id)) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_CONFLICT,
                                 "Cannot destroy session (expect in progress?)");
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
    
    /* The expect runs synchronously on the event-loop thread, so the timeout
     * directly bounds how long the whole daemon stalls. timeout <= 0 means
     * "wait forever" in the engine - reject it, and cap the maximum. */
    if (timeout <= 0 || timeout > REST_EXPECT_TIMEOUT_MAX_MS) {
        char err[128];
        snprintf(err, sizeof(err), "timeout must be between 1 and %d ms", REST_EXPECT_TIMEOUT_MAX_MS);
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, err);
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
    
    /* One expect per session: the expect buffer is shared, and a second
     * concurrent expect would interleave reads into the same buffer. */
    if (session_expect_active(session)) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_CONFLICT, "Expect already in progress for this session");
        return;
    }
    
    /* Create expect context */
    expect_context_t *ctx = expect_context_create(session, timeout, EXPECT_FLAG_NONE);
    if (!ctx) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to create expect context");
        return;
    }
    
    /* Wait for pattern match */
    match_result_t match;
    expect_result_t result = expect_pattern(ctx, pattern, case_sensitive, &match);
    
    /* Build response */
    char response[4096];
    if (result == EXPECT_MATCHED) {
        /* Escape matched text for JSON */
        char escaped_text[1024] = {0};
        if (match.matched_text) {
            const char *src = match.matched_text;
            char *dst = escaped_text;
            size_t remaining = sizeof(escaped_text) - 1;
            
            while (*src && remaining > 1) {
                if (*src == '"' || *src == '\\') {
                    if (remaining > 2) {
                        *dst++ = '\\';
                        remaining--;
                    }
                }
                *dst++ = *src++;
                remaining--;
            }
            *dst = '\0';
        }
        
        snprintf(response, sizeof(response),
                "{\"status\":\"matched\",\"matched_text\":\"%s\",\"start\":%zu,\"end\":%zu}",
                escaped_text, match.start, match.end);
        
        match_result_free(&match);
    } else if (result == EXPECT_TIMEOUT) {
        snprintf(response, sizeof(response),
                "{\"status\":\"timeout\",\"timeout\":%d}", timeout);
    } else if (result == EXPECT_EOF) {
        snprintf(response, sizeof(response),
                "{\"status\":\"eof\"}");
    } else {
        snprintf(response, sizeof(response),
                "{\"status\":\"error\"}");
    }
    
    log_debug("Expect pattern in session %s: result=%s", session_id, expect_result_name(result));
    
    expect_context_destroy(ctx);
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
    
    if (rows <= 0 || rows > 512 || cols <= 0 || cols > 1024) {
        json_free(json);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid rows (1-512) or cols (1-1024)");
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
    
    /* Read NEW output from session (raw buffer, consumed on read) */
    char buffer[4096];
    ssize_t n = session_read_raw_output(session, buffer, sizeof(buffer) - 1);
    
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
    
    /* Escape screen text for JSON */
    char escaped[16384];
    size_t j = 0;
    for (size_t i = 0; i < text_len && j < sizeof(escaped) - 2; i++) {
        switch (screen_text[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            default:
                if ((unsigned char)screen_text[i] < 32) {
                    /* Control character - escape as unicode */
                    j += snprintf(escaped + j, sizeof(escaped) - j, "\\u%04x", (unsigned char)screen_text[i]);
                } else {
                    escaped[j++] = screen_text[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    /* Build JSON response */
    char response[32768];
    int len = snprintf(response, sizeof(response),
                      "{\"text\":\"%s\",\"cursor_row\":%d,\"cursor_col\":%d,\"bytes\":%d}",
                      escaped, cursor_row, cursor_col, (int)text_len);
    
    if (len > 0 && len < (int)sizeof(response)) {
        http_response_send_json(resp, HTTP_STATUS_OK, response);
    } else {
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to build response");
    }
    
    log_debug("Get screen from session: %s (cursor: %d,%d)", session_id, cursor_row, cursor_col);
    free(session_id);
}

static void handle_get_raw(http_request_t *req, http_response_t *resp, void *user_data) {
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
    
    /* Check query parameters */
    bool peek_mode = false;
    bool clear_after = false;
    
    const char *peek_param = http_request_get_query(req, "peek");
    if (peek_param && (strcmp(peek_param, "true") == 0 || strcmp(peek_param, "1") == 0)) {
        peek_mode = true;
    }
    
    const char *clear_param = http_request_get_query(req, "clear");
    if (clear_param && (strcmp(clear_param, "true") == 0 || strcmp(clear_param, "1") == 0)) {
        clear_after = true;
    }
    
    /* Get available data size */
    size_t available = session_get_raw_available(session);
    
    /* Allocate buffer for raw data */
    size_t max_size = 65536;  /* 64KB max per request */
    size_t read_size = (available < max_size) ? available : max_size;
    char *raw_data = malloc(read_size + 1);
    if (!raw_data) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Memory allocation failed");
        return;
    }
    
    /* Read or peek raw data */
    size_t bytes_read;
    if (peek_mode) {
        bytes_read = session_peek_raw_output(session, raw_data, read_size);
    } else {
        bytes_read = session_read_raw_output(session, raw_data, read_size);
    }
    raw_data[bytes_read] = '\0';
    
    /* Clear buffer if requested */
    if (clear_after && !peek_mode) {
        session_clear_raw_buffer(session);
    }
    
    /* Escape raw data for JSON */
    size_t escaped_size = bytes_read * 6 + 1;  /* Worst case: all chars need \uXXXX */
    char *escaped = malloc(escaped_size);
    if (!escaped) {
        free(raw_data);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Memory allocation failed");
        return;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < bytes_read && j < escaped_size - 6; i++) {
        switch (raw_data[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            default:
                if ((unsigned char)raw_data[i] < 32 || (unsigned char)raw_data[i] == 127) {
                    /* Control character - escape as unicode */
                    j += snprintf(escaped + j, escaped_size - j, "\\u%04x", (unsigned char)raw_data[i]);
                } else {
                    escaped[j++] = raw_data[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    /* Build JSON response */
    size_t response_size = j + 256;
    char *response = malloc(response_size);
    if (!response) {
        free(escaped);
        free(raw_data);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Memory allocation failed");
        return;
    }
    
    int len = snprintf(response, response_size,
                      "{\"data\":\"%s\",\"bytes\":%zu,\"available\":%zu,\"peek\":%s}",
                      escaped, bytes_read, available, peek_mode ? "true" : "false");
    
    if (len > 0 && len < (int)response_size) {
        http_response_send_json(resp, HTTP_STATUS_OK, response);
    } else {
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to build response");
    }
    
    log_debug("Get raw output from session: %s (bytes: %zu, available: %zu, peek: %d)", 
              session_id, bytes_read, available, peek_mode);
    
    free(response);
    free(escaped);
    free(raw_data);
    free(session_id);
}

/* Handler: Get scrollback */
static void handle_get_scrollback(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    /* Get session ID from URL */
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Get query parameters */
    const char *start_str = http_request_get_query(req, "start_line");
    const char *count_str = http_request_get_query(req, "count");
    
    int start_line = start_str ? atoi(start_str) : 0;
    int count = count_str ? atoi(count_str) : 100;
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Get scrollback info */
    int scrollback_size = session_get_scrollback_size(session);
    int scrollback_used = session_get_scrollback_used(session);
    
    /* Validate parameters */
    if (start_line < 0 || start_line >= scrollback_used) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid start_line");
        return;
    }
    
    if (count <= 0) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid count");
        return;
    }
    
    /* Limit count to available lines */
    if (start_line + count > scrollback_used) {
        count = scrollback_used - start_line;
    }
    
    /* Build JSON response */
    char response[8192];
    int offset = snprintf(response, sizeof(response),
                         "{\"scrollback_size\":%d,\"scrollback_used\":%d,\"start_line\":%d,\"count\":%d,\"lines\":[",
                         scrollback_size, scrollback_used, start_line, count);
    
    for (int i = 0; i < count && offset < (int)sizeof(response) - 100; i++) {
        char line_buffer[256];
        ssize_t line_len = session_get_scrollback_line(session, start_line + i, line_buffer, sizeof(line_buffer));
        
        if (line_len < 0) break;
        
        if (i > 0) {
            offset += snprintf(response + offset, sizeof(response) - offset, ",");
        }
        
        offset += snprintf(response + offset, sizeof(response) - offset, "\"");
        
        /* Escape line for JSON */
        for (ssize_t j = 0; j < line_len && offset < (int)sizeof(response) - 10; j++) {
            switch (line_buffer[j]) {
                case '\n': offset += snprintf(response + offset, sizeof(response) - offset, "\\n"); break;
                case '\r': offset += snprintf(response + offset, sizeof(response) - offset, "\\r"); break;
                case '\t': offset += snprintf(response + offset, sizeof(response) - offset, "\\t"); break;
                case '"': offset += snprintf(response + offset, sizeof(response) - offset, "\\\""); break;
                case '\\': offset += snprintf(response + offset, sizeof(response) - offset, "\\\\"); break;
                default:
                    if (line_buffer[j] >= 32) {
                        response[offset++] = line_buffer[j];
                    }
                    break;
            }
        }
        
        offset += snprintf(response + offset, sizeof(response) - offset, "\"");
    }
    
    snprintf(response + offset, sizeof(response) - offset, "]}");
    
    http_response_send_json(resp, HTTP_STATUS_OK, response);
    free(session_id);
}

static void handle_get_lines(http_request_t *req, http_response_t *resp, void *user_data) {
    rest_api_t *api = (rest_api_t*)user_data;
    
    if (!check_auth(api, req, resp)) return;
    
    /* Get session ID from URL */
    char *session_id = extract_session_id(http_request_get_path(req));
    if (!session_id) {
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Invalid session ID");
        return;
    }
    
    /* Get query parameters */
    const char *start_str = http_request_get_query(req, "start");
    const char *end_str = http_request_get_query(req, "end");
    
    if (!start_str || !end_str) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "Missing start or end parameter");
        return;
    }
    
    int start_line = atoi(start_str);
    int end_line = atoi(end_str);
    
    /* Get session */
    session_t *session = session_manager_get(api->session_mgr, session_id);
    if (!session) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Session not found");
        return;
    }
    
    /* Get total lines */
    session_line_no_t total_lines = session_get_total_lines(session);
    
    /* Validate parameters */
    if (start_line < 1) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "start must be >= 1");
        return;
    }
    
    if (end_line < start_line) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "end must be >= start");
        return;
    }
    
    if (start_line > (int)total_lines) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_BAD_REQUEST, "start exceeds total lines");
        return;
    }
    
    /* Read line range */
    size_t content_len = 0;
    char *content = session_read_line_range(session, start_line, end_line, &content_len);
    
    if (!content) {
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Failed to read line range");
        return;
    }
    
    /* Build JSON response - need to escape content */
    /* Worst case every byte expands to a two-char escape (\n -> \\n). The old
     * content_len*2 bound was one byte short of worst case per char plus the
     * fixed header, which could truncate the JSON. Size it safely. */
    size_t response_capacity = content_len * 2 + 1024;
    char *response_json = malloc(response_capacity);
    if (!response_json) {
        free(content);
        free(session_id);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Out of memory");
        return;
    }
    
    int offset = snprintf(response_json, response_capacity,
                         "{\"total_lines\":%llu,\"start_line\":%d,\"end_line\":%d,\"content\":\"",
                         (unsigned long long)total_lines, start_line,
                         end_line > (int)total_lines ? (int)total_lines : end_line);
    
    /* Escape content for JSON */
    for (size_t i = 0; i < content_len && offset < (int)response_capacity - 10; i++) {
        switch (content[i]) {
            case '\n': offset += snprintf(response_json + offset, response_capacity - offset, "\\n"); break;
            case '\r': offset += snprintf(response_json + offset, response_capacity - offset, "\\r"); break;
            case '\t': offset += snprintf(response_json + offset, response_capacity - offset, "\\t"); break;
            case '"': offset += snprintf(response_json + offset, response_capacity - offset, "\\\""); break;
            case '\\': offset += snprintf(response_json + offset, response_capacity - offset, "\\\\"); break;
            default:
                if (content[i] >= 32 || content[i] == '\n' || content[i] == '\r' || content[i] == '\t') {
                    response_json[offset++] = content[i];
                }
                break;
        }
    }
    
    snprintf(response_json + offset, response_capacity - offset, "\"}");
    
    http_response_set_header(resp, "Content-Type", "application/json");
    http_response_send_json(resp, HTTP_STATUS_OK, response_json);
    
    free(content);
    free(response_json);
    free(session_id);
}
