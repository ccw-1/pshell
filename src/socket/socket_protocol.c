/*
 * socket_protocol.c - Unix socket protocol implementation
 * 
 * Implements JSON-based protocol for socket communication.
 */

#include "pshell/socket_server.h"
#include "pshell/json_simple.h"
#include "pshell/session.h"
#include "pshell/expect_engine.h"
#include "pshell/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

/* External function to get default shell */
extern const char* get_default_shell(void);

/* Forward declarations */
static void handle_create_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_list_sessions(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_get_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_destroy_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_send_text(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_get_output(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_get_screen(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_resize_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_expect_pattern(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_get_scrollback(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);

static void handle_daemon_status(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);

static const char* state_to_string(session_state_t state) {
    switch (state) {
        case SESSION_STATE_STARTING: return "starting";
        case SESSION_STATE_ACTIVE: return "active";
        case SESSION_STATE_IDLE: return "idle";
        case SESSION_STATE_DEAD: return "dead";
        default: return "unknown";
    }
}

static void send_response(socket_connection_t *conn, const char *response) {
    /* Send length prefix (4 bytes, network byte order) */
    size_t len = strlen(response);
    uint32_t len_net = htonl((uint32_t)len);
    
    int fd = socket_connection_get_fd(conn);
    if (write(fd, &len_net, sizeof(len_net)) != sizeof(len_net)) {
        log_error("Failed to send response length");
        return;
    }
    
    /* Send response */
    if (write(fd, response, len) != (ssize_t)len) {
        log_error("Failed to send response");
        return;
    }
}

static void send_error(socket_connection_t *conn, const char *message) {
    char response[1024];
    snprintf(response, sizeof(response),
             "{\"status\":\"error\",\"error\":{\"message\":\"%s\"}}",
             message);
    send_response(conn, response);
}

static void send_success(socket_connection_t *conn, const char *result) {
    char response[8192];
    snprintf(response, sizeof(response),
             "{\"status\":\"success\",\"result\":%s}",
             result ? result : "{}");
    send_response(conn, response);
}

void socket_protocol_handler(socket_connection_t *conn, const char *message, void *user_data) {
    session_manager_t *mgr = (session_manager_t*)user_data;
    
    log_debug("Socket message received: %s", message);
    
    /* Parse JSON message */
    json_value_t *json = json_parse(message);
    if (!json) {
        send_error(conn, "Invalid JSON");
        return;
    }
    
    /* Get method */
    const char *method = json_object_get_string(json, "method");
    if (!method) {
        send_error(conn, "Missing method field");
        json_free(json);
        return;
    }
    
    /* Get params */
    json_value_t *params = json_object_get(json, "params");
    
    /* Dispatch method */
    if (strcmp(method, "create_session") == 0) {
        handle_create_session(conn, params, mgr);
    } else if (strcmp(method, "list_sessions") == 0) {
        handle_list_sessions(conn, params, mgr);
    } else if (strcmp(method, "get_session") == 0) {
        handle_get_session(conn, params, mgr);
    } else if (strcmp(method, "destroy_session") == 0) {
        handle_destroy_session(conn, params, mgr);
    } else if (strcmp(method, "send_text") == 0) {
        handle_send_text(conn, params, mgr);
    } else if (strcmp(method, "get_output") == 0) {
        handle_get_output(conn, params, mgr);
    } else if (strcmp(method, "get_screen") == 0) {
        handle_get_screen(conn, params, mgr);
    } else if (strcmp(method, "get_scrollback") == 0) {
        handle_get_scrollback(conn, params, mgr);
    } else if (strcmp(method, "resize_session") == 0) {
        handle_resize_session(conn, params, mgr);
    } else if (strcmp(method, "expect_pattern") == 0) {
        handle_expect_pattern(conn, params, mgr);
    } else if (strcmp(method, "daemon_status") == 0) {
        handle_daemon_status(conn, params, mgr);
    } else {
        send_error(conn, "Unknown method");
    }
    
    json_free(json);
}

static void handle_create_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *command = json_object_get_string(params, "command");
    int rows = json_object_get_int(params, "rows", 24);
    int cols = json_object_get_int(params, "cols", 80);
    int scrollback_lines = json_object_get_int(params, "scrollback_lines", 1000);
    const char *output_file = json_object_get_string(params, "output_file");
    int rotate_count = json_object_get_int(params, "rotate_count", 0);
    int rotate_lines = json_object_get_int(params, "rotate_lines", 0);
    
    /* Use default shell if command not specified */
    if (!command) {
        command = get_default_shell();
        log_debug("Using default shell: %s", command);
    }
    
    /* Create session */
    const char *session_id = session_manager_create_session(mgr, command, rows, cols, scrollback_lines, output_file, rotate_count, rotate_lines);
    if (!session_id) {
        send_error(conn, "Failed to create session");
        return;
    }
    
    /* Build response */
    char result[512];
    snprintf(result, sizeof(result),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d}",
             session_id, command, rows, cols);
    
    send_success(conn, result);
}

static void handle_list_sessions(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    (void)params;
    
    /* Get session list */
    session_info_t sessions[50];
    int count = session_manager_list(mgr, sessions, 50);
    
    if (count < 0) {
        send_error(conn, "Failed to list sessions");
        return;
    }
    
    /* Build JSON array */
    char result[8192];
    int offset = snprintf(result, sizeof(result), "{\"sessions\":[");
    
    for (int i = 0; i < count; i++) {
        offset += snprintf(result + offset, sizeof(result) - offset,
                          "%s{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
                          i > 0 ? "," : "",
                          sessions[i].id,
                          sessions[i].command,
                          sessions[i].rows,
                          sessions[i].cols,
                          sessions[i].pid,
                          state_to_string(sessions[i].state));
    }
    
    snprintf(result + offset, sizeof(result) - offset, "],\"count\":%d}", count);
    
    send_success(conn, result);
}

static void handle_get_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Get session info */
    session_info_t info;
    if (!session_get_info(session, &info)) {
        send_error(conn, "Failed to get session info");
        return;
    }
    
    /* Build response */
    char result[512];
    snprintf(result, sizeof(result),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
             info.id, info.command, info.rows, info.cols, info.pid, state_to_string(info.state));
    
    send_success(conn, result);
}

static void handle_destroy_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    /* Destroy session */
    if (!session_manager_destroy_session(mgr, session_id)) {
        send_error(conn, "Failed to destroy session");
        return;
    }
    
    send_success(conn, "{}");
}

static void handle_send_text(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    const char *text = json_object_get_string(params, "text");
    
    if (!session_id || !text) {
        send_error(conn, "Missing session id or text");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Send text */
    ssize_t bytes = session_send_input(session, text, strlen(text));
    if (bytes < 0) {
        send_error(conn, "Failed to send text");
        return;
    }
    
    /* Build response */
    char result[128];
    snprintf(result, sizeof(result), "{\"bytes\":%zd}", bytes);
    
    send_success(conn, result);
}

static void handle_get_output(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Read output */
    char output[4096];
    ssize_t bytes = session_read_output(session, output, sizeof(output) - 1);
    if (bytes < 0) {
        send_error(conn, "Failed to read output");
        return;
    }
    
    output[bytes] = '\0';
    
    /* Build response (escape output for JSON) */
    char result[8192];
    int offset = snprintf(result, sizeof(result), "{\"output\":\"");
    
    for (ssize_t i = 0; i < bytes && offset < (int)sizeof(result) - 10; i++) {
        switch (output[i]) {
            case '\n': offset += snprintf(result + offset, sizeof(result) - offset, "\\n"); break;
            case '\r': offset += snprintf(result + offset, sizeof(result) - offset, "\\r"); break;
            case '\t': offset += snprintf(result + offset, sizeof(result) - offset, "\\t"); break;
            case '"': offset += snprintf(result + offset, sizeof(result) - offset, "\\\""); break;
            case '\\': offset += snprintf(result + offset, sizeof(result) - offset, "\\\\"); break;
            default:
                if (output[i] >= 32 || output[i] == '\n' || output[i] == '\r' || output[i] == '\t') {
                    result[offset++] = output[i];
                }
                break;
        }
    }
    
    snprintf(result + offset, sizeof(result) - offset, "\",\"bytes\":%zd}", bytes);
    
    send_success(conn, result);
}

static void handle_get_screen(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Get screen */
    char screen[4096];
    size_t bytes = session_get_screen_text(session, screen, sizeof(screen) - 1);
    screen[bytes] = '\0';
    
    /* Get cursor position */
    int cursor_row, cursor_col;
    session_get_cursor_position(session, &cursor_row, &cursor_col);
    
    /* Build response */
    char result[8192];
    int offset = snprintf(result, sizeof(result), "{\"text\":\"");
    
    for (size_t i = 0; i < bytes && offset < (int)sizeof(result) - 100; i++) {
        switch (screen[i]) {
            case '\n': offset += snprintf(result + offset, sizeof(result) - offset, "\\n"); break;
            case '\r': offset += snprintf(result + offset, sizeof(result) - offset, "\\r"); break;
            case '\t': offset += snprintf(result + offset, sizeof(result) - offset, "\\t"); break;
            case '"': offset += snprintf(result + offset, sizeof(result) - offset, "\\\""); break;
            case '\\': offset += snprintf(result + offset, sizeof(result) - offset, "\\\\"); break;
            default:
                if (screen[i] >= 32 || screen[i] == '\n' || screen[i] == '\r' || screen[i] == '\t') {
                    result[offset++] = screen[i];
                }
                break;
        }
    }
    
    snprintf(result + offset, sizeof(result) - offset,
             "\",\"cursor_row\":%d,\"cursor_col\":%d,\"bytes\":%zu}",
             cursor_row, cursor_col, bytes);
    
    send_success(conn, result);
}

static void handle_resize_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    int rows = json_object_get_int(params, "rows", 0);
    int cols = json_object_get_int(params, "cols", 0);
    
    if (!session_id || rows <= 0 || cols <= 0) {
        send_error(conn, "Invalid parameters");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Resize session */
    if (!session_resize(session, rows, cols)) {
        send_error(conn, "Failed to resize session");
        return;
    }
    
    /* Build response */
    char result[128];
    snprintf(result, sizeof(result), "{\"rows\":%d,\"cols\":%d}", rows, cols);
    
    send_success(conn, result);
}

static void handle_expect_pattern(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    log_info("handle_expect_pattern called");
    
    if (!params) {
        log_error("handle_expect_pattern: Missing params");
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    const char *pattern = json_object_get_string(params, "pattern");
    int timeout = json_object_get_int(params, "timeout", 30000);
    bool case_sensitive = json_object_get_bool(params, "case_sensitive", true);
    
    log_info("handle_expect_pattern: session_id=%s, pattern=%s, timeout=%d", 
             session_id ? session_id : "NULL", pattern ? pattern : "NULL", timeout);
    
    if (!session_id || !pattern) {
        log_error("handle_expect_pattern: Missing session id or pattern");
        send_error(conn, "Missing session id or pattern");
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Create expect context */
    expect_context_t *ctx = expect_context_create(session, timeout);
    if (!ctx) {
        send_error(conn, "Failed to create expect context");
        return;
    }
    
    /* Wait for pattern match */
    match_result_t match;
    expect_result_t result = expect_pattern(ctx, pattern, case_sensitive, &match);
    
    /* Build response */
    char response[4096];
    if (result == EXPECT_MATCHED) {
        /* Calculate line position relative to current cursor */
        const char *buffer = expect_get_buffer(ctx);
        int matched_line = 0;
        for (size_t i = 0; i < match.start && buffer[i]; i++) {
            if (buffer[i] == '\n') matched_line++;
        }
        
        int cursor_row, cursor_col;
        session_get_cursor_position(session, &cursor_row, &cursor_col);
        int relative_line = matched_line - cursor_row;
        
        snprintf(response, sizeof(response),
                "{\"found\":true,\"matched_text\":\"%s\",\"start\":%zu,\"end\":%zu,\"line\":%d}",
                match.matched_text ? match.matched_text : "", match.start, match.end, relative_line);
        match_result_free(&match);
    } else if (result == EXPECT_TIMEOUT) {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"timeout\"}");
    } else if (result == EXPECT_EOF) {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"eof\"}");
    } else {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"error\"}");
    }
    
    expect_context_destroy(ctx);
    send_success(conn, response);
}

static void handle_daemon_status(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    (void)params;
    
    /* Get session count */
    int count = session_manager_count(mgr);
    
    /* Build response */
    char result[256];
    snprintf(result, sizeof(result),
             "{\"status\":\"running\",\"sessions\":%d}",
             count);
    
    send_success(conn, result);
}

static void handle_get_scrollback(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    int start_line = json_object_get_int(params, "start_line", 0);
    int count = json_object_get_int(params, "count", 100);
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Get scrollback info */
    int scrollback_size = session_get_scrollback_size(session);
    int scrollback_used = session_get_scrollback_used(session);
    
    /* Validate parameters */
    if (start_line < 0 || start_line >= scrollback_used) {
        send_error(conn, "Invalid start_line");
        return;
    }
    
    if (count <= 0) {
        send_error(conn, "Invalid count");
        return;
    }
    
    /* Limit count to available lines */
    if (start_line + count > scrollback_used) {
        count = scrollback_used - start_line;
    }
    
    /* Build response with lines array */
    char result[8192];
    int offset = snprintf(result, sizeof(result), 
                         "{\"scrollback_size\":%d,\"scrollback_used\":%d,\"start_line\":%d,\"count\":%d,\"lines\":[",
                         scrollback_size, scrollback_used, start_line, count);
    
    for (int i = 0; i < count && offset < (int)sizeof(result) - 100; i++) {
        char line_buffer[256];
        ssize_t line_len = session_get_scrollback_line(session, start_line + i, line_buffer, sizeof(line_buffer));
        
        if (line_len < 0) break;
        
        if (i > 0) {
            offset += snprintf(result + offset, sizeof(result) - offset, ",");
        }
        
        offset += snprintf(result + offset, sizeof(result) - offset, "\"");
        
        /* Escape line for JSON */
        for (ssize_t j = 0; j < line_len && offset < (int)sizeof(result) - 10; j++) {
            switch (line_buffer[j]) {
                case '\n': offset += snprintf(result + offset, sizeof(result) - offset, "\\n"); break;
                case '\r': offset += snprintf(result + offset, sizeof(result) - offset, "\\r"); break;
                case '\t': offset += snprintf(result + offset, sizeof(result) - offset, "\\t"); break;
                case '"': offset += snprintf(result + offset, sizeof(result) - offset, "\\\""); break;
                case '\\': offset += snprintf(result + offset, sizeof(result) - offset, "\\\\"); break;
                default:
                    if (line_buffer[j] >= 32) {
                        result[offset++] = line_buffer[j];
                    }
                    break;
            }
        }
        
        offset += snprintf(result + offset, sizeof(result) - offset, "\"");
    }
    
    snprintf(result + offset, sizeof(result) - offset, "]}");
    
    send_success(conn, result);
}