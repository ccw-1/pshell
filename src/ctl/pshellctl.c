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
#include "pshell/version.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include <limits.h>

#define DEFAULT_SOCKET_PATH "/tmp/pshell.sock"

/* Safe atoi replacement: returns 0 on error and sets errno */
static int safe_atoi(const char *str, int *out) {
    if (!str) return 0;
    errno = 0;
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0' || val < INT_MIN || val > INT_MAX) {
        return 0;
    }
    *out = (int)val;
    return 1;
}

/* Return a malloc'd JSON-string-escaped copy of s, or NULL on allocation failure */
static char *json_escape_dup(const char *s) {
    char *out = malloc(strlen(s) * 6 + 1);
    if (out) str_json_escape(out, strlen(s) * 6 + 1, s);
    return out;
}

/* Return a malloc'd copy of a pattern escaped for a JSON string. Backslash pairs
 * are kept (patterns have always been passed as JSON string text, e.g. "\\$ "),
 * so only quotes and control characters are escaped. */
static char *json_escape_pattern_dup(const char *s) {
    size_t cap = strlen(s) * 6 + 1;
    char *out = malloc(cap);
    if (out) str_json_escape_pattern(out, cap, s);
    return out;
}

/* Forward declarations */
static void print_usage(const char *prog);
static int cmd_create(socket_client_t *client, int argc, char **argv);
static int cmd_list(socket_client_t *client, int argc, char **argv);
static int cmd_info(socket_client_t *client, int argc, char **argv);
static int cmd_send(socket_client_t *client, int argc, char **argv);
static int cmd_output(socket_client_t *client, int argc, char **argv);
static int cmd_screen(socket_client_t *client, int argc, char **argv);
static int cmd_resize(socket_client_t *client, int argc, char **argv);
static int cmd_attach(socket_client_t *client, int argc, char **argv);
static int cmd_expect(socket_client_t *client, int argc, char **argv);
static int cmd_expect_incoming(socket_client_t *client, int argc, char **argv);
static int cmd_scrollback(socket_client_t *client, int argc, char **argv);

static int cmd_lines(socket_client_t *client, int argc, char **argv);


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
    printf("  -V           Show version\n");
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
    printf("  lines <session-id> --start <n> --end <n>\n");
    printf("      Get line range from output log (works across rotated files)\n");
    printf("      --start: Starting line number (1-based, inclusive)\n");
    printf("      --end: Ending line number (1-based, inclusive)\n");
    printf("      --tail <n>: Get last n lines (alternative to --start/--end)\n");
    printf("      --head <n>: Get first n lines (alternative to --start/--end)\n");
    printf("\n");
    printf("  resize <session-id> <rows> <cols>\n");
    printf("      Resize terminal\n");
    printf("\n");
    printf("  attach <session-id>\n");
    printf("      Open an interactive terminal on the session (like tmux attach).\n");
    printf("      The session is resized to your terminal; press Ctrl-] to detach.\n");
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
            if (!safe_atoi(argv[++i], &rows) || rows <= 0) {
                fprintf(stderr, "Invalid --rows value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &cols) || cols <= 0) {
                fprintf(stderr, "Invalid --cols value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--scrollback") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &scrollback_lines) || scrollback_lines < 0) {
                fprintf(stderr, "Invalid --scrollback value\n");
                return 1;
            }
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
    
    /* Escape user text for JSON, then build the request in a dynamic buffer */
    char *esc_command_buf = json_escape_dup(command);
    char *esc_output_buf = output_file ? json_escape_dup(output_file) : NULL;
    if (!esc_command_buf || (output_file && !esc_output_buf)) {
        fprintf(stderr, "Failed to allocate request buffer\n");
        free(esc_command_buf);
        free(esc_output_buf);
        return 1;
    }
    command = esc_command_buf;
    output_file = esc_output_buf;
    size_t request_size = 512 + strlen(command) + (output_file ? strlen(output_file) : 0);
    char *request = malloc(request_size);
    if (!request) {
        fprintf(stderr, "Failed to allocate request buffer\n");
        free(esc_command_buf);
        free(esc_output_buf);
        return 1;
    }

    int n;
    if (output_file) {
        if (rotate_count > 0 && rotate_lines > 0) {
            n = snprintf(request, request_size,
                         "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d,\"output_file\":\"%s\",\"rotate_count\":%d,\"rotate_lines\":%d}}",
                         command, rows, cols, scrollback_lines, output_file, rotate_count, rotate_lines);
        } else {
            n = snprintf(request, request_size,
                         "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d,\"output_file\":\"%s\"}}",
                         command, rows, cols, scrollback_lines, output_file);
        }
    } else {
        n = snprintf(request, request_size,
                     "{\"method\":\"create_session\",\"params\":{\"command\":\"%s\",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d}}",
                     command, rows, cols, scrollback_lines);
    }
    free(esc_command_buf);
    free(esc_output_buf);
    if (n < 0 || (size_t)n >= request_size) {
        fprintf(stderr, "Failed to build request\n");
        free(request);
        return 1;
    }
    
    /* Send request */
    char response[4096];
    int send_rc = socket_client_send_request(client, request, response, sizeof(response));
    free(request);
    if (send_rc < 0) {
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
    
    /* Process escape sequences in text: bounded, src kept separate from
     * dst because some escapes (\F5 etc.) expand and would otherwise
     * overwrite unread source bytes. */
    char text_buf[4096];
    str_unescape(text_buf, sizeof(text_buf), argv[1]);
    
    /* Build request (text is JSON-escaped: it may contain quotes, backslashes
     * and the control characters produced by the escapes above) */
    char *esc_text = json_escape_dup(text_buf);
    char *esc_id = json_escape_dup(session_id);
    if (!esc_text || !esc_id) {
        fprintf(stderr, "Failed to allocate request buffer\n");
        free(esc_text);
        free(esc_id);
        return 1;
    }
    size_t request_size = strlen(esc_text) + strlen(esc_id) + 128;
    char *request = malloc(request_size);
    if (!request) {
        fprintf(stderr, "Failed to allocate request buffer\n");
        free(esc_text);
        free(esc_id);
        return 1;
    }
    snprintf(request, request_size,
             "{\"method\":\"send_text\",\"params\":{\"id\":\"%s\",\"text\":\"%s\"}}",
             esc_id, esc_text);
    free(esc_text);
    free(esc_id);
    
    /* Send request */
    char response[1024];
    int send_rc = socket_client_send_request(client, request, response, sizeof(response));
    free(request);
    if (send_rc < 0) {
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

#define SCREEN_RESPONSE_SIZE 262144

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
    
    /* Send request (the screen text is UTF-8, so allow for large screens) */
    char *response = malloc(SCREEN_RESPONSE_SIZE);
    if (!response) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    if (socket_client_send_request(client, request, response, SCREEN_RESPONSE_SIZE) < 0) {
        fprintf(stderr, "Failed to send request\n");
        free(response);
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    free(response);
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
    int rows = 0, cols = 0;
    if (!safe_atoi(argv[1], &rows) || rows <= 0) {
        fprintf(stderr, "Invalid rows value\n");
        return 1;
    }
    if (!safe_atoi(argv[2], &cols) || cols <= 0) {
        fprintf(stderr, "Invalid cols value\n");
        return 1;
    }
    
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

/* ---- attach: interactive terminal on a session ---- */

#define ATTACH_DETACH_KEY 0x1d  /* Ctrl-] */

static volatile sig_atomic_t g_attach_winch = 0;
static volatile sig_atomic_t g_attach_quit = 0;

static void attach_on_winch(int sig) { (void)sig; g_attach_winch = 1; }
static void attach_on_term(int sig) { (void)sig; g_attach_quit = 1; }

/* Write all of buf to fd (blocking), retrying on EINTR/partial writes */
static int write_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t w = write(fd, buf + total, len - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)w;
    }
    return 0;
}

/* Tell the daemon the new terminal size (separate short-lived connection:
 * the attach connection is a raw byte stream and carries no commands). */
static void attach_send_resize(const char *socket_path, const char *session_id,
                               int rows, int cols) {
    socket_client_t rc;
    if (socket_client_connect(&rc, socket_path) < 0) return;
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"resize_session\",\"params\":{\"id\":\"%s\",\"rows\":%d,\"cols\":%d}}",
             session_id, rows, cols);
    char response[1024];
    socket_client_send_request(&rc, request, response, sizeof(response));
    socket_client_disconnect(&rc);
}

/* Private modes the session switched on the user's terminal, followed in the
 * output stream (and in the redraw the daemon sends first), so detaching can
 * switch them off again. Only "CSI ? Ps ; Ps h|l" matters. */
typedef struct {
    int state;            /* 0 text, 1 after ESC, 2 after ESC [, 3 after ESC [ ? */
    int param;
    int params[16];
    int count;
    int alt;              /* 47, 1047 or 1049 while the alternate screen is on */
    int mouse;            /* active mouse tracking mode */
    int mouse_enc;        /* active mouse encoding */
    bool cursor_hidden, app_keys, no_wrap, focus, paste;
} attach_modes_t;

static void attach_modes_set(attach_modes_t *m, int mode, bool on) {
    switch (mode) {
        case 1:    m->app_keys = on; break;
        case 7:    m->no_wrap = !on; break;
        case 25:   m->cursor_hidden = !on; break;
        case 47: case 1047: case 1049: m->alt = on ? mode : 0; break;
        case 9: case 1000: case 1001: case 1002: case 1003:
            if (on) m->mouse = mode; else if (m->mouse == mode) m->mouse = 0;
            break;
        case 1005: case 1006: case 1015:
            if (on) m->mouse_enc = mode; else if (m->mouse_enc == mode) m->mouse_enc = 0;
            break;
        case 1004: m->focus = on; break;
        case 2004: m->paste = on; break;
        default: break;
    }
}

static void attach_modes_feed(attach_modes_t *m, const char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)buf[i];
        switch (m->state) {
            case 0:
                if (ch == 0x1b) m->state = 1;
                break;
            case 1:
                m->state = (ch == '[') ? 2 : (ch == 0x1b ? 1 : 0);
                break;
            case 2:
                if (ch == '?') { m->state = 3; m->param = 0; m->count = 0; }
                else m->state = (ch == 0x1b) ? 1 : 0;
                break;
            default:
                if (ch >= '0' && ch <= '9') {
                    if (m->param < 100000) m->param = m->param * 10 + (ch - '0');
                } else if (ch == ';') {
                    if (m->count < 16) m->params[m->count++] = m->param;
                    m->param = 0;
                } else {
                    if (ch == 'h' || ch == 'l') {
                        if (m->count < 16) m->params[m->count++] = m->param;
                        for (int k = 0; k < m->count; k++) {
                            attach_modes_set(m, m->params[k], ch == 'h');
                        }
                    }
                    m->state = (ch == 0x1b) ? 1 : 0;
                }
                break;
        }
    }
}

/* Switch off what the session switched on; always leave the cursor visible
 * and the text attributes plain */
static void attach_modes_restore(const attach_modes_t *m) {
    char out[256];
    size_t n = 0;
    n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[0m");
    if (m->mouse_enc) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?%dl", m->mouse_enc);
    if (m->mouse) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?%dl", m->mouse);
    if (m->focus) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?1004l");
    if (m->paste) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?2004l");
    if (m->app_keys) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?1l");
    if (m->no_wrap) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?7h");
    if (m->alt) n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?%dl", m->alt);
    n += (size_t)snprintf(out + n, sizeof(out) - n, "\x1b[?25h");
    write_all(STDOUT_FILENO, out, n);
}

static int cmd_attach(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: attach <session-id>\n");
        return 1;
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "attach needs a terminal on stdin and stdout\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    struct winsize ws;
    int rows = 0, cols = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    
    /* Handshake: normal request/response; afterwards the socket is a raw stream */
    char request[512];
    snprintf(request, sizeof(request),
             "{\"method\":\"attach_session\",\"params\":{\"id\":\"%s\",\"rows\":%d,\"cols\":%d}}",
             session_id, rows, cols);
    char response[1024];
    if (socket_client_send_request(client, request, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    json_value_t *root = json_parse(response);
    if (!root) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    const char *status_str = json_object_get_string(root, "status");
    if (!status_str || strcmp(status_str, "success") != 0) {
        fprintf(stderr, "Failed to attach: session not found, dead, or too many terminals attached\n");
        json_free(root);
        return 1;
    }
    json_free(root);
    
    /* Raw local terminal */
    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) < 0) {
        fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
        return 1;
    }
    raw = orig;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
        return 1;
    }
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = attach_on_winch;  /* no SA_RESTART: poll() must wake up */
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = attach_on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    
    const char *why = "session ended";
    attach_modes_t modes;
    memset(&modes, 0, sizeof(modes));
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = client->fd;
    fds[1].events = POLLIN;
    
    while (!g_attach_quit) {
        if (g_attach_winch) {
            g_attach_winch = 0;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
                attach_send_resize(client->socket_path, session_id, ws.ws_row, ws.ws_col);
            }
        }
        
        int pr = poll(fds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            why = "poll failed";
            break;
        }
        
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[4096];
            ssize_t n = read(client->fd, buf, sizeof(buf));
            if (n > 0) {
                attach_modes_feed(&modes, buf, (size_t)n);
                if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) break;
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                break;  /* session ended or daemon went away */
            }
        }
        
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                ssize_t keep = n;
                bool detach = false;
                for (ssize_t i = 0; i < n; i++) {
                    if ((unsigned char)buf[i] == ATTACH_DETACH_KEY) {
                        keep = i;
                        detach = true;
                        break;
                    }
                }
                if (keep > 0 && write_all(client->fd, buf, (size_t)keep) < 0) {
                    why = "connection lost";
                    break;
                }
                if (detach) {
                    why = "detached";
                    break;
                }
            } else if (n == 0) {
                why = "stdin closed";
                break;
            } else if (errno != EINTR && errno != EAGAIN) {
                why = "stdin error";
                break;
            }
        }
    }
    
    attach_modes_restore(&modes);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    printf("\r\n[%s]\r\n", g_attach_quit ? "terminated" : why);
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
            if (!safe_atoi(argv[++i], &start_line) || start_line < 0) {
                fprintf(stderr, "Invalid --start value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &count) || count <= 0) {
                fprintf(stderr, "Invalid --count value\n");
                return 1;
            }
        }
    }
    
    /* Build request */
    char request[1024];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_scrollback\",\"params\":{\"id\":\"%s\",\"start_line\":%d,\"count\":%d}}",
             session_id, start_line, count);
    
    /* Send request */
    /* Up to SESSION_SCROLLBACK_MAX_BYTES of lines plus the JSON frame */
    size_t response_size = 64 * 1024;
    char *response = malloc(response_size);
    if (!response) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    if (socket_client_send_request(client, request, response, response_size) < 0) {
        fprintf(stderr, "Failed to send request\n");
        free(response);
        return 1;
    }
    
    /* Parse response */
    json_value_t *root = json_parse(response);
    free(response);
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

static int cmd_lines(socket_client_t *client, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: lines <session-id> --start <n> --end <n>\n");
        fprintf(stderr, "   or: lines <session-id> --tail <n>\n");
        fprintf(stderr, "   or: lines <session-id> --head <n>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    int start_line = 0;
    int end_line = 0;
    int tail_lines = 0;
    int head_lines = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &start_line) || start_line < 1) {
                fprintf(stderr, "Invalid --start value (must be >= 1)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--end") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &end_line) || end_line < 1) {
                fprintf(stderr, "Invalid --end value (must be >= 1)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--tail") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &tail_lines) || tail_lines <= 0) {
                fprintf(stderr, "Invalid --tail value (must be > 0)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--head") == 0 && i + 1 < argc) {
            if (!safe_atoi(argv[++i], &head_lines) || head_lines <= 0) {
                fprintf(stderr, "Invalid --head value (must be > 0)\n");
                return 1;
            }
        }
    }
    
    /* Validate arguments */
    if (tail_lines > 0) {
        /* Get total lines first to calculate range */
        char info_request[512];
        snprintf(info_request, sizeof(info_request),
                 "{\"method\":\"get_session\",\"params\":{\"id\":\"%s\"}}",
                 session_id);
        
        char info_response[16384];
        if (socket_client_send_request(client, info_request, info_response, sizeof(info_response)) < 0) {
            fprintf(stderr, "Failed to get session info\n");
            return 1;
        }
        
        json_value_t *info_root = json_parse(info_response);
        if (!info_root) {
            fprintf(stderr, "Failed to parse response\n");
            return 1;
        }
        
        json_value_t *info_result = json_object_get(info_root, "result");
        int total_lines = info_result ? json_object_get_int(info_result, "total_lines", 0) : 0;
        json_free(info_root);

        if (total_lines <= 0) {
            fprintf(stderr, "Output log is empty.\n");
            return 1;
        }

        start_line = (total_lines > tail_lines) ? (total_lines - tail_lines + 1) : 1;
        end_line = total_lines;

        /* The daemon handles one message per connection, so reconnect
         * before issuing the next request on this socket. */
        socket_client_disconnect(client);
        if (socket_client_connect(client, client->socket_path) < 0) {
            fprintf(stderr, "Failed to reconnect to pshell daemon\n");
            return 1;
        }
    } else if (head_lines > 0) {
        start_line = 1;
        end_line = head_lines;
    } else if (start_line == 0 || end_line == 0) {
        fprintf(stderr, "Must specify --start/--end, --tail, or --head\n");
        return 1;
    }
    
    /* Build request */
    char request[1024];
    snprintf(request, sizeof(request),
             "{\"method\":\"get_lines\",\"params\":{\"id\":\"%s\",\"start_line\":%d,\"end_line\":%d}}",
             session_id, start_line, end_line);
    
    /* Send request */
    char response[65536];  /* Larger buffer for line content */
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
            int total_lines = json_object_get_int(result, "total_lines", 0);
            int ret_start = json_object_get_int(result, "start_line", 0);
            int ret_end = json_object_get_int(result, "end_line", 0);
            const char *content = json_object_get_string(result, "content");
            
            fprintf(stderr, "Total lines: %d, showing lines %d-%d:\n", total_lines, ret_start, ret_end);
            
            if (content) {
                printf("%s", content);
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
    char *esc_pattern = json_escape_pattern_dup(pattern);
    if (!esc_pattern) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    size_t request_size = strlen(esc_pattern) + strlen(session_id) + 256;
    char *request = malloc(request_size);
    if (!request) {
        free(esc_pattern);
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    snprintf(request, request_size,
            "{\"method\":\"expect_pattern_incoming\",\"params\":{\"session_id\":\"%s\",\"pattern\":\"%s\",\"timeout\":%d,\"case_sensitive\":true}}",
            session_id, esc_pattern, timeout);
    free(esc_pattern);
    
    /* Send request */
    char response[8192];
    int sent = socket_client_send_request(client, request, response, sizeof(response));
    free(request);
    if (sent < 0) {
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
                json_free(root);
                return 0;
            } else {
                const char *reason = json_object_get_string(result, "reason");
                printf("No match (%s)\n", reason ? reason : "unknown");
                json_free(root);
                return 1;  /* Non-zero exit for timeout/no match */
            }
        }
    } else {
        fprintf(stderr, "Expect failed\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 1;
}

static int cmd_expect(socket_client_t *client, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: expect <session-id> <pattern> [timeout_ms]\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    const char *pattern = argv[1];
    int timeout = 5000;
    if (argc > 2) {
        if (!safe_atoi(argv[2], &timeout) || timeout <= 0) {
            fprintf(stderr, "Invalid timeout value (must be > 0)\n");
            return 1;
        }
    }
    
    /* Build request */
    char *esc_pattern = json_escape_pattern_dup(pattern);
    if (!esc_pattern) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    size_t request_size = strlen(esc_pattern) + strlen(session_id) + 256;
    char *request = malloc(request_size);
    if (!request) {
        free(esc_pattern);
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    snprintf(request, request_size,
             "{\"method\":\"expect_pattern\",\"params\":{\"id\":\"%s\",\"pattern\":\"%s\",\"timeout\":%d}}",
             session_id, esc_pattern, timeout);
    free(esc_pattern);
    
    /* Send request */
    char response[8192];
    int sent = socket_client_send_request(client, request, response, sizeof(response));
    free(request);
    if (sent < 0) {
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
                json_free(root);
                return 0;
            } else {
                const char *reason = json_object_get_string(result, "reason");
                printf("No match (%s)\n", reason ? reason : "unknown");
                json_free(root);
                return 1;  /* Non-zero exit for timeout/no match */
            }
        }
    } else {
        fprintf(stderr, "Expect failed\n");
        json_free(root);
        return 1;
    }
    
    json_free(root);
    return 1;
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
            if (!safe_atoi(argv[i + 1], &key_length) || key_length < 16 || key_length > 64) {
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
    
    /* Writes to a closed daemon connection must not kill the client */
    signal(SIGPIPE, SIG_IGN);
    
    /* Check for environment variable override */
    const char *env_socket = getenv("PSHELL_SOCKET_PATH");
    if (env_socket && env_socket[0] != '\0') {
        socket_path = env_socket;
    }
    
    /* Parse options (command-line overrides environment variable) */
    while ((opt = getopt(argc, argv, "s:hV")) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'V':
                printf("pshellctl %s (%s)\n", PSHELL_VERSION_STRING, PSHELL_BUILD_INFO);
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
    } else if (strcmp(command, "lines") == 0) {
        result = cmd_lines(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "resize") == 0) {
        result = cmd_resize(&client, cmd_argc, cmd_argv);
    } else if (strcmp(command, "attach") == 0) {
        result = cmd_attach(&client, cmd_argc, cmd_argv);
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