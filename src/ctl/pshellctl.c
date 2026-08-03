/*
 * pshellctl - Unix socket control utility for pshell daemon
 * 
 * Provides local control interface via Unix domain socket.
 * No authentication required (uses file permissions).
 */

#include "pshell/socket_client.h"
#include "pshell/json_simple.h"
#include "pshell/logger.h"
#include "pshell/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>

#define DEFAULT_SOCKET_PATH "/tmp/pshell.sock"

/* Forward declarations */
static void print_usage(const char *prog);
static int cmd_create(socket_client_t *client, int argc, char **argv);
static int cmd_list(socket_client_t *client, int argc, char **argv);
static int cmd_info(socket_client_t *client, int argc, char **argv);
static int cmd_send(socket_client_t *client, int argc, char **argv);
static int cmd_output(socket_client_t *client, int argc, char **argv);
static int cmd_screen(socket_client_t *client, int argc, char **argv);
static int cmd_resize(socket_client_t *client, int argc, char **argv);
static int cmd_expect(socket_client_t *client, int argc, char **argv);
static int cmd_expect_incoming(socket_client_t *client, int argc, char **argv);
static int cmd_scrollback(socket_client_t *client, int argc, char **argv);

static int cmd_destroy(socket_client_t *client, int argc, char **argv);
static int cmd_status(socket_client_t *client, int argc, char **argv);
static int cmd_generate_key(int argc, char **argv);

static void print_usage(const char *prog) {
    printf("Usage: %s [options] <command> [args...]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -s <path>    Socket path (default: %s)\n", DEFAULT_SOCKET_PATH);
    printf("               Can also be set via PSHELL_SOCKET_PATH environment variable\n");
    printf("               Priority: -s flag > PSHELL_SOCKET_PATH > default\n");
    printf("  -h           Show this help\n");
    printf("\n");
    printf("Commands:\n");
    printf("  create [--command <cmd>] [--rows <n>] [--cols <n>] [--scrollback <lines>] [--output <file>] [--rotate n:m]\n");
    printf("      Create new session\n");
    printf("      --scrollback: Number of scrollback lines (default: 1000)\n");
    printf("      --output: Log all output to file (append mode, raw PTY data)\n");
    printf("      --rotate: Rotate log files (n=count, m=lines per file, e.g., 4:1000)\n");
    printf("                Creates files: output.0, output.1, output.2, output.3\n");
    printf("\n");
    printf("  list\n");
    printf("      List all sessions\n");
    printf("\n");
    printf("  info <session-id>\n");
    printf("      Get session information\n");
    printf("\n");
    printf("  send <session-id> <text>\n");
    printf("      Send text to session with escape sequence support:\n");
    printf("        Standard: \\n \\r \\t \\e (ESC) \\cC (Ctrl-C)\n");
    printf("        Function keys: \\F1 through \\F12\n");
    printf("        Arrow keys: \\UP \\DOWN \\LEFT \\RIGHT\n");
    printf("        Special: \\HOME \\END \\INS \\DEL \\PGUP \\PGDN\n");
    printf("        Shift+keys: \\SHIFT_UP \\SHIFT_F1 \\SHIFT_TAB\n");
    printf("        Numeric: \\x03 (hex) \\033 (octal)\n");
    printf("\n");
    printf("  expect <session-id> <pattern> <timeout-ms>\n");
    printf("      Wait for pattern in output (matches historical + new data)\n");
    printf("\n");
    printf("  expect-incoming <session-id> <pattern> <timeout-ms>\n");
    printf("      Wait for pattern in NEW output only (ignores historical data)\n");
    printf("\n");
    printf("  output <session-id>\n");
    printf("      Read output from session\n");
    printf("\n");
    printf("  screen <session-id>\n");
    printf("      Get screen state\n");
    printf("\n");
    printf("  scrollback <session-id> [--start <n>] [--count <n>]\n");
    printf("      Get scrollback buffer lines (terminal history)\n");
    printf("      --start: Starting line index (default: 0)\n");
    printf("      --count: Number of lines to retrieve (default: 100)\n");
    printf("\n");
    printf("  resize <session-id> <rows> <cols>\n");
    printf("      Resize terminal\n");
    printf("\n");
    printf("  expect <session-id> <pattern> [timeout_ms]\n");
    printf("      Wait for pattern match\n");
    printf("\n");
    printf("  destroy <session-id>\n");
    printf("      Destroy session\n");
    printf("\n");
    printf("  status\n");
    printf("      Get daemon status\n");
    printf("\n");
    printf("  generate-key [--length <n>]\n");
    printf("      Generate a cryptographically secure API key\n");
    printf("      --length: Key length in bytes (default: 32, min: 16, max: 64)\n");
    printf("      Output: Base64-encoded random key suitable for [auth] api_keys config\n");
    printf("\n");
}

static int cmd_create(socket_client_t *client, int argc, char **argv) {
    const char *command = "bash";
    int rows = 24;
    int cols = 80;
    int scrollback_lines = 0;  /* 0 = use default (1000) */
    const char *output_file = NULL;
    int rotate_count = 0;
    int rotate_lines = 0;
    
    /* Parse arguments */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--command") == 0 && i + 1 < argc) {
            command = argv[++i];
        } else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            rows = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
            cols = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scrollback") == 0 && i + 1 < argc) {
            scrollback_lines = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--rotate") == 0 && i + 1 < argc) {
            /* Parse rotate format: n:m (count:lines) */
            const char *rotate_spec = argv[++i];
            if (sscanf(rotate_spec, "%d:%d", &rotate_count, &rotate_lines) != 2) {
                fprintf(stderr, "Invalid --rotate format. Use: n:m (e.g., 4:1000)\n");
                return 1;
            }
            if (rotate_count <= 0 || rotate_lines <= 0) {
                fprintf(stderr, "Invalid --rotate values. Both n and m must be positive.\n");
                return 1;
            }
        }
    }
    
    /* Build request */
    char request[1024];
    if (output_file) {
        if (rotate_count > 0 && rotate_lines > 0) {
            snprintf(request, sizeof(request),
                     "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d,\"output_file\":\"%s\",\"rotate_count\":%d,\"rotate_lines\":%d}}",
                     command, rows, cols, scrollback_lines, output_file, rotate_count, rotate_lines);
        } else {
            snprintf(request, sizeof(request),
                     "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d,\"output_file\":\"%s\"}}",
                     command, rows, cols, scrollback_lines, output_file);
        }
    } else {
        snprintf(request, sizeof(request),
                 "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d}}",
                 command, rows, cols, scrollback_lines);
    }
    
    /* Send request */
    char response[4096];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            const char *id = json_object_get_string(result, "id");
            if (id) {
                printf("%s\n", id);
            }
        }
    } else {
        json_value_t *error = json_object_get(root, "error");
        if (error) {
            const char *message = json_object_get_string(error, "message");
            if (message) {
                fprintf(stderr, "Error: %s\n", message);
            }
        }
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_list(socket_client_t *client, int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Build request */
    const char *request = "{\"method\":\"list_sessions\"}";
    
    /* Send request */
    char response[8192];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            json_value_t *sessions = json_object_get(result, "sessions");
            int count = json_object_get_int(result, "count", 0);
            
            printf("Sessions (%d):\n", count);
            
            if (sessions && json_get_type(sessions) == JSON_ARRAY) {
                size_t array_size = json_array_size(sessions);
                for (size_t i = 0; i < array_size; i++) {
                    json_value_t *session = json_array_get(sessions, i);
                    const char *id = json_object_get_string(session, "id");
                    const char *cmd = json_object_get_string(session, "command");
                    int rows = json_object_get_int(session, "rows", 0);
                    int cols = json_object_get_int(session, "cols", 0);
                    int pid = json_object_get_int(session, "pid", 0);
                    const char *state = json_object_get_string(session, "state");
                    
                    printf("  ID: %s\n", id ? id : "unknown");
                    printf("  Command: %s\n", cmd ? cmd : "unknown");
                    printf("  Size: %dx%d\n", rows, cols);
                    printf("  PID: %d\n", pid);
                    printf("  State: %s\n", state ? state : "unknown");
                    if (i < array_size - 1) {
                        printf("\n");
                    }
                }
            }
        }
    } else {
        fprintf(stderr, "Error listing sessions\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_info(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: info <session-id>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    /* Build request */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_session\",\"params\":{\"id\":\"%s\"}}",
             session_id);
    
    /* Send request */
    char response[4096];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse and display response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            const char *id = json_object_get_string(result, "id");
            const char *cmd = json_object_get_string(result, "command");
            int rows = json_object_get_int(result, "rows", 0);
            int cols = json_object_get_int(result, "cols", 0);
            int pid = json_object_get_int(result, "pid", 0);
            const char *state = json_object_get_string(result, "state");
            
            printf("Session Information:\n");
            printf("  ID: %s\n", id ? id : "unknown");
            printf("  Command: %s\n", cmd ? cmd : "unknown");
            printf("  Size: %dx%d\n", rows, cols);
            printf("  PID: %d\n", pid);
            printf("  State: %s\n", state ? state : "unknown");
        }
    } else {
        fprintf(stderr, "Session not found\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_send(socket_client_t *client, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: send <session-id> <text>\n");
        fprintf(stderr, "Escape sequences: \\n \\r \\t \\cC (Ctrl-C) \\x03 \\033 etc.\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    /* Process escape sequences in text */
    char text_buf[4096];
    strncpy(text_buf, argv[1], sizeof(text_buf) - 1);
    text_buf[sizeof(text_buf) - 1] = '\0';
    str_unescape(text_buf);
    
    /* Build request */
    char request[4096];
    snprintf(request, sizeof(request),
             "{\"method\":\"send_text\",\"params\":{\"id\":\"%s\",\"text\":\"%s\"}}",
             session_id, text_buf);
    
    /* Send request */
    char response[1024];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            int bytes = json_object_get_int(result, "bytes", 0);
            printf("Sent %d bytes\n", bytes);
        }
    } else {
        fprintf(stderr, "Failed to send text\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_output(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: output <session-id>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    /* Build request */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_output\",\"params\":{\"id\":\"%s\"}}",
             session_id);
    
    /* Send request */
    char response[8192];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            const char *output = json_object_get_string(result, "output");
            if (output) {
                printf("%s", output);
            }
        }
    } else {
        fprintf(stderr, "Failed to read output\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_screen(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: screen <session-id>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    /* Build request */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_screen\",\"params\":{\"id\":\"%s\"}}",
             session_id);
    
    /* Send request */
    char response[8192];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            const char *text = json_object_get_string(result, "text");
            int cursor_row = json_object_get_int(result, "cursor_row", 0);
            int cursor_col = json_object_get_int(result, "cursor_col", 0);
            
            if (text) {
                printf("%s", text);
            }
            printf("\n[Cursor at row %d, col %d]\n", cursor_row, cursor_col);
        }
    } else {
        fprintf(stderr, "Failed to get screen\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_resize(socket_client_t *client, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: resize <session-id> <rows> <cols>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    int rows = atoi(argv[1]);
    int cols = atoi(argv[2]);
    
    /* Build request */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"resize_session\",\"params\":{\"id\":\"%s\",\"rows\":%d,\"cols\":%d}}",
             session_id, rows, cols);
    
    /* Send request */
    char response[1024];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        printf("Resized to %dx%d\n", rows, cols);
    } else {
        fprintf(stderr, "Failed to resize\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_scrollback(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: scrollback <session-id> [--start <n>] [--count <n>]\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    int start_line = 0;
    int count = 100;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start_line = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
        }
    }
    
    /* Build request */
    char request[1024];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_scrollback\",\"params\":{\"id\":\"%s\",\"start_line\":%d,\"count\":%d}}",
             session_id, start_line, count);
    
    /* Send request */
    char response[16384];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            int scrollback_size = json_object_get_int(result, "scrollback_size", 0);
            int scrollback_used = json_object_get_int(result, "scrollback_used", 0);
            int ret_start = json_object_get_int(result, "start_line", 0);
            int ret_count = json_object_get_int(result, "count", 0);
            
            printf("Scrollback: %d/%d lines used\n", scrollback_used, scrollback_size);
            printf("Showing lines %d-%d:\n\n", ret_start, ret_start + ret_count - 1);
            
            json_value_t *lines = json_object_get(result, "lines");
            if (lines && lines->type == JSON_ARRAY) {
                size_t line_count = json_array_size(lines);
                for (size_t i = 0; i < line_count; i++) {
                    json_value_t *line = json_array_get(lines, i);
                    if (line && line->type == JSON_STRING) {
                        const char *line_text = json_get_string(line);
                        if (line_text) {
                            printf("%s\n", line_text);
                        }
                    }
                }
            }
        }
    } else {
        json_value_t *error = json_object_get(root, "error");
        if (error) {
            const char *message = json_object_get_string(error, "message");
            if (message) {
                fprintf(stderr, "Error: %s\n", message);
            }
        }
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_expect_incoming(socket_client_t *client, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: pshellctl expect-incoming <session-id> <pattern> <timeout-ms>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    const char *pattern = argv[1];
    int timeout = atoi(argv[2]);
    
    /* Build request */
    char request[4096];
    snprintf(request, sizeof(request),
            "{\"method\":\"expect_pattern_incoming\",\"params\":{\"session_id\":\"%s\",\"pattern\":\"%s\",\"timeout\":%d,\"case_sensitive\":true}}",
            session_id, pattern, timeout);
    
    /* Send request */
    char response[8192];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            bool found = json_object_get_bool(result, "found", false);
            if (found) {
                const char *matched_text = json_object_get_string(result, "matched_text");
                int line = json_object_get_int(result, "line", 0);
                if (matched_text) {
                    printf("Matched: \"%s\" at %d (incoming-only)\n", matched_text, line);
                } else {
                    printf("Pattern matched at %d (incoming-only)\n", line);
                }
            } else {
                const char *reason = json_object_get_string(result, "reason");
                printf("No match (%s)\n", reason ? reason : "unknown");
            }
        }
    } else {
        fprintf(stderr, "Expect failed\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_expect(socket_client_t *client, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: expect <session-id> <pattern> [timeout_ms]\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    const char *pattern = argv[1];
    int timeout = argc > 2 ? atoi(argv[2]) : 5000;
    
    /* Build request */
    char request[1024];
    snprintf(request, sizeof(request),
             "{\"method\":\"expect_pattern\",\"params\":{\"id\":\"%s\",\"pattern\":\"%s\",\"timeout\":%d}}",
             session_id, pattern, timeout);
    
    /* Send request */
    char response[8192];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            bool found = json_object_get_bool(result, "found", false);
            if (found) {
                const char *matched_text = json_object_get_string(result, "matched_text");
                int line = json_object_get_int(result, "line", 0);
                if (matched_text) {
                    printf("Matched: \"%s\" at %d\n", matched_text, line);
                } else {
                    printf("Pattern matched at %d\n", line);
                }
            } else {
                const char *reason = json_object_get_string(result, "reason");
                printf("No match (%s)\n", reason ? reason : "unknown");
            }
        }
    } else {
        fprintf(stderr, "Expect failed\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_destroy(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: destroy <session-id>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    /* Build request */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"destroy_session\",\"params\":{\"id\":\"%s\"}}",
             session_id);
    
    /* Send request */
    char response[1024];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        printf("Session destroyed\n");
    } else {
        fprintf(stderr, "Failed to destroy session\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

static int cmd_status(socket_client_t *client, int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Build request */
    const char *request = "{\"method\":\"daemon_status\"}";
    
    /* Send request */
    char response[4096];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    const char *status_str = json_object_get_string(root, "status");
    if (status_str && strcmp(status_str, "success") == 0) {
        json_value_t *result = json_object_get(root, "result");
        if (result) {
            const char *daemon_status = json_object_get_string(result, "status");
            int sessions = json_object_get_int(result, "sessions", 0);
            int uptime = json_object_get_int(result, "uptime", 0);
            
            printf("Status: %s\n", daemon_status ? daemon_status : "unknown");
            printf("Sessions: %d\n", sessions);
            if (uptime > 0) {
                printf("Uptime: %ds\n", uptime);
            }
        }
    } else {
        fprintf(stderr, "Failed to get status\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 0;
}

/* Base64 encoding table */
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64 encode binary data */
static char* base64_encode(const unsigned char *data, size_t len) {
    size_t output_len = 4 * ((len + 2) / 3);
    char *encoded = malloc(output_len + 1);
    if (!encoded) return NULL;
    
    size_t i, j;
    for (i = 0, j = 0; i < len;) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        encoded[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 6) & 0x3F];
        encoded[j++] = base64_chars[triple & 0x3F];
    }
    
    /* Add padding */
    for (i = 0; i < (3 - len % 3) % 3; i++) {
        encoded[output_len - 1 - i] = '=';
    }
    
    encoded[output_len] = '\0';
    return encoded;
}

static int cmd_generate_key(int argc, char **argv) {
    int key_length = 32;  /* Default: 32 bytes = 256 bits */
    
    /* Parse arguments */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--length") == 0 && i + 1 < argc) {
            key_length = atoi(argv[i + 1]);
            if (key_length < 16 || key_length > 64) {
                fprintf(stderr, "Error: Key length must be between 16 and 64 bytes\n");
                return 1;
            }
            i++;
        }
    }
    
    /* Allocate buffer for random bytes */
    unsigned char *random_bytes = malloc(key_length);
    if (!random_bytes) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return 1;
    }
    
    /* Read from /dev/urandom for cryptographic randomness */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open /dev/urandom: %s\n", strerror(errno));
        free(random_bytes);
        return 1;
    }
    
    ssize_t bytes_read = read(fd, random_bytes, key_length);
    close(fd);
    
    if (bytes_read != key_length) {
        fprintf(stderr, "Error: Failed to read random bytes\n");
        free(random_bytes);
        return 1;
    }
    
    /* Base64 encode the random bytes */
    char *encoded_key = base64_encode(random_bytes, key_length);
    free(random_bytes);
    
    if (!encoded_key) {
        fprintf(stderr, "Error: Base64 encoding failed\n");
        return 1;
    }
    
    /* Output the key */
    printf("Generated API Key (%d bytes, %zu characters):\n", key_length, strlen(encoded_key));
    printf("%s\n", encoded_key);
    printf("\n");
    printf("Add to your pshell.conf:\n");
    printf("[auth]\n");
    printf("enabled = true\n");
    printf("api_keys = %s\n", encoded_key);
    printf("\n");
    printf("Use in REST API requests:\n");
    printf("curl -H \"X-API-Key: %s\" http://localhost:8080/api/sessions\n", encoded_key);
    
    free(encoded_key);
    return 0;
}

int main(int argc, char **argv) {
    const char *socket_path = DEFAULT_SOCKET_PATH;
    int opt;
    
    /* Check for environment variable override */
    const char *env_socket = getenv("PSHELL_SOCKET_PATH");
    if (env_socket && env_socket[0] != '\0') {
        socket_path = env_socket;
    }
    
    /* Parse options (command-line overrides environment variable) */
    while ((opt = getopt(argc, argv, "s:h")) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Check for command */
    if (optind >= argc) {
        fprintf(stderr, "Error: No command specified\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[optind];
    int cmd_argc = argc - optind - 1;
    char **cmd_argv = argv + optind + 1;
    
    /* Connect to socket */
    socket_client_t client;
    if (socket_client_connect(&client, socket_path) < 0) {
        fprintf(stderr, "Failed to connect to pshell daemon at %s\n", socket_path);
        fprintf(stderr, "Is the daemon running?\n");
        return 1;
    }
    
    /* Dispatch command */
    int result = 1;
    if (strcmp(command, "create") == 0) {
        result = cmd_create(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "list") == 0) {
        result = cmd_list(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "info") == 0) {
        result = cmd_info(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "send") == 0) {
        result = cmd_send(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "output") == 0) {
        result = cmd_output(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "screen") == 0) {
        result = cmd_screen(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "scrollback") == 0) {
        result = cmd_scrollback(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "resize") == 0) {
        result = cmd_resize(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "expect") == 0) {
        result = cmd_expect(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "expect-incoming") == 0) {
        result = cmd_expect_incoming(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "destroy") == 0) {
        result = cmd_destroy(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "status") == 0) {
        result = cmd_status(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "generate-key") == 0) {
        /* generate-key doesn't need socket connection */
        socket_client_disconnect(&client);
        return cmd_generate_key(cmd_argc, cmd_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        result = 1;
    }
    
    /* Cleanup */
    socket_client_disconnect(&client);
    
    return result;
}