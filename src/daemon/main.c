#include "pshell/logger.h"
#include "pshell/config.h"
#include "pshell/pty_posix.h"
#include "pshell/process_spawn.h"
#include "pshell/io_multiplexer.h"
#include "pshell/http_server.h"
#include "pshell/rest_api.h"
#include "pshell/socket_server.h"
#include "pshell/json_simple.h"
#include "pshell/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#define DEFAULT_CONFIG_FILE "/etc/pshell/pshell.conf"
#define DEFAULT_PID_FILE "/var/run/pshell.pid"

/* Global state */
static volatile bool g_running = true;
static config_t *g_config = NULL;
static io_multiplexer_t *g_mux = NULL;
static http_server_t *g_http_server = NULL;
static rest_api_t *g_rest_api = NULL;
static socket_server_t *g_socket_server = NULL;
static session_manager_t *g_session_mgr = NULL;

/* HTTP server event callback */
static void http_server_callback(int fd, io_event_type_t events, void *user_data) {
    (void)fd;
    (void)events;
    http_server_t *server = (http_server_t*)user_data;
    http_server_handle_connection(server);
}

/* Socket server event callback */
static void socket_server_callback(int fd, io_event_type_t events, void *user_data) {
    (void)fd;
    (void)events;
    socket_server_t *server = (socket_server_t*)user_data;
    socket_server_handle_connection(server);
}

/* Socket protocol handler (implemented in socket_protocol.c) */
extern void socket_protocol_handler(socket_connection_t *conn, const char *message, void *user_data);

/* Signal handler */
static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            log_info("Received signal %d, shutting down...", sig);
            g_running = false;
            if (g_mux) {
                io_multiplexer_stop(g_mux);
            }
            break;
        case SIGHUP:
            log_info("Received SIGHUP, reloading configuration...");
            // TODO: Implement config reload
            break;
        default:
            break;
    }
}

/* Setup signal handlers */
static bool setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGTERM, &sa, NULL) < 0 ||
        sigaction(SIGINT, &sa, NULL) < 0 ||
        sigaction(SIGHUP, &sa, NULL) < 0) {
        fprintf(stderr, "Failed to setup signal handlers: %s\n", strerror(errno));
        return false;
    }
    
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    return true;
}

/* Write PID file */
static bool write_pid_file(const char *pid_file) {
    FILE *fp = fopen(pid_file, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create PID file %s: %s\n",
                pid_file, strerror(errno));
        return false;
    }
    
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    
    return true;
}

/* Remove PID file */
static void remove_pid_file(const char *pid_file) {
    unlink(pid_file);
}

/* Daemonize process */
static bool daemonize(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        fprintf(stderr, "Fork failed: %s\n", strerror(errno));
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }
    
    // Child process continues
    
    // Create new session
    if (setsid() < 0) {
        fprintf(stderr, "setsid failed: %s\n", strerror(errno));
        return false;
    }
    
    // Change working directory to root
    if (chdir("/") < 0) {
        fprintf(stderr, "chdir failed: %s\n", strerror(errno));
        return false;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Reopen to /dev/null
    open("/dev/null", O_RDONLY);  // stdin
    open("/dev/null", O_WRONLY);  // stdout
    open("/dev/null", O_WRONLY);  // stderr
    
    return true;
}

/* Print usage */
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -c FILE    Configuration file (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -f         Run in foreground (don't daemonize)\n");
    printf("  -h         Show this help message\n");
    printf("  -v         Show version information\n");
    printf("\n");
}

/* Print version */
static void print_version(void) {
    printf("pshell version 0.1.0\n");
    printf("Portable Programmable Shell with REST API\n");
}

/* Main entry point */
int main(int argc, char **argv) {
    const char *config_file = DEFAULT_CONFIG_FILE;
    bool foreground = false;
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "c:fhv")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case 'f':
                foreground = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Load configuration
    g_config = config_load(config_file);
    if (!g_config) {
        fprintf(stderr, "Failed to load configuration from %s\n", config_file);
        fprintf(stderr, "Using default configuration\n");
    }
    
    // Get configuration values
    const char *pid_file = config_get_string(g_config, "daemon", "pid_file", DEFAULT_PID_FILE);
    const char *log_file = config_get_string(g_config, "daemon", "log_file", "/var/log/pshell.log");
    const char *log_level_str = config_get_string(g_config, "daemon", "log_level", "info");
    bool use_syslog = config_get_bool(g_config, "daemon", "use_syslog", true);
    
    log_level_t log_level = logger_string_to_level(log_level_str);
    
    // Initialize logger (before daemonizing so we can see errors)
    if (!logger_init(log_file, use_syslog, log_level)) {
        fprintf(stderr, "Failed to initialize logger\n");
        config_free(g_config);
        return 1;
    }
    
    log_info("Starting pshell daemon...");
    log_info("Configuration file: %s", config_file);
    log_info("PID file: %s", pid_file);
    log_info("Log file: %s", log_file);
    log_info("Log level: %s", log_level_str);
    
    // Setup signal handlers
    if (!setup_signals()) {
        log_error("Failed to setup signal handlers");
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Daemonize if not in foreground mode
    if (!foreground) {
        log_info("Daemonizing...");
        if (!daemonize()) {
            log_error("Failed to daemonize");
            logger_shutdown();
            config_free(g_config);
            return 1;
        }
    }
    
    // Write PID file
    if (!write_pid_file(pid_file)) {
        log_error("Failed to write PID file");
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    log_info("pshell daemon started (PID: %d)", getpid());
    
    // Create I/O multiplexer
    g_mux = io_multiplexer_create();
    if (!g_mux) {
        log_error("Failed to create I/O multiplexer");
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Initialize HTTP server
    const char *bind_addr = config_get_string(g_config, "http", "bind_address", "127.0.0.1");
    int http_port = config_get_int(g_config, "http", "port", 8080);
    
    g_http_server = http_server_create(bind_addr, http_port);
    if (!g_http_server) {
        log_error("Failed to create HTTP server");
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    
    /* Set static file directory for web interface */
    const char *static_dir = config_get_string(g_config, "http", "static_dir", "web");
    http_server_set_static_dir(g_http_server, static_dir);
    log_info("Static files directory: %s", static_dir);

    // Initialize session manager first
    int max_sessions = config_get_int(g_config, "sessions", "max_sessions", 50);
    g_session_mgr = session_manager_create(max_sessions);
    if (!g_session_mgr) {
        log_error("Failed to create session manager");
        http_server_destroy(g_http_server);
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    log_info("Session manager initialized (max_sessions=%d)", max_sessions);
    
    // Initialize REST API with session manager
    g_rest_api = rest_api_create(g_http_server, g_session_mgr);
    if (!g_rest_api) {
        log_error("Failed to create REST API");
        session_manager_destroy(g_session_mgr);
        http_server_destroy(g_http_server);
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Set API key if configured
    const char *api_key = config_get_string(g_config, "http", "api_key", NULL);
    if (api_key) {
        rest_api_set_key(g_rest_api, api_key);
        log_info("API key authentication enabled");
    }
    
    // Register REST API routes
    if (!rest_api_register_routes(g_rest_api)) {
        log_error("Failed to register REST API routes");
        rest_api_destroy(g_rest_api);
        http_server_destroy(g_http_server);
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Start HTTP server
    if (!http_server_start(g_http_server)) {
        log_error("Failed to start HTTP server");
        rest_api_destroy(g_rest_api);
        http_server_destroy(g_http_server);
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Add HTTP server to multiplexer
    int http_fd = http_server_get_fd(g_http_server);
    if (!io_multiplexer_add(g_mux, http_fd, IO_EVENT_READ, http_server_callback, g_http_server)) {
        log_error("Failed to add HTTP server to multiplexer");
        http_server_stop(g_http_server);
        rest_api_destroy(g_rest_api);
        http_server_destroy(g_http_server);
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    log_info("HTTP server listening on %s:%d", bind_addr, http_port);
    
    // Initialize Unix socket server
    bool socket_enabled = config_get_bool(g_config, "socket", "enabled", true);
    if (socket_enabled) {
        const char *socket_path = config_get_string(g_config, "socket", "socket_path", "/tmp/pshell.sock");
        int socket_perms = config_get_int(g_config, "socket", "socket_permissions", 0600);
        
        g_socket_server = socket_server_create(socket_path);
        if (!g_socket_server) {
            log_error("Failed to create socket server");
            http_server_stop(g_http_server);
            rest_api_destroy(g_rest_api);
            http_server_destroy(g_http_server);
            io_multiplexer_destroy(g_mux);
            remove_pid_file(pid_file);
            logger_shutdown();
            config_free(g_config);
            return 1;
        }
        
        socket_server_set_permissions(g_socket_server, socket_perms);
    socket_server_set_handler(g_socket_server, socket_protocol_handler, g_session_mgr);
        
        if (!socket_server_start(g_socket_server)) {
            log_error("Failed to start socket server");
            socket_server_destroy(g_socket_server);
            http_server_stop(g_http_server);
            rest_api_destroy(g_rest_api);
            http_server_destroy(g_http_server);
            io_multiplexer_destroy(g_mux);
            remove_pid_file(pid_file);
            logger_shutdown();
            config_free(g_config);
            return 1;
        }
        
        // Add socket server to multiplexer
        int socket_fd = socket_server_get_fd(g_socket_server);
        if (!io_multiplexer_add(g_mux, socket_fd, IO_EVENT_READ, socket_server_callback, g_socket_server)) {
            log_error("Failed to add socket server to multiplexer");
            socket_server_stop(g_socket_server);
            socket_server_destroy(g_socket_server);
            http_server_stop(g_http_server);
            rest_api_destroy(g_rest_api);
            http_server_destroy(g_http_server);
            io_multiplexer_destroy(g_mux);
            remove_pid_file(pid_file);
            logger_shutdown();
            config_free(g_config);
            return 1;
        }
        
    log_info("Socket server listening on %s", socket_path);
    } else {
        log_info("Socket server disabled");
    }
    
    log_info("Entering main event loop...");
    
    // Main event loop
    while (g_running) {
        int ret = io_multiplexer_run(g_mux, 1000);  // 1 second timeout
        
        if (ret < 0) {
            log_error("I/O multiplexer error");
            break;
        }
        
        // Events are handled by callbacks
        // TODO: Check for dead sessions
        // TODO: Handle timeouts
    }
    
    log_info("Shutting down pshell daemon...");
    
    // Cleanup
    if (g_mux) {
        io_multiplexer_destroy(g_mux);
        g_mux = NULL;
    }
    
    remove_pid_file(pid_file);
    logger_shutdown();
    config_free(g_config);
    
    log_info("pshell daemon stopped");
    
    return 0;
}
