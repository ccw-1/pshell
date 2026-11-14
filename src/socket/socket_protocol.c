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
#include "pshell/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>

/* Socket expects run on a detached worker thread, so a longer cap is safe
 * (it does not stall the event loop). timeout <= 0 still means "wait
 * forever" in the engine, which would pin the session's expect slot and the
 * worker thread indefinitely, so it is rejected. */
#define SOCKET_EXPECT_TIMEOUT_MAX_MS 600000  /* 10 minutes */

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
static void handle_expect_pattern_incoming(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_get_scrollback(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);

static void handle_get_lines(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);
static void handle_attach_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr);


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

    /* Write length prefix with retry on partial write */
    size_t total = 0;
    while (total < sizeof(len_net)) {
        ssize_t w = write(fd, (char*)&len_net + total, sizeof(len_net) - total);
        if (w <= 0) {
            log_error("Failed to send response length: %s", strerror(errno));
            return;
        }
        total += (size_t)w;
    }

    /* Write response body with retry on partial write */
    total = 0;
    while (total < len) {
        ssize_t w = write(fd, response + total, len - total);
        if (w <= 0) {
            log_error("Failed to send response: %s", strerror(errno));
            return;
        }
        total += (size_t)w;
    }
}

static void send_error(socket_connection_t *conn, const char *message) {
    char response[1024];
    size_t pos = 0;
    
    pos += snprintf(response + pos, sizeof(response) - pos,
                    "{\"status\":\"error\",\"error\":{\"message\":\"");
    
    /* Escape message for JSON */
    if (message) {
        for (const char *p = message; *p && pos < sizeof(response) - 8; p++) {
            char c = *p;
            switch (c) {
                case '"':  response[pos++] = '\\'; response[pos++] = '"';  break;
                case '\\': response[pos++] = '\\'; response[pos++] = '\\'; break;
                case '\n': response[pos++] = '\\'; response[pos++] = 'n';  break;
                case '\r': response[pos++] = '\\'; response[pos++] = 'r';  break;
                case '\t': response[pos++] = '\\'; response[pos++] = 't';  break;
                default:
                    if ((unsigned char)c < 0x20) {
                        int n = snprintf(response + pos, sizeof(response) - pos, "\\u%04x", (unsigned char)c);
                        if (n < 0 || (size_t)n >= sizeof(response) - pos) {
                            pos = sizeof(response) - 1;
                            response[pos] = '\0';
                            goto send_it;
                        }
                        pos += (size_t)n;
                    } else {
                        response[pos++] = c;
                    }
                    break;
            }
        }
    }
    
send_it:
    pos += snprintf(response + pos, sizeof(response) - pos, "\"}}");
    send_response(conn, response);
}

static void send_success(socket_connection_t *conn, const char *result) {
    /* Sized from the result: a UTF-8 screen snapshot can exceed any small
     * fixed buffer (3-4 bytes per cell). */
    const char *body = result ? result : "{}";
    static const char prefix[] = "{\"status\":\"success\",\"result\":";
    size_t cap = sizeof(prefix) + strlen(body) + 2;
    char *response = malloc(cap);
    if (!response) {
        send_error(conn, "Out of memory");
        return;
    }
    int n = snprintf(response, cap, "%s%s}", prefix, body);
    if (n < 0 || (size_t)n >= cap) {
        free(response);
        send_error(conn, "Response too large");
        return;
    }
    send_response(conn, response);
    free(response);
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
    } else if (strcmp(method, "get_lines") == 0) {
        handle_get_lines(conn, params, mgr);
    } else if (strcmp(method, "resize_session") == 0) {
        handle_resize_session(conn, params, mgr);
    } else if (strcmp(method, "attach_session") == 0) {
        handle_attach_session(conn, params, mgr);
    } else if (strcmp(method, "expect_pattern") == 0) {
        handle_expect_pattern(conn, params, mgr);
    } else if (strcmp(method, "expect_pattern_incoming") == 0) {
        handle_expect_pattern_incoming(conn, params, mgr);
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
    
    /* Validate dimensions: unbounded values overflow the int multiplication
     * in screen_buffer_create (rows*cols), producing a tiny allocation and
     * heap corruption, or request gigabytes of memory. */
    if (rows < 1 || rows > 512 || cols < 1 || cols > 1024 ||
        scrollback_lines < 0 || scrollback_lines > 100000) {
        send_error(conn, "Invalid rows (1-512), cols (1-1024), or scrollback_lines (0-100000)");
        return;
    }
    
    /* Use default shell if command not specified */
    if (!command) {
        command = get_default_shell();
        if (!command) {
            send_error(conn, "No command specified and no default shell available");
            return;
        }
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
    
    /* Build JSON array. Budget leaves room for send_success()'s wrapper
     * ({"status":"success","result":...}) inside its own 8192 buffer. */
    char result[8160];
    size_t len = 0;
    size_t cap = sizeof(result);
    int n;
    
    n = snprintf(result, cap, "{\"sessions\":[");
    len = (n > 0) ? (size_t)n : 0;
    
    for (int i = 0; i < count; i++) {
        /* snprintf returns the would-be length; once it >= remaining space
         * the output is truncated and len must not grow further (otherwise
         * cap - len underflows and the next write goes out of bounds). */
        size_t rem = (len < cap) ? cap - len : 0;
        size_t before = len;
        n = snprintf(result + len, rem,
                     "%s{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\"}",
                     i > 0 ? "," : "",
                     sessions[i].id,
                     sessions[i].command,
                     sessions[i].rows,
                     sessions[i].cols,
                     sessions[i].pid,
                     state_to_string(sessions[i].state));
        if (n < 0 || (size_t)n >= rem) {
            log_warn("list_sessions response truncated at %d sessions", i);
            len = before;  /* Roll back to last good position */
            break;
        }
        len += (size_t)n;
    }
    
    {
        size_t rem = (len < cap) ? cap - len : 0;
        n = snprintf(result + len, rem, "],\"count\":%d}", count);
        if (n > 0 && (size_t)n < rem) {
            len += (size_t)n;
        } else {
            /* No room for the terminator: force a valid (truncated) close.
             * "]}" + NUL needs 3 bytes, so the tail must start at cap-3. */
            static const char close_json[] = "]}";
            size_t tail = (cap - 3 < len) ? cap - 3 : len;
            memcpy(result + tail, close_json, 3);
            len = tail + 2;
        }
    }
    result[len < cap ? len : cap - 1] = '\0';
    
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
    
    /* Get total lines written */
    session_line_no_t total_lines = session_get_total_lines(session);
    
    /* Build response */
    char result[512];
    snprintf(result, sizeof(result),
             "{\"id\":\"%s\",\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"pid\":%d,\"state\":\"%s\",\"total_lines\":%llu}",
             info.id, info.command, info.rows, info.cols, info.pid, state_to_string(info.state), 
             (unsigned long long)total_lines);
    
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
    
    /* Never free a session out from under an active expect worker */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    if (session_expect_active(session)) {
        send_error(conn, "Expect in progress for this session");
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
                if ((unsigned char)output[i] >= 32) {
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
    
    /* Get screen: the text is UTF-8, up to UTF8_MAX_BYTES per cell plus a
     * newline per row */
    session_info_t info;
    if (!session_get_info(session, &info) || info.rows <= 0 || info.cols <= 0) {
        send_error(conn, "Session not available");
        return;
    }
    size_t screen_cap = (size_t)info.rows * ((size_t)info.cols * UTF8_MAX_BYTES + 1) + 1;
    char *screen = malloc(screen_cap);
    /* JSON escaping at most doubles a byte (\n, \", \\) */
    size_t result_cap = screen_cap * 2 + 128;
    char *result = malloc(result_cap);
    if (!screen || !result) {
        free(screen);
        free(result);
        send_error(conn, "Out of memory");
        return;
    }
    size_t bytes = session_get_screen_text(session, screen, screen_cap - 1);
    screen[bytes] = '\0';
    
    /* Get cursor position */
    int cursor_row, cursor_col;
    session_get_cursor_position(session, &cursor_row, &cursor_col);
    
    /* Build response */
    size_t offset = (size_t)snprintf(result, result_cap, "{\"text\":\"");
    
    for (size_t i = 0; i < bytes && offset < result_cap - 128; i++) {
        switch (screen[i]) {
            case '\n': result[offset++] = '\\'; result[offset++] = 'n'; break;
            case '\r': result[offset++] = '\\'; result[offset++] = 'r'; break;
            case '\t': result[offset++] = '\\'; result[offset++] = 't'; break;
            case '"':  result[offset++] = '\\'; result[offset++] = '"'; break;
            case '\\': result[offset++] = '\\'; result[offset++] = '\\'; break;
            default:
                /* Control characters are dropped; bytes >= 0x80 are UTF-8 */
                if ((unsigned char)screen[i] >= 32) {
                    result[offset++] = screen[i];
                }
                break;
        }
    }
    
    snprintf(result + offset, result_cap - offset,
             "\",\"cursor_row\":%d,\"cursor_col\":%d,\"bytes\":%zu}",
             cursor_row, cursor_col, bytes);
    
    send_success(conn, result);
    free(screen);
    free(result);
}

static void handle_resize_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    int rows = json_object_get_int(params, "rows", 0);
    int cols = json_object_get_int(params, "cols", 0);
    
    if (!session_id || rows <= 0 || rows > 512 || cols <= 0 || cols > 1024) {
        send_error(conn, "Invalid parameters (rows 1-512, cols 1-1024)");
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

/* Interactive attach: after the normal length-prefixed success reply the
 * connection stops being request/response and becomes a raw byte stream
 * owned by the session (client input -> PTY, PTY output -> client). */
static void handle_attach_session(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Optional: size the session to the attaching terminal */
    int rows = json_object_get_int(params, "rows", 0);
    int cols = json_object_get_int(params, "cols", 0);
    if (rows > 0 && rows <= 512 && cols > 0 && cols <= 1024) {
        session_resize(session, rows, cols);
    }
    
    int fd = socket_connection_get_fd(conn);
    if (!session_attach_client(session, fd)) {
        send_error(conn, "Failed to attach (session dead or too many terminals attached)");
        return;
    }
    
    /* The session owns the fd from here on */
    socket_connection_detach(conn);
    
    char result[128];
    snprintf(result, sizeof(result), "{\"id\":\"%s\",\"attached\":true}", session_id);
    send_success(conn, result);
    session_attach_redraw(session, fd);
}

/* ------------------------------------------------------------------ */
/* Asynchronous expect handling                                        */
/*                                                                    */
/* expect_pattern() blocks until the pattern matches or the timeout    */
/* expires. Running it directly in the event loop would stall PTY     */
/* reads, so output produced while waiting is never observed (the     */
/* classic expect race). Instead the wait runs on a detached worker    */
/* thread: the event loop keeps servicing PTYs, the PTY callback      */
/* appends new output to the expect buffer, and the worker matches    */
/* against it. The connection fd is detached and owned by the worker,  */
/* which writes the response and closes the fd.                       */
/* ------------------------------------------------------------------ */

/* Append a JSON-escaped copy of s to out */
static void append_json_escaped(char *out, size_t out_size, size_t *pos, const char *s) {
    if (!out || *pos >= out_size - 1) return;
    if (!s) s = "";
    for (const char *p = s; *p && *pos < out_size - 8; p++) {
        char c = *p;
        switch (c) {
            case '"':  out[(*pos)++] = '\\'; out[(*pos)++] = '"';  break;
            case '\\': out[(*pos)++] = '\\'; out[(*pos)++] = '\\'; break;
            case '\n': out[(*pos)++] = '\\'; out[(*pos)++] = 'n';  break;
            case '\r': out[(*pos)++] = '\\'; out[(*pos)++] = 'r';  break;
            case '\t': out[(*pos)++] = '\\'; out[(*pos)++] = 't';  break;
            default:
                if ((unsigned char)c < 0x20) {
                    int n = snprintf(out + *pos, out_size - *pos, "\\u%04x", (unsigned char)c);
                    if (n < 0 || (size_t)n >= out_size - *pos) {
                        *pos = out_size - 1;
                        out[*pos] = '\0';
                        return;
                    }
                    *pos += (size_t)n;
                } else {
                    out[(*pos)++] = c;
                }
                break;
        }
    }
    out[*pos] = '\0';
}

/* Write a length-prefixed response to a raw fd (worker-thread side) */
static void send_fd_response(int fd, const char *response) {
    size_t len = strlen(response);
    uint32_t len_net = htonl((uint32_t)len);

    /* Write length prefix with retry on partial write */
    size_t total = 0;
    while (total < sizeof(len_net)) {
        ssize_t w = write(fd, (char*)&len_net + total, sizeof(len_net) - total);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            log_error("expect worker: failed to write response length: %s", strerror(errno));
            return;
        }
        total += (size_t)w;
    }

    /* Write response body with retry on partial write */
    total = 0;
    while (total < len) {
        ssize_t n = write(fd, response + total, len - total);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            log_error("expect worker: failed to write response: %s", strerror(errno));
            return;
        }
        total += (size_t)n;
    }
}

/* Cancellation probe: true when the client has gone away */
static bool expect_client_gone(void *user_data) {
    int fd = (int)(intptr_t)user_data;
    char c;
    ssize_t n = recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return true;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return true;
    return false;
}

typedef struct {
    int fd;
    session_t *session;
    expect_context_t *ctx;
    char pattern[512];
    bool case_sensitive;
} expect_job_t;

static void *expect_worker_thread(void *arg) {
    expect_job_t *job = (expect_job_t *)arg;
    
    match_result_t match;
    memset(&match, 0, sizeof(match));
    expect_result_t result = expect_pattern(job->ctx, job->pattern, job->case_sensitive, &match);
    
    /* Build the response while the expect is still active so the session
     * (cursor position) can be read safely; after expect_context_destroy()
     * the session may be freed by another thread. */
    char response[4096];
    if (result == EXPECT_MATCHED) {
        const char *buffer = expect_get_buffer(job->ctx);
        int matched_line = 0;
        for (size_t i = 0; i < match.start && buffer && buffer[i]; i++) {
            if (buffer[i] == '\n') matched_line++;
        }
        
        int cursor_row = 0, cursor_col = 0;
        session_get_cursor_position(job->session, &cursor_row, &cursor_col);
        int relative_line = matched_line - cursor_row;
        
        size_t pos = 0;
        int prefix_len = snprintf(response, sizeof(response),
                                  "{\"found\":true,\"matched_text\":\"");
        if (prefix_len > 0) pos = (size_t)prefix_len;
        append_json_escaped(response, sizeof(response), &pos,
                            match.matched_text ? match.matched_text : "");
        snprintf(response + pos, sizeof(response) - pos,
                 "\",\"start\":%zu,\"end\":%zu,\"line\":%d}",
                 match.start, match.end, relative_line);
        match_result_free(&match);
    } else if (result == EXPECT_TIMEOUT) {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"timeout\"}");
    } else if (result == EXPECT_EOF) {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"eof\"}");
    } else {
        snprintf(response, sizeof(response), "{\"found\":false,\"reason\":\"error\"}");
    }
    
    /* Deactivates the session expect buffer; the session is no longer ours */
    expect_context_destroy(job->ctx);
    
    /* Same envelope the synchronous handlers use */
    char envelope[4160];
    snprintf(envelope, sizeof(envelope), "{\"status\":\"success\",\"result\":%s}", response);
    
    send_fd_response(job->fd, envelope);
    fsync(job->fd);
    shutdown(job->fd, SHUT_WR);
    close(job->fd);
    
    free(job);
    return NULL;
}

static void handle_expect_common(socket_connection_t *conn, json_value_t *params,
                                 session_manager_t *mgr, bool incoming_only) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, incoming_only ? "session_id" : "id");
    const char *pattern = json_object_get_string(params, "pattern");
    int timeout = json_object_get_int(params, "timeout", incoming_only ? 5000 : 30000);
    bool case_sensitive = json_object_get_bool(params, "case_sensitive", true);
    
    log_debug("handle_expect_pattern: session_id=%s, pattern=%s, timeout=%d, incoming_only=%d",
              session_id ? session_id : "NULL", pattern ? pattern : "NULL", timeout, incoming_only);
    
    if (!session_id || !pattern) {
        send_error(conn, "Missing session id or pattern");
        return;
    }
    
    /* Reject an unbounded wait (timeout <= 0 means "forever" in the engine)
     * and cap the maximum so the worker thread and expect slot cannot be
     * pinned indefinitely. */
    if (timeout <= 0 || timeout > SOCKET_EXPECT_TIMEOUT_MAX_MS) {
        char err[128];
        snprintf(err, sizeof(err), "timeout must be between 1 and %d ms",
                 SOCKET_EXPECT_TIMEOUT_MAX_MS);
        send_error(conn, err);
        return;
    }
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* One expect per session: the expect buffer is shared, and session
     * destruction is refused while an expect is active. */
    if (session_expect_active(session)) {
        send_error(conn, "Expect already in progress for this session");
        return;
    }
    
    /* Create expect context (seeds from the screen, or clears for incoming-only).
     * Runs on the event loop thread; activation is what guards the session. */
    expect_context_t *ctx = expect_context_create(session, timeout,
                                                  incoming_only ? EXPECT_FLAG_SKIP_SEEDING
                                                                : EXPECT_FLAG_NONE);
    if (!ctx) {
        send_error(conn, "Failed to create expect context");
        return;
    }
    
    expect_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        expect_context_destroy(ctx);
        send_error(conn, "Out of memory");
        return;
    }
    
    job->fd = socket_connection_get_fd(conn);
    job->session = session;
    job->ctx = ctx;
    job->case_sensitive = case_sensitive;
    strncpy(job->pattern, pattern, sizeof(job->pattern) - 1);
    
    expect_set_cancel_check(ctx, expect_client_gone, (void *)(intptr_t)job->fd);
    
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    if (pthread_create(&tid, &attr, expect_worker_thread, job) != 0) {
        pthread_attr_destroy(&attr);
        expect_context_destroy(ctx);
        free(job);
        send_error(conn, "Failed to start expect worker");
        return;
    }
    pthread_attr_destroy(&attr);
    
    /* The worker owns the fd (response + close) from here on */
    socket_connection_detach(conn);
    
    log_info("Expect started for session %s (pattern=%s, timeout=%d, incoming_only=%d)",
             session_id, pattern, timeout, incoming_only);
}

static void handle_expect_pattern(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    handle_expect_common(conn, params, mgr, false);
}

static void handle_expect_pattern_incoming(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    handle_expect_common(conn, params, mgr, true);
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
    
    /* Limit count to available lines (avoid integer overflow) */
    if (count > scrollback_used - start_line) {
        count = scrollback_used - start_line;
    }
    
    /* Build response with lines array */
    char result[8192];
    int offset = snprintf(result, sizeof(result), 
                         "{\"scrollback_size\":%d,\"scrollback_used\":%d,\"start_line\":%d,\"count\":%d,\"lines\":[",
                         scrollback_size, scrollback_used, start_line, count);
    
    for (int i = 0; i < count && offset < (int)sizeof(result) - 100; i++) {
        char line_buffer[4096];
        ssize_t line_len = session_get_scrollback_line(session, start_line + i, line_buffer, sizeof(line_buffer));
        
        if (line_len < 0) break;
        
        /* Check if line was truncated */
        if (line_len >= (ssize_t)sizeof(line_buffer)) {
            /* Line too long - skip or send error */
            log_warn("Scrollback line %d truncated (%zd bytes)", start_line + i, line_len);
            /* Continue with truncated line */
        }

        if (i > 0) {
            int n = snprintf(result + offset, sizeof(result) - offset, ",");
            if (n < 0 || (size_t)n >= sizeof(result) - offset) break;
            offset += (size_t)n;
        }

        int n = snprintf(result + offset, sizeof(result) - offset, "\"");
        if (n < 0 || (size_t)n >= sizeof(result) - offset) break;
        offset += (size_t)n;

        /* Escape line for JSON */
        for (ssize_t j = 0; j < line_len && offset < (int)sizeof(result) - 10; j++) {
            switch (line_buffer[j]) {
                case '\n': offset += snprintf(result + offset, sizeof(result) - offset, "\\n"); break;
                case '\r': offset += snprintf(result + offset, sizeof(result) - offset, "\\r"); break;
                case '\t': offset += snprintf(result + offset, sizeof(result) - offset, "\\t"); break;
                case '"': offset += snprintf(result + offset, sizeof(result) - offset, "\\\""); break;
                case '\\': offset += snprintf(result + offset, sizeof(result) - offset, "\\\\"); break;
                default:
                    if ((unsigned char)line_buffer[j] >= 32) {
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

static void handle_get_lines(socket_connection_t *conn, json_value_t *params, session_manager_t *mgr) {
    if (!params) {
        send_error(conn, "Missing params");
        return;
    }
    
    const char *session_id = json_object_get_string(params, "id");
    if (!session_id) {
        send_error(conn, "Missing session id");
        return;
    }
    
    /* Get line range parameters */
    int start_line = json_object_get_int(params, "start_line", 0);
    int end_line = json_object_get_int(params, "end_line", -1);
    
    /* Get session */
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        send_error(conn, "Session not found");
        return;
    }
    
    /* Get total lines */
    session_line_no_t total_lines = session_get_total_lines(session);
    
    /* Validate parameters */
    if (start_line < 1) {
        send_error(conn, "start_line must be >= 1");
        return;
    }
    
    if (end_line < start_line) {
        send_error(conn, "end_line must be >= start_line");
        return;
    }
    
    if (start_line > (int)total_lines) {
        send_error(conn, "start_line exceeds total lines");
        return;
    }
    
    /* Read line range */
    size_t content_len = 0;
    char *content = session_read_line_range(session, start_line, end_line, &content_len);
    
    if (!content) {
        send_error(conn, "Failed to read line range");
        return;
    }
    
    /* Build response - need to escape content for JSON */
    size_t result_capacity = content_len * 6 + 1024;  /* Extra space for escaping */
    char *result = malloc(result_capacity);
    if (!result) {
        free(content);
        send_error(conn, "Out of memory");
        return;
    }
    
    int offset = snprintf(result, result_capacity,
                         "{\"total_lines\":%llu,\"start_line\":%d,\"end_line\":%d,\"content\":\"",
                         (unsigned long long)total_lines, start_line, 
                         end_line > (int)total_lines ? (int)total_lines : end_line);
    
    /* Escape content for JSON */
    for (size_t i = 0; i < content_len && offset < (int)result_capacity - 10; i++) {
        switch (content[i]) {
            case '\n': offset += snprintf(result + offset, result_capacity - offset, "\\n"); break;
            case '\r': offset += snprintf(result + offset, result_capacity - offset, "\\r"); break;
            case '\t': offset += snprintf(result + offset, result_capacity - offset, "\\t"); break;
            case '"': offset += snprintf(result + offset, result_capacity - offset, "\\\""); break;
            case '\\': offset += snprintf(result + offset, result_capacity - offset, "\\\\"); break;
            default:
                if ((unsigned char)content[i] >= 32) {
                    result[offset++] = content[i];
                }
                break;
        }
    }
    
    snprintf(result + offset, result_capacity - offset, "\"}");
    
    send_success(conn, result);
    
    free(content);
    free(result);
}