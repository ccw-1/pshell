/*
 * mcp_server.c - Model Context Protocol (MCP) server for pshell
 *
 * Exposes pshell terminal session control as MCP tools over stdio using
 * newline-delimited JSON-RPC 2.0. Tool calls are forwarded to the pshell
 * daemon over the Unix domain socket. If the daemon is not running it is
 * started automatically.
 */

#include "pshell/config.h"
#include "pshell/version.h"
#include "pshell/json_simple.h"
#include "pshell/logger.h"
#include "pshell/socket_client.h"
#include "pshell/string_utils.h"
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MCP_PROTOCOL_VERSION     "2025-03-26"
#define MCP_SERVER_NAME         "pshell-mcp"
#define MCP_SERVER_VERSION      PSHELL_VERSION_STRING
#define DEFAULT_SOCKET_PATH     "/tmp/pshell.sock"
#define DEFAULT_CONFIG_FILE     "/etc/pshell/pshell.conf"
#define DAEMON_RESPONSE_SIZE    65536
#define DAEMON_START_TIMEOUT_MS 5000
#define DAEMON_START_POLL_MS    50

/* ------------------------------------------------------------------ */
/* Dynamic string buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static bool sb_init(strbuf_t *sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->buf = malloc(sb->cap);
    if (sb->buf) sb->buf[0] = '\0';
    return sb->buf != NULL;
}

static void sb_free(strbuf_t *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool sb_grow(strbuf_t *sb, size_t extra) {
    if (sb->buf && sb->len + extra + 1 <= sb->cap) return true;
    size_t new_cap = sb->cap ? sb->cap : 4096;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    char *nb = realloc(sb->buf, new_cap);
    if (!nb) return false;
    sb->buf = nb;
    sb->cap = new_cap;
    return true;
}

static void sb_append(strbuf_t *sb, const char *s, size_t n) {
    if (!sb_grow(sb, n)) return;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_puts(strbuf_t *sb, const char *s) {
    sb_append(sb, s, strlen(s));
}

static void sb_printf(strbuf_t *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return;
    if (!sb_grow(sb, (size_t)needed)) return;
    va_start(ap, fmt);
    vsnprintf(sb->buf + sb->len, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)needed;
}

static void sb_put_json_string(strbuf_t *sb, const char *s) {
    if (!s) {
        sb_puts(sb, "null");
        return;
    }
    sb_puts(sb, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"':  sb_puts(sb, "\\\""); break;
            case '\\': sb_puts(sb, "\\\\"); break;
            case '\n': sb_puts(sb, "\\n"); break;
            case '\r': sb_puts(sb, "\\r"); break;
            case '\t': sb_puts(sb, "\\t"); break;
            case '\b': sb_puts(sb, "\\b"); break;
            case '\f': sb_puts(sb, "\\f"); break;
            default:
                if (c < 0x20) {
                    sb_printf(sb, "\\u%04x", c);
                } else {
                    sb_append(sb, (const char *)&c, 1);
                }
        }
    }
    sb_puts(sb, "\"");
}

static char *sb_dup(const strbuf_t *sb) {
    return str_dup(sb->buf ? sb->buf : "");
}

static void sb_put_id(strbuf_t *sb, const json_value_t *id) {
    if (!id) {
        sb_puts(sb, "null");
        return;
    }
    json_type_t type = json_get_type(id);
    if (type == JSON_NUMBER) {
        double num = 0;
        json_get_number(id, &num);
        sb_printf(sb, "%.17g", num);
    } else if (type == JSON_STRING) {
        sb_put_json_string(sb, json_get_string(id));
    } else {
        sb_puts(sb, "null");
    }
}

/* ------------------------------------------------------------------ */
/* Runtime configuration                                              */
/* ------------------------------------------------------------------ */

static const char *g_socket_path = DEFAULT_SOCKET_PATH;
static const char *g_config_path = NULL;
static const char *g_daemon_bin = NULL;
static bool g_autostart = true;
static bool g_daemon_spawn_attempted = false;
static char g_spawn_config_path[4096] = "";

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("pshell MCP server (Model Context Protocol over stdio).\n");
    printf("Forwards tool calls to the pshell daemon over the Unix domain socket,\n");
    printf("starting the daemon automatically when it is not running.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s <path>    Daemon socket path (default: %s)\n", DEFAULT_SOCKET_PATH);
    printf("               Can also be set via PSHELL_SOCKET_PATH\n");
    printf("  -c <path>    Config file used when starting the daemon\n");
    printf("               Can also be set via PSHELL_CONFIG (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -b <path>    pshell daemon binary to start\n");
    printf("               Can also be set via PSHELL_DAEMON_BIN\n");
    printf("  -n           Do not auto-start the daemon\n");
    printf("  -v           Show version\n");
    printf("  -h           Show this help\n");
}

/* ------------------------------------------------------------------ */
/* Daemon discovery and auto-start                                    */
/* ------------------------------------------------------------------ */

static bool file_is_executable(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return access(path, X_OK) == 0;
}

static const char *self_dir(void) {
    static char dir[4096];
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) return NULL;
    *slash = '\0';
    snprintf(dir, sizeof(dir), "%s", buf);
    return dir;
}

static bool find_config_file(void) {
    static char beside_binary[4096];
    const char *candidates[4];
    int n = 0;
    const char *dir = self_dir();

    candidates[n++] = DEFAULT_CONFIG_FILE;
    if (dir) {
        snprintf(beside_binary, sizeof(beside_binary), "%s/../config/pshell.conf", dir);
        candidates[n++] = beside_binary;
    }
    candidates[n++] = "config/pshell.conf";
    candidates[n++] = "pshell.conf";

    for (int i = 0; i < n; i++) {
        struct stat st;
        if (candidates[i] && stat(candidates[i], &st) == 0 && S_ISREG(st.st_mode)) {
            g_config_path = candidates[i];
            return true;
        }
    }
    g_config_path = NULL;
    return false;
}

static const char *find_daemon_binary(void) {
    static char beside_binary[4096];
    const char *dir = self_dir();

    if (dir) {
        snprintf(beside_binary, sizeof(beside_binary), "%s/pshell", dir);
        if (file_is_executable(beside_binary)) return beside_binary;
    }
    if (file_is_executable("/usr/local/bin/pshell")) return "/usr/local/bin/pshell";
    if (file_is_executable("/usr/bin/pshell")) return "/usr/bin/pshell";
    if (file_is_executable("/bin/pshell")) return "/bin/pshell";
    if (dir) return beside_binary;
    return "pshell";
}

static bool write_spawn_config(void) {
    snprintf(g_spawn_config_path, sizeof(g_spawn_config_path),
             "/tmp/pshell-mcp-%ld.conf", (long)getpid());

    FILE *out = fopen(g_spawn_config_path, "w");
    if (!out) {
        g_spawn_config_path[0] = '\0';
        return false;
    }

    fprintf(out, "[daemon]\n");
    fprintf(out, "pid_file = /tmp/pshell-mcp.pid\n");
    fprintf(out, "log_file = /tmp/pshell-mcp.log\n");
    fprintf(out, "[socket]\n");
    fprintf(out, "socket_path = %s\n", g_socket_path);

    if (g_config_path) {
        FILE *in = fopen(g_config_path, "r");
        if (in) {
            char line[1024];
            while (fgets(line, sizeof(line), in)) {
                fputs(line, out);
            }
            fclose(in);
        }
    }

    fclose(out);
    return true;
}

static bool socket_accepts_connection(void) {
    socket_client_t client;
    if (socket_client_connect(&client, g_socket_path) < 0) {
        return false;
    }
    socket_client_disconnect(&client);
    return true;
}

static bool wait_for_socket(int timeout_ms) {
    int waited = 0;
    while (waited <= timeout_ms) {
        if (socket_accepts_connection()) return true;
        usleep(DAEMON_START_POLL_MS * 1000);
        waited += DAEMON_START_POLL_MS;
    }
    return false;
}

static bool spawn_daemon(void) {
    const char *bin = g_daemon_bin ? g_daemon_bin : find_daemon_binary();
    const char *config = g_spawn_config_path[0] ? g_spawn_config_path : g_config_path;

    pid_t pid = fork();
    if (pid < 0) {
        log_error("mcp: fork failed: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        /* Close stdin/stdout to prevent daemon output from corrupting
         * the MCP protocol stream */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        /* Redirect stderr to /dev/null as well */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        if (config) {
            execl(bin, bin, "-c", config, (char *)NULL);
        } else {
            execl(bin, bin, (char *)NULL);
        }
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        log_error("mcp: failed to execute daemon binary: %s", bin);
        return false;
    }

    return true;
}

static bool ensure_daemon_running(char **err_text) {
    if (socket_accepts_connection()) return true;

    if (!g_autostart) {
        strbuf_t sb;
        sb_init(&sb);
        sb_printf(&sb,
                  "pshell daemon is not running at %s and auto-start is disabled (-n).",
                  g_socket_path);
        *err_text = sb_dup(&sb);
        sb_free(&sb);
        return false;
    }

    if (!g_daemon_spawn_attempted) {
        g_daemon_spawn_attempted = true;
        const char *bin = g_daemon_bin ? g_daemon_bin : find_daemon_binary();
        if (!file_is_executable(bin)) {
            strbuf_t sb;
            sb_init(&sb);
            sb_printf(&sb,
                      "pshell daemon binary not found or not executable: %s. "
                      "Build it with 'make' or install it, or set PSHELL_DAEMON_BIN.",
                      bin);
            *err_text = sb_dup(&sb);
            sb_free(&sb);
            return false;
        }
        write_spawn_config();
        log_info("mcp: starting pshell daemon (%s) config=%s",
                 bin, g_spawn_config_path[0] ? g_spawn_config_path
                                             : (g_config_path ? g_config_path : "defaults"));
        if (!spawn_daemon()) {
            strbuf_t sb;
            sb_init(&sb);
            sb_printf(&sb, "Failed to start pshell daemon: %s", bin);
            *err_text = sb_dup(&sb);
            sb_free(&sb);
            return false;
        }
    }

    if (wait_for_socket(DAEMON_START_TIMEOUT_MS)) {
        if (g_spawn_config_path[0]) {
            unlink(g_spawn_config_path);
        }
        return true;
    }

    strbuf_t sb;
    sb_init(&sb);
    sb_printf(&sb,
              "pshell daemon did not become available at %s within %d ms of starting it.",
              g_socket_path, DAEMON_START_TIMEOUT_MS);
    *err_text = sb_dup(&sb);
    sb_free(&sb);
    return false;
}

/* ------------------------------------------------------------------ */
/* Daemon RPC bridge                                                  */
/* ------------------------------------------------------------------ */

static json_value_t *daemon_rpc(const char *request, char **err_text) {
    socket_client_t client;
    if (socket_client_connect(&client, g_socket_path) < 0) {
        if (!ensure_daemon_running(err_text)) {
            return NULL;
        }
        if (socket_client_connect(&client, g_socket_path) < 0) {
            strbuf_t sb;
            sb_init(&sb);
            sb_printf(&sb, "Failed to connect to pshell daemon at %s", g_socket_path);
            *err_text = sb_dup(&sb);
            sb_free(&sb);
            return NULL;
        }
    }

    char *response = malloc(DAEMON_RESPONSE_SIZE);
    if (!response) {
        socket_client_disconnect(&client);
        *err_text = str_dup("Out of memory");
        return NULL;
    }

    int n = socket_client_send_request(&client, request, response, DAEMON_RESPONSE_SIZE);
    socket_client_disconnect(&client);

    if (n < 0) {
        free(response);
        *err_text = str_dup("Daemon request failed (response too large or connection lost)");
        return NULL;
    }

    json_value_t *root = json_parse(response);
    free(response);

    if (!root) {
        *err_text = str_dup("Failed to parse daemon response");
        return NULL;
    }

    const char *status = json_object_get_string(root, "status");
    if (!status || strcmp(status, "success") != 0) {
        json_value_t *error = json_object_get(root, "error");
        const char *message = error ? json_object_get_string(error, "message") : NULL;
        *err_text = str_dup(message ? message : "Daemon returned an error");
        json_free(root);
        return NULL;
    }

    return root;
}

/* ------------------------------------------------------------------ */
/* Tool helpers                                                       */
/* ------------------------------------------------------------------ */

static const char *arg_string(const json_value_t *args, const char *key) {
    return json_object_get_string(args, key);
}

static int arg_int(const json_value_t *args, const char *key, int default_val) {
    return json_object_get_int(args, key, default_val);
}

static double arg_number(const json_value_t *args, const char *key, double default_val) {
    json_value_t *val = json_object_get(args, key);
    double num = 0;
    if (val && json_get_number(val, &num)) return num;
    return default_val;
}

static bool arg_required_string(const json_value_t *args, const char *key,
                                const char **out, char **err_text) {
    const char *value = json_object_get_string(args, key);
    if (!value || !value[0]) {
        strbuf_t sb;
        sb_init(&sb);
        sb_printf(&sb, "Missing required parameter: %s", key);
        *err_text = sb_dup(&sb);
        sb_free(&sb);
        return false;
    }
    *out = value;
    return true;
}

/* ------------------------------------------------------------------ */
/* Tools                                                              */
/* ------------------------------------------------------------------ */

static char *tool_create_session(const json_value_t *args, bool *is_error) {
    const char *command = arg_string(args, "command");
    if (!command || !command[0]) command = "bash";
    int rows = arg_int(args, "rows", 24);
    int cols = arg_int(args, "cols", 80);
    int scrollback = arg_int(args, "scrollback_lines", 0);
    const char *output_file = arg_string(args, "output_file");
    int rotate_count = arg_int(args, "rotate_count", 0);
    int rotate_lines = arg_int(args, "rotate_lines", 0);

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"create_session\",\"params\":{\"command\":");
    sb_put_json_string(&req, command);
    sb_printf(&req, ",\"rows\":%d,\"cols\":%d,\"scrollback_lines\":%d", rows, cols, scrollback);
    if (output_file && output_file[0]) {
        sb_puts(&req, ",\"output_file\":");
        sb_put_json_string(&req, output_file);
        if (rotate_count > 0 && rotate_lines > 0) {
            sb_printf(&req, ",\"rotate_count\":%d,\"rotate_lines\":%d", rotate_count, rotate_lines);
        }
    }
    sb_puts(&req, "}}");

    char *err = NULL;
    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);

    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to create session");
    }

    json_value_t *result = json_object_get(root, "result");
    const char *id = result ? json_object_get_string(result, "id") : NULL;
    int pid = result ? json_object_get_int(result, "pid", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    if (id) {
        sb_printf(&out, "Session created: %s\nCommand: %s\nSize: %dx%d\nPID: %d",
                  id, command, rows, cols, pid);
    } else {
        sb_puts(&out, "Session created but the daemon returned no session ID");
        *is_error = true;
    }
    char *text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return text;
}

static char *tool_list_sessions(const json_value_t *args, bool *is_error) {
    (void)args;

    char *err = NULL;
    json_value_t *root = daemon_rpc("{\"method\":\"list_sessions\"}", &err);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to list sessions");
    }

    json_value_t *result = json_object_get(root, "result");
    json_value_t *sessions = result ? json_object_get(result, "sessions") : NULL;
    int count = result ? json_object_get_int(result, "count", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    if (!sessions || json_get_type(sessions) != JSON_ARRAY || json_array_size(sessions) == 0) {
        sb_puts(&out, "No active sessions.");
    } else {
        sb_printf(&out, "Active sessions (%d):\n", count);
        size_t total = json_array_size(sessions);
        for (size_t i = 0; i < total; i++) {
            json_value_t *session = json_array_get(sessions, i);
            const char *id = json_object_get_string(session, "id");
            const char *cmd = json_object_get_string(session, "command");
            int rows = json_object_get_int(session, "rows", 0);
            int cols = json_object_get_int(session, "cols", 0);
            int pid = json_object_get_int(session, "pid", 0);
            const char *state = json_object_get_string(session, "state");
            sb_printf(&out, "  %s  command=%s  %dx%d  pid=%d  state=%s\n",
                      id ? id : "?", cmd ? cmd : "?", rows, cols, pid,
                      state ? state : "unknown");
        }
    }
    char *text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return text;
}

static char *tool_send(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }
    const char *text = NULL;
    if (!arg_required_string(args, "text", &text, &err)) {
        *is_error = true;
        return err;
    }

    /* Escapes can expand (e.g. \F5: 3 bytes -> 5), so when processing is
     * requested, unescape into a buffer with 2x headroom instead of
     * in place on the exact-size str_dup. */
    char *payload;
    if (json_object_get_bool(args, "process_escapes", false)) {
        size_t cap = strlen(text) * 2 + 16;
        payload = malloc(cap);
        if (!payload) {
            *is_error = true;
            return str_dup("Out of memory");
        }
        str_unescape(payload, cap, text);
    } else {
        payload = str_dup(text);
        if (!payload) {
            *is_error = true;
            return str_dup("Out of memory");
        }
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"send_text\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_puts(&req, ",\"text\":");
    sb_put_json_string(&req, payload);
    sb_puts(&req, "}}");
    free(payload);

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to send text");
    }

    json_value_t *result = json_object_get(root, "result");
    int bytes = result ? json_object_get_int(result, "bytes", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    sb_printf(&out, "Sent %d bytes to session %s", bytes, session_id);
    char *out_text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return out_text;
}

static char *tool_read_output(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"get_output\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_puts(&req, "}}");

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to read output");
    }

    json_value_t *result = json_object_get(root, "result");
    const char *output = result ? json_object_get_string(result, "output") : NULL;

    char *out_text = str_dup((output && output[0]) ? output : "(no new output since last read)");
    json_free(root);
    return out_text;
}

static char *tool_screen(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"get_screen\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_puts(&req, "}}");

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to get screen");
    }

    json_value_t *result = json_object_get(root, "result");
    const char *text = result ? json_object_get_string(result, "text") : NULL;
    int cursor_row = result ? json_object_get_int(result, "cursor_row", 0) : 0;
    int cursor_col = result ? json_object_get_int(result, "cursor_col", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    if (text && text[0]) {
        sb_puts(&out, text);
        sb_puts(&out, "\n");
    } else {
        sb_puts(&out, "(empty screen)\n");
    }
    sb_printf(&out, "[Cursor at row %d, col %d]", cursor_row, cursor_col);

    char *out_text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return out_text;
}

static char *tool_scrollback(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }
    int start_line = arg_int(args, "start_line", 0);
    int count = arg_int(args, "count", 100);

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"get_scrollback\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_printf(&req, ",\"start_line\":%d,\"count\":%d}}", start_line, count);

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        if (err && strstr(err, "Invalid start_line")) {
            free(err);
            return str_dup("Scrollback is empty (no lines have scrolled off the "
                           "visible screen yet). Use pshell_screen for the visible area.");
        }
        *is_error = true;
        return err ? err : str_dup("Failed to get scrollback");
    }

    json_value_t *result = json_object_get(root, "result");
    int size = result ? json_object_get_int(result, "scrollback_size", 0) : 0;
    int used = result ? json_object_get_int(result, "scrollback_used", 0) : 0;
    int ret_start = result ? json_object_get_int(result, "start_line", 0) : 0;
    int ret_count = result ? json_object_get_int(result, "count", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    if (used <= 0) {
        sb_puts(&out, "Scrollback is empty (0/");
        sb_printf(&out, "%d lines).", size);
    } else {
        sb_printf(&out, "Scrollback: %d/%d lines used\nShowing lines %d-%d:\n",
                  used, size, ret_start, ret_start + ret_count - 1);
        json_value_t *lines = json_object_get(result, "lines");
        if (lines && json_get_type(lines) == JSON_ARRAY) {
            size_t total = json_array_size(lines);
            for (size_t i = 0; i < total; i++) {
                json_value_t *line = json_array_get(lines, i);
                const char *line_text = json_get_string(line);
                if (line_text) {
                    sb_puts(&out, line_text);
                    sb_puts(&out, "\n");
                }
            }
        }
    }

    char *out_text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return out_text;
}

static char *tool_read_lines(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }

    int start_line = arg_int(args, "start_line", 0);
    int end_line = arg_int(args, "end_line", 0);
    int head = arg_int(args, "head", 0);
    int tail = arg_int(args, "tail", 0);

    if (head <= 0 && tail <= 0 && (start_line < 1 || end_line < start_line)) {
        *is_error = true;
        return str_dup("Provide start_line and end_line (1-based), or head, or tail");
    }

    if (head > 0) {
        start_line = 1;
        end_line = head;
    } else if (tail > 0) {
        strbuf_t info_req;
        sb_init(&info_req);
        sb_puts(&info_req, "{\"method\":\"get_session\",\"params\":{\"id\":");
        sb_put_json_string(&info_req, session_id);
        sb_puts(&info_req, "}}");

        json_value_t *info = daemon_rpc(info_req.buf, &err);
        sb_free(&info_req);
        if (!info) {
            *is_error = true;
            return err ? err : str_dup("Failed to get session info");
        }
        json_value_t *info_result = json_object_get(info, "result");
        double total = info_result ? arg_number(info_result, "total_lines", 0) : 0;
        json_free(info);

        if (total < 1) {
            return str_dup("Output log is empty.");
        }
        long long total_ll = (long long)total;
        start_line = (total_ll > tail) ? (int)(total_ll - tail + 1) : 1;
        end_line = (int)total_ll;
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"get_lines\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_printf(&req, ",\"start_line\":%d,\"end_line\":%d}}", start_line, end_line);

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to read lines");
    }

    json_value_t *result = json_object_get(root, "result");
    double total = result ? arg_number(result, "total_lines", 0) : 0;
    int ret_start = result ? json_object_get_int(result, "start_line", start_line) : start_line;
    int ret_end = result ? json_object_get_int(result, "end_line", end_line) : end_line;
    const char *content = result ? json_object_get_string(result, "content") : NULL;

    strbuf_t out;
    sb_init(&out);
    sb_printf(&out, "Total lines: %lld (showing %d-%d)\n",
              (long long)total, ret_start, ret_end);
    if (content && content[0]) {
        sb_puts(&out, content);
    }
    char *out_text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return out_text;
}

static char *format_expect_result(json_value_t *root, int timeout_ms) {
    json_value_t *result = json_object_get(root, "result");
    bool found = result ? json_object_get_bool(result, "found", false) : false;

    strbuf_t out;
    sb_init(&out);
    if (found) {
        const char *matched = result ? json_object_get_string(result, "matched_text") : NULL;
        int line = result ? json_object_get_int(result, "line", 0) : 0;
        sb_printf(&out, "Matched: \"%s\" at line %d (relative to cursor)",
                  matched ? matched : "", line);
    } else {
        const char *reason = result ? json_object_get_string(result, "reason") : NULL;
        sb_printf(&out, "No match (%s) after %d ms",
                  reason ? reason : "unknown", timeout_ms);
    }
    char *text = sb_dup(&out);
    sb_free(&out);
    return text;
}

static char *tool_expect_impl(const json_value_t *args, bool *is_error, bool incoming_only) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }
    const char *pattern = NULL;
    if (!arg_required_string(args, "pattern", &pattern, &err)) {
        *is_error = true;
        return err;
    }
    int timeout_ms = arg_int(args, "timeout_ms", 5000);
    bool case_sensitive = json_object_get_bool(args, "case_sensitive", true);

    strbuf_t req;
    sb_init(&req);
    if (incoming_only) {
        sb_puts(&req, "{\"method\":\"expect_pattern_incoming\",\"params\":{\"session_id\":");
    } else {
        sb_puts(&req, "{\"method\":\"expect_pattern\",\"params\":{\"id\":");
    }
    sb_put_json_string(&req, session_id);
    sb_puts(&req, ",\"pattern\":");
    sb_put_json_string(&req, pattern);
    sb_printf(&req, ",\"timeout\":%d,\"case_sensitive\":%s}}",
              timeout_ms, case_sensitive ? "true" : "false");

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Expect failed");
    }

    char *text = format_expect_result(root, timeout_ms);
    json_free(root);
    return text;
}

static char *tool_expect(const json_value_t *args, bool *is_error) {
    return tool_expect_impl(args, is_error, false);
}

static char *tool_expect_incoming(const json_value_t *args, bool *is_error) {
    return tool_expect_impl(args, is_error, true);
}

static char *tool_resize(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }
    int rows = arg_int(args, "rows", 0);
    int cols = arg_int(args, "cols", 0);
    if (rows <= 0 || cols <= 0) {
        *is_error = true;
        return str_dup("Missing or invalid parameters: rows and cols must be positive");
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"resize_session\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_printf(&req, ",\"rows\":%d,\"cols\":%d}}", rows, cols);

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to resize session");
    }
    json_free(root);

    strbuf_t out;
    sb_init(&out);
    sb_printf(&out, "Resized session %s to %dx%d", session_id, rows, cols);
    char *out_text = sb_dup(&out);
    sb_free(&out);
    return out_text;
}

static char *tool_destroy_session(const json_value_t *args, bool *is_error) {
    const char *session_id = NULL;
    char *err = NULL;
    if (!arg_required_string(args, "session_id", &session_id, &err)) {
        *is_error = true;
        return err;
    }

    strbuf_t req;
    sb_init(&req);
    sb_puts(&req, "{\"method\":\"destroy_session\",\"params\":{\"id\":");
    sb_put_json_string(&req, session_id);
    sb_puts(&req, "}}");

    json_value_t *root = daemon_rpc(req.buf, &err);
    sb_free(&req);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to destroy session");
    }
    json_free(root);

    strbuf_t out;
    sb_init(&out);
    sb_printf(&out, "Session %s destroyed", session_id);
    char *out_text = sb_dup(&out);
    sb_free(&out);
    return out_text;
}

static char *tool_status(const json_value_t *args, bool *is_error) {
    (void)args;
    (void)is_error;

    char *err = NULL;
    json_value_t *root = daemon_rpc("{\"method\":\"daemon_status\"}", &err);
    if (!root) {
        *is_error = true;
        return err ? err : str_dup("Failed to get daemon status");
    }

    json_value_t *result = json_object_get(root, "result");
    const char *status = result ? json_object_get_string(result, "status") : NULL;
    int sessions = result ? json_object_get_int(result, "sessions", 0) : 0;
    long long uptime = result ? (long long)arg_number(result, "uptime", 0) : 0;

    strbuf_t out;
    sb_init(&out);
    sb_printf(&out, "Status: %s\nSessions: %d", status ? status : "unknown", sessions);
    if (uptime > 0) {
        sb_printf(&out, "\nUptime: %llds", uptime);
    }
    char *out_text = sb_dup(&out);
    sb_free(&out);
    json_free(root);
    return out_text;
}

/* ------------------------------------------------------------------ */
/* Tool registry and schema                                           */
/* ------------------------------------------------------------------ */

typedef char *(*tool_fn)(const json_value_t *args, bool *is_error);

typedef struct {
    const char *name;
    tool_fn fn;
} tool_entry_t;

static const tool_entry_t TOOL_TABLE[] = {
    {"pshell_create_session",   tool_create_session},
    {"pshell_list_sessions",    tool_list_sessions},
    {"pshell_send",             tool_send},
    {"pshell_read_output",      tool_read_output},
    {"pshell_screen",           tool_screen},
    {"pshell_scrollback",       tool_scrollback},
    {"pshell_read_lines",       tool_read_lines},
    {"pshell_expect",           tool_expect},
    {"pshell_expect_incoming",  tool_expect_incoming},
    {"pshell_resize",           tool_resize},
    {"pshell_destroy_session",  tool_destroy_session},
    {"pshell_status",           tool_status},
};

static const char *TOOLS_LIST_JSON =
"{\"tools\":["
"{\"name\":\"pshell_create_session\",\"description\":\"Create a new pshell terminal session running a command (default bash). Returns a session ID used by the other tools.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Command to run in the session. Examples: bash, ssh user@host, vim file.txt. Default: bash.\"},\"rows\":{\"type\":\"integer\",\"description\":\"Terminal height in rows. Default: 24.\"},\"cols\":{\"type\":\"integer\",\"description\":\"Terminal width in columns. Default: 80.\"},\"scrollback_lines\":{\"type\":\"integer\",\"description\":\"Scrollback buffer size in lines. Default: 1000.\"},\"output_file\":{\"type\":\"string\",\"description\":\"Optional: log all raw output to this file. Required for pshell_read_lines.\"},\"rotate_count\":{\"type\":\"integer\",\"description\":\"Optional: number of rotated log files (requires output_file).\"},\"rotate_lines\":{\"type\":\"integer\",\"description\":\"Optional: lines per rotated file (requires output_file).\"}},\"required\":[]}},"
"{\"name\":\"pshell_list_sessions\",\"description\":\"List all active pshell sessions with ID, command, size, PID and state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}},"
"{\"name\":\"pshell_send\",\"description\":\"Send text input to a session. End text with a newline to execute a shell command.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"text\":{\"type\":\"string\",\"description\":\"Text to send to the session.\"},\"process_escapes\":{\"type\":\"boolean\",\"description\":\"Interpret backslash escape sequences in text (\\\\n Enter, \\\\cC Ctrl-C, \\\\UP, \\\\F1). Default: false.\"}},\"required\":[\"session_id\",\"text\"]}},"
"{\"name\":\"pshell_read_output\",\"description\":\"Read new output from a session since the last read. Output is consumed and is not returned twice.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"}},\"required\":[\"session_id\"]}},"
"{\"name\":\"pshell_screen\",\"description\":\"Get the current visible terminal screen of a session (VT100 emulated) with cursor position.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"}},\"required\":[\"session_id\"]}},"
"{\"name\":\"pshell_scrollback\",\"description\":\"Get terminal history (scrollback) lines from a session.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"start_line\":{\"type\":\"integer\",\"description\":\"Zero-based start line index. Default: 0.\"},\"count\":{\"type\":\"integer\",\"description\":\"Number of lines to return. Default: 100.\"}},\"required\":[\"session_id\"]}},"
"{\"name\":\"pshell_read_lines\",\"description\":\"Read a global line range from the session output log (1-based line numbers, works across rotated log files). Requires the session to have been created with output_file. Use head or tail for the first or last N lines.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"start_line\":{\"type\":\"integer\",\"description\":\"First line to read (1-based, inclusive).\"},\"end_line\":{\"type\":\"integer\",\"description\":\"Last line to read (1-based, inclusive).\"},\"head\":{\"type\":\"integer\",\"description\":\"Read the first N lines instead of a range.\"},\"tail\":{\"type\":\"integer\",\"description\":\"Read the last N lines instead of a range.\"}},\"required\":[\"session_id\"]}},"
"{\"name\":\"pshell_expect\",\"description\":\"Wait for a POSIX extended regex pattern in session output (matches scrollback content plus new output). Returns the matched text.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"pattern\":{\"type\":\"string\",\"description\":\"Extended regular expression to wait for.\"},\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Timeout in milliseconds. Default: 5000.\"},\"case_sensitive\":{\"type\":\"boolean\",\"description\":\"Case sensitive matching. Default: true.\"}},\"required\":[\"session_id\",\"pattern\"]}},"
"{\"name\":\"pshell_expect_incoming\",\"description\":\"Wait for a POSIX extended regex pattern in NEW output only, ignoring historical data.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"pattern\":{\"type\":\"string\",\"description\":\"Extended regular expression to wait for.\"},\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Timeout in milliseconds. Default: 5000.\"},\"case_sensitive\":{\"type\":\"boolean\",\"description\":\"Case sensitive matching. Default: true.\"}},\"required\":[\"session_id\",\"pattern\"]}},"
"{\"name\":\"pshell_resize\",\"description\":\"Resize a session terminal.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"},\"rows\":{\"type\":\"integer\",\"description\":\"New height in rows.\"},\"cols\":{\"type\":\"integer\",\"description\":\"New width in columns.\"}},\"required\":[\"session_id\",\"rows\",\"cols\"]}},"
"{\"name\":\"pshell_destroy_session\",\"description\":\"Destroy a session and terminate its process.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"session_id\":{\"type\":\"string\",\"description\":\"Session ID.\"}},\"required\":[\"session_id\"]}},"
"{\"name\":\"pshell_status\",\"description\":\"Get pshell daemon status: running state, session count, uptime.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}}"
"]}";

static const tool_entry_t *find_tool(const char *name) {
    size_t count = sizeof(TOOL_TABLE) / sizeof(TOOL_TABLE[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(TOOL_TABLE[i].name, name) == 0) {
            return &TOOL_TABLE[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* JSON-RPC 2.0 over stdio                                           */
/* ------------------------------------------------------------------ */

static void send_result(const json_value_t *id, const char *result_json) {
    strbuf_t sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"jsonrpc\":\"2.0\",\"id\":");
    sb_put_id(&sb, id);
    sb_puts(&sb, ",\"result\":");
    sb_puts(&sb, result_json);
    sb_puts(&sb, "}\n");
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
}

static void send_error(const json_value_t *id, int code, const char *message) {
    strbuf_t sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"jsonrpc\":\"2.0\",\"id\":");
    sb_put_id(&sb, id);
    sb_printf(&sb, ",\"error\":{\"code\":%d,\"message\":", code);
    sb_put_json_string(&sb, message ? message : "");
    sb_puts(&sb, "}}\n");
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
}

static void send_tool_result(const json_value_t *id, const char *text, bool is_error) {
    strbuf_t sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"jsonrpc\":\"2.0\",\"id\":");
    sb_put_id(&sb, id);
    sb_puts(&sb, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    sb_put_json_string(&sb, text ? text : "");
    sb_printf(&sb, "}],\"isError\":%s}}\n", is_error ? "true" : "false");
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
}

static void handle_initialize(const json_value_t *id, const json_value_t *params) {
    const char *requested = params ? json_object_get_string(params, "protocolVersion") : NULL;
    const char *version = MCP_PROTOCOL_VERSION;
    if (requested &&
        (strcmp(requested, "2024-11-05") == 0 ||
         strcmp(requested, "2025-03-26") == 0 ||
         strcmp(requested, "2025-06-18") == 0)) {
        version = requested;
    }

    strbuf_t sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"jsonrpc\":\"2.0\",\"id\":");
    sb_put_id(&sb, id);
    sb_puts(&sb, ",\"result\":{\"protocolVersion\":\"");
    sb_puts(&sb, version);
    sb_puts(&sb, "\",\"capabilities\":{\"tools\":{\"listChanged\":false}},");
    sb_puts(&sb, "\"serverInfo\":{\"name\":\"" MCP_SERVER_NAME "\",\"version\":\"" MCP_SERVER_VERSION "\"},");
    sb_puts(&sb, "\"instructions\":\"pshell provides programmable terminal sessions. ");
    sb_puts(&sb, "Typical flow: pshell_create_session to start a program (bash, ssh, etc.), ");
    sb_puts(&sb, "pshell_send to type input (end with a newline to run a command), ");
    sb_puts(&sb, "pshell_expect to wait for a pattern, pshell_screen or pshell_read_output ");
    sb_puts(&sb, "to see results, then pshell_destroy_session when done. ");
    sb_puts(&sb, "The pshell daemon is started automatically if it is not running.\"}");
    sb_puts(&sb, "}\n");
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
}

static void handle_tools_call(const json_value_t *id, const json_value_t *params) {
    const char *name = params ? json_object_get_string(params, "name") : NULL;
    if (!name) {
        send_error(id, -32602, "Invalid params: missing tool name");
        return;
    }

    const tool_entry_t *tool = find_tool(name);
    if (!tool) {
        char message[256];
        snprintf(message, sizeof(message), "Unknown tool: %s", name);
        send_tool_result(id, message, true);
        return;
    }

    json_value_t *arguments = params ? json_object_get(params, "arguments") : NULL;
    json_value_t *empty = NULL;
    if (!arguments || json_get_type(arguments) != JSON_OBJECT) {
        empty = json_object_create();
        arguments = empty;
    }

    bool is_error = false;
    char *text = tool->fn(arguments, &is_error);
    send_tool_result(id, text ? text : "", is_error);
    free(text);

    json_free(empty);
}

static void handle_message(const char *line) {
    json_value_t *msg = json_parse(line);
    if (!msg) {
        send_error(NULL, -32700, "Parse error");
        return;
    }

    if (json_get_type(msg) != JSON_OBJECT) {
        send_error(NULL, -32600, "Invalid Request");
        json_free(msg);
        return;
    }

    const char *method = json_object_get_string(msg, "method");
    json_value_t *id = json_object_get(msg, "id");
    bool has_id = id != NULL;
    json_value_t *params = json_object_get(msg, "params");

    if (!method) {
        if (has_id) send_error(id, -32600, "Invalid Request: missing method");
        json_free(msg);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        if (has_id) handle_initialize(id, params);
    } else if (strcmp(method, "ping") == 0) {
        if (has_id) send_result(id, "{}");
    } else if (strcmp(method, "tools/list") == 0) {
        if (has_id) send_result(id, TOOLS_LIST_JSON);
    } else if (strcmp(method, "tools/call") == 0) {
        if (has_id) handle_tools_call(id, params);
    } else if (strcmp(method, "resources/list") == 0) {
        if (has_id) send_result(id, "{\"resources\":[]}");
    } else if (strcmp(method, "prompts/list") == 0) {
        if (has_id) send_result(id, "{\"prompts\":[]}");
    } else if (strcmp(method, "shutdown") == 0) {
        if (has_id) send_result(id, "{}");
    } else if (strncmp(method, "notifications/", 13) == 0) {
        /* notifications never get a response */
    } else {
        if (has_id) send_error(id, -32601, "Method not found");
    }

    json_free(msg);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *socket_path = NULL;
    const char *config_path = NULL;
    const char *daemon_bin = NULL;
    bool autostart = true;

    int opt;
    while ((opt = getopt(argc, argv, "s:c:b:nvh")) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'c':
                config_path = optarg;
                break;
            case 'b':
                daemon_bin = optarg;
                break;
            case 'n':
                autostart = false;
                break;
            case 'v':
                printf("pshell-mcp (pshell %s, MCP protocol %s)\n",
                       PSHELL_VERSION_STRING, MCP_PROTOCOL_VERSION);
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!config_path) config_path = getenv("PSHELL_CONFIG");
    if (config_path && config_path[0]) {
        g_config_path = config_path;
    } else {
        find_config_file();
    }

    if (!socket_path) socket_path = getenv("PSHELL_SOCKET_PATH");
    if (!socket_path || !socket_path[0]) {
        config_t *cfg = g_config_path ? config_load(g_config_path) : NULL;
        if (cfg) {
            const char *cfg_socket = config_get_string(cfg, "socket", "socket_path", NULL);
            if (cfg_socket && cfg_socket[0]) {
                static char cfg_socket_buf[4096];
                snprintf(cfg_socket_buf, sizeof(cfg_socket_buf), "%s", cfg_socket);
                socket_path = cfg_socket_buf;
            }
            config_free(cfg);
        }
    }
    if (!socket_path || !socket_path[0]) socket_path = DEFAULT_SOCKET_PATH;
    g_socket_path = socket_path;

    if (!daemon_bin) daemon_bin = getenv("PSHELL_DAEMON_BIN");
    if (daemon_bin && daemon_bin[0]) g_daemon_bin = daemon_bin;

    const char *no_autostart = getenv("PSHELL_NO_AUTOSTART");
    if (no_autostart && strcmp(no_autostart, "1") == 0) autostart = false;
    g_autostart = autostart;

    signal(SIGPIPE, SIG_IGN);

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    while ((n = getline(&line, &cap, stdin)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) continue;
        handle_message(line);
        fflush(stdout);
    }

    free(line);
    return 0;
}
