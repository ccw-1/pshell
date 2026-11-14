#include "pshell/http_client.h"
#include "pshell/json_simple.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080

/* Safe atoi replacement */
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

/* Global options */
static char *g_host = NULL;
static int g_port = 0;
static char *g_api_key = NULL;

/* Print usage */
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] COMMAND [ARGS...]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --host HOST      Server host (default: %s)\n", DEFAULT_HOST);
    printf("  -p, --port PORT      Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -k, --api-key KEY    API key for authentication\n");
    printf("  --help               Show this help message\n");
    printf("\n");
    printf("Commands:\n");
    printf("  create [OPTIONS]     Create new session\n");
    printf("    --command CMD      Command to execute (default: bash)\n");
    printf("    --rows ROWS        Terminal rows (default: 24)\n");
    printf("    --cols COLS        Terminal columns (default: 80)\n");
    printf("\n");
    printf("  list                 List all sessions\n");
    printf("\n");
    printf("  info SESSION_ID      Get session information\n");
    printf("\n");
    printf("  send SESSION_ID TEXT Send text to session\n");
    printf("\n");
    printf("  output SESSION_ID    Read output from session\n");
    printf("\n");
    printf("  screen SESSION_ID    Get screen state\n");
    printf("\n");
    printf("  resize SESSION_ID    Resize terminal\n");
    printf("    --rows ROWS        Terminal rows\n");
    printf("    --cols COLS        Terminal columns\n");
    printf("\n");
    printf("  expect SESSION_ID    Wait for pattern\n");
    printf("    --pattern PATTERN  Regex pattern to match\n");
    printf("    --timeout MS       Timeout in milliseconds (default: 30000)\n");
    printf("    --case-sensitive   Case-sensitive matching (default: true)\n");
    printf("\n");
    printf("  destroy SESSION_ID   Destroy session\n");
    printf("\n");
    printf("  status               Get daemon status\n");
    printf("\n");
    printf("  health               Check daemon health\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s create --command bash\n", prog);
    printf("  %s list\n", prog);
    printf("  %s send <session-id> \"ls -la\\n\"\n", prog);
    printf("  %s expect <session-id> --pattern \"\\$\\s*$\" --timeout 5000\n", prog);
    printf("  %s destroy <session-id>\n", prog);
    printf("\n");
}

/* Command: create */
static int cmd_create(int argc, char **argv) {
    const char *command = "bash";
    int rows = 24;
    int cols = 80;

    /* Simple manual argument parsing */
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
        }
    }

    /* Build JSON request with dynamic buffer to avoid truncation */
    size_t body_size = 256 + (command ? strlen(command) : 0);
    char *body = malloc(body_size);
    if (!body) {
        fprintf(stderr, "Failed to allocate request body\n");
        return 1;
    }
    int n = snprintf(body, body_size,
                     "{\"command\":\"%s\",\"rows\":%d,\"cols\":%d}",
                     command, rows, cols);
    if (n < 0 || (size_t)n >= body_size) {
        fprintf(stderr, "Failed to build request body\n");
        free(body);
        return 1;
    }

    /* Make request */
    http_response_t *resp = http_client_post(g_host, g_port, "/api/sessions", body, g_api_key);
    free(body);
    if (!resp) {
        fprintf(stderr, "Failed to create session\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    /* Parse response */
    json_value_t *json = json_parse(resp->body);
    if (json) {
        const char *id = json_object_get_string(json, "id");
        if (id) {
            printf("%s\n", id);
        }
        json_free(json);
    }

    http_response_free(resp);
    return 0;
}

/* Command: list */
static int cmd_list(int argc, char **argv) {
    (void)argc;
    (void)argv;

    http_response_t *resp = http_client_get(g_host, g_port, "/api/sessions", g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to list sessions\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: info */
static int cmd_info(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: info SESSION_ID\n");
        return 1;
    }

    const char *session_id = argv[0];
    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s", session_id);

    http_response_t *resp = http_client_get(g_host, g_port, path, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to get session info\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: send */
static int cmd_send(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: send SESSION_ID TEXT\n");
        return 1;
    }

    const char *session_id = argv[0];
    const char *text = argv[1];

    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s/send", session_id);

    char body[4096];
    snprintf(body, sizeof(body), "{\"text\":\"%s\"}", text);

    http_response_t *resp = http_client_post(g_host, g_port, path, body, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to send input\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: output */
static int cmd_output(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: output SESSION_ID\n");
        return 1;
    }

    const char *session_id = argv[0];
    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s/output", session_id);

    http_response_t *resp = http_client_get(g_host, g_port, path, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to read output\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    /* Parse and print output */
    json_value_t *json = json_parse(resp->body);
    if (json) {
        const char *output = json_object_get_string(json, "output");
        if (output && strlen(output) > 0) {
            printf("%s", output);
        }
        json_free(json);
    }

    http_response_free(resp);
    return 0;
}

/* Command: screen */
static int cmd_screen(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: screen SESSION_ID\n");
        return 1;
    }

    const char *session_id = argv[0];
    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s/screen", session_id);

    http_response_t *resp = http_client_get(g_host, g_port, path, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to get screen\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: resize */
static int cmd_resize(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: resize SESSION_ID --rows ROWS --cols COLS\n");
        return 1;
    }

    const char *session_id = argv[0];
    int rows = 0;
    int cols = 0;

    struct option long_options[] = {
        {"rows", required_argument, 0, 'r'},
        {"cols", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "r:c:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                if (!safe_atoi(optarg, &rows) || rows <= 0) {
                    fprintf(stderr, "Invalid --rows value\n");
                    return 1;
                }
                break;
            case 'c':
                if (!safe_atoi(optarg, &cols) || cols <= 0) {
                    fprintf(stderr, "Invalid --cols value\n");
                    return 1;
                }
                break;
                break;
        }
    }

    if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "Invalid rows or cols\n");
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s/resize", session_id);

    char body[256];
    snprintf(body, sizeof(body), "{\"rows\":%d,\"cols\":%d}", rows, cols);

    http_response_t *resp = http_client_post(g_host, g_port, path, body, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to resize terminal\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: expect */
static int cmd_expect(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: expect SESSION_ID --pattern PATTERN [--timeout MS] [--case-sensitive]\n");
        return 1;
    }

    const char *session_id = argv[0];
    const char *pattern = NULL;
    int timeout = 30000;
    int case_sensitive = 1;

    struct option long_options[] = {
        {"pattern", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"case-sensitive", no_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "p:t:s", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                pattern = optarg;
                break;
            case 't':
                if (!safe_atoi(optarg, &timeout) || timeout <= 0) {
                    fprintf(stderr, "Invalid --timeout value\n");
                    return 1;
                }
                break;
            case 's':
                case_sensitive = 1;
                break;
        }
    }

    if (!pattern) {
        fprintf(stderr, "Pattern is required\n");
        return 1;
    }

    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s/expect", session_id);

    char body[4096];
    snprintf(body, sizeof(body),
             "{\"pattern\":\"%s\",\"timeout\":%d,\"case_sensitive\":%s}",
             pattern, timeout, case_sensitive ? "true" : "false");

    http_response_t *resp = http_client_post(g_host, g_port, path, body, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to expect pattern\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: destroy */
static int cmd_destroy(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: destroy SESSION_ID\n");
        return 1;
    }

    const char *session_id = argv[0];
    char path[256];
    snprintf(path, sizeof(path), "/api/sessions/%s", session_id);

    http_response_t *resp = http_client_delete(g_host, g_port, path, g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to destroy session\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("Session destroyed\n");
    http_response_free(resp);
    return 0;
}

/* Command: status */
static int cmd_status(int argc, char **argv) {
    (void)argc;
    (void)argv;

    http_response_t *resp = http_client_get(g_host, g_port, "/api/status", g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to get status\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Command: health */
static int cmd_health(int argc, char **argv) {
    (void)argc;
    (void)argv;

    http_response_t *resp = http_client_get(g_host, g_port, "/api/health", g_api_key);
    if (!resp) {
        fprintf(stderr, "Failed to check health\n");
        return 1;
    }

    if (!http_response_is_success(resp)) {
        fprintf(stderr, "Error: %s\n", resp->error ? resp->error : "Unknown error");
        http_response_free(resp);
        return 1;
    }

    printf("%s\n", resp->body);
    http_response_free(resp);
    return 0;
}

/* Main entry point */
int main(int argc, char **argv) {
    g_host = DEFAULT_HOST;
    g_port = DEFAULT_PORT;

    /* Parse global options */
    struct option long_options[] = {
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"api-key", required_argument, 0, 'k'},
        {"help", no_argument, 0, 'H'},
        {0, 0, 0, 0}
    };

    int opt;
    opterr = 0;  /* Disable error messages for unknown options (subcommand options) */
    while ((opt = getopt_long(argc, argv, "+h:p:k:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                g_host = optarg;
                break;
            case 'p':
                if (!safe_atoi(optarg, &g_port) || g_port <= 0 || g_port > 65535) {
                    fprintf(stderr, "Invalid --port value (1-65535)\n");
                    return 1;
                }
                break;
            case 'k':
                g_api_key = optarg;
                break;
            case 'H':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    opterr = 1;  /* Re-enable error messages */

    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[optind];
    
    /* Skip command name, pass remaining args to subcommand */
    int cmd_argc = argc - optind - 1;
    char **cmd_argv = argv + optind + 1;

    /* Dispatch command */
    if (strcmp(command, "create") == 0) {
        return cmd_create(cmd_argc, cmd_argv);
    } else if (strcmp(command, "list") == 0) {
        return cmd_list(cmd_argc, cmd_argv);
    } else if (strcmp(command, "info") == 0) {
        return cmd_info(cmd_argc, cmd_argv);
    } else if (strcmp(command, "send") == 0) {
        return cmd_send(cmd_argc, cmd_argv);
    } else if (strcmp(command, "output") == 0) {
        return cmd_output(cmd_argc, cmd_argv);
    } else if (strcmp(command, "screen") == 0) {
        return cmd_screen(cmd_argc, cmd_argv);
    } else if (strcmp(command, "resize") == 0) {
        return cmd_resize(cmd_argc, cmd_argv);
    } else if (strcmp(command, "expect") == 0) {
        return cmd_expect(cmd_argc, cmd_argv);
    } else if (strcmp(command, "destroy") == 0) {
        return cmd_destroy(cmd_argc, cmd_argv);
    } else if (strcmp(command, "status") == 0) {
        return cmd_status(cmd_argc, cmd_argv);
    } else if (strcmp(command, "health") == 0) {
        return cmd_health(cmd_argc, cmd_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
}