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
static char g_default_shell[256] = "/bin/sh";

/* Get default shell from configuration (non-static for external use) */
const char* get_default_shell(void) {
    return g_default_shell;
}

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

/* Export default shell getter for REST API and socket protocol */
extern const char* get_default_shell(void);

/* Signal handler */
static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            /* CRITICAL: Do NOT call log_info() or any non-async-signal-safe functions here!
             * Calling log_info() from signal handler causes deadlock if signal interrupts
             * another log_info() call (logger mutex already held by interrupted thread).
             * Only set flags and use async-signal-safe functions like write(). */
            g_running = false;
            if (g_mux) {
                io_multiplexer_stop(g_mux);
            }
            break;
        case SIGHUP:
            /* Same rule: no log_info() in signal handlers */
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
    fprintf(stderr, "DEBUG: write_pid_file: opening %s\n", pid_file);
    fflush(stderr);
    FILE *fp = fopen(pid_file, "w");
    fprintf(stderr, "DEBUG: write_pid_file: fopen returned %p\n", (void*)fp);
    fflush(stderr);
    if (!fp) {
        fprintf(stderr, "Failed to create PID file %s: %s\n",
                pid_file, strerror(errno));
        return false;
    }
    
    fprintf(stderr, "DEBUG: write_pid_file: writing PID %d\n", getpid());
    fflush(stderr);
    fprintf(fp, "%d\n", getpid());
    fprintf(stderr, "DEBUG: write_pid_file: closing file\n");
    fflush(stderr);
    fclose(fp);
    fprintf(stderr, "DEBUG: write_pid_file: done\n");
    fflush(stderr);
    
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
    /* Use write() instead of fprintf() to avoid buffering issues */
    const char *msg1 = "=== PSHELL DAEMON STARTING ===\n";
    write(STDERR_FILENO, msg1, strlen(msg1));
    
    const char *config_file = DEFAULT_CONFIG_FILE;
    bool foreground = false;
    
    const char *msg2 = "Parsing arguments...\n";
    write(STDERR_FILENO, msg2, strlen(msg2));
    
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
    fprintf(stderr, "Loading config from: %s\n", config_file);
    fflush(stderr);
    g_config = config_load(config_file);
    fprintf(stderr, "Config loaded: %s\n", g_config ? "SUCCESS" : "FAILED");
    fflush(stderr);
    if (!g_config) {
        fprintf(stderr, "Failed to load configuration from %s\n", config_file);
        fprintf(stderr, "Using default configuration\n");
    }
    
    // Get configuration values (handle NULL config gracefully)
    const char *pid_file = g_config ? config_get_string(g_config, "daemon", "pid_file", DEFAULT_PID_FILE) : DEFAULT_PID_FILE;
    const char *log_file = g_config ? config_get_string(g_config, "daemon", "log_file", "/var/log/pshell.log") : "/var/log/pshell.log";
    const char *log_level_str = g_config ? config_get_string(g_config, "daemon", "log_level", "info") : "info";
    bool use_syslog = g_config ? config_get_bool(g_config, "daemon", "use_syslog", true) : true;
    
    fprintf(stderr, "Config values retrieved\n");
    fflush(stderr);
    
    // Get default shell for sessions (copy to static buffer to avoid dangling pointer)
    fprintf(stderr, "Getting default shell...\n");
    fflush(stderr);
    const char *shell_config = g_config ? config_get_string(g_config, "sessions", "default_shell", "/bin/sh") : "/bin/sh";
    strncpy(g_default_shell, shell_config, sizeof(g_default_shell) - 1);
    g_default_shell[sizeof(g_default_shell) - 1] = '\0';
    fprintf(stderr, "Default shell: %s\n", g_default_shell);
    fflush(stderr);
    
    fprintf(stderr, "Converting log level...\n");
    fflush(stderr);
    
    log_level_t log_level = logger_string_to_level(log_level_str);
    
    fprintf(stderr, "Initializing logger...\n");
    fflush(stderr);
    
    // Initialize logger (before daemonizing so we can see errors)
    if (!logger_init(log_file, use_syslog, log_level)) {
        fprintf(stderr, "Failed to initialize logger\n");
        config_free(g_config);
        return 1;
    }
    
    fprintf(stderr, "DEBUG: Logger initialized, about to call log_info\n");
    fflush(stderr);
    log_info("Starting pshell daemon...");
    fprintf(stderr, "DEBUG: log_info completed\n");
    fflush(stderr);
    log_info("Configuration file: %s", config_file);
    log_info("PID file: %s", pid_file);
    log_info("Log file: %s", log_file);
    log_info("Log level: %s", log_level_str);
    
    // Setup signal handlers
    fprintf(stderr, "DEBUG: About to setup signals\n");
    fflush(stderr);
    if (!setup_signals()) {
        log_error("Failed to setup signal handlers");
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    fprintf(stderr, "DEBUG: Signals setup complete\n");
    fflush(stderr);
    
    // Daemonize if not in foreground mode
    fprintf(stderr, "DEBUG: foreground=%d, checking daemonize\n", foreground);
    fflush(stderr);
    if (!foreground) {
        log_info("Daemonizing...");
        if (!daemonize()) {
            log_error("Failed to daemonize");
            logger_shutdown();
            config_free(g_config);
            return 1;
        }
    }
    fprintf(stderr, "DEBUG: Skipped daemonize, writing PID file\n");
    fflush(stderr);
    
    // Write PID file
    fprintf(stderr, "DEBUG: About to write PID file: %s\n", pid_file);
    fflush(stderr);
    if (!write_pid_file(pid_file)) {
        log_error("Failed to write PID file");
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    fprintf(stderr, "DEBUG: About to log daemon started\n");
    fflush(stderr);
    log_info("pshell daemon started (PID: %d)", getpid());
    fprintf(stderr, "DEBUG: Logged daemon started, creating I/O multiplexer\n");
    fflush(stderr);
    
    // Create I/O multiplexer
    g_mux = io_multiplexer_create();
    fprintf(stderr, "DEBUG: I/O multiplexer created: %p\n", (void*)g_mux);
    fflush(stderr);
    if (!g_mux) {
        log_error("Failed to create I/O multiplexer");
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    // Initialize HTTP server
    fprintf(stderr, "DEBUG: About to get HTTP config\n");
    fflush(stderr);
    const char *bind_addr = config_get_string(g_config, "http", "bind_address", "127.0.0.1");
    int http_port = config_get_int(g_config, "http", "port", 8080);
    
    fprintf(stderr, "DEBUG: Creating HTTP server on %s:%d\n", bind_addr, http_port);
    fflush(stderr);
    g_http_server = http_server_create(bind_addr, http_port);
    fprintf(stderr, "DEBUG: HTTP server created: %p\n", (void*)g_http_server);
    fflush(stderr);
    if (!g_http_server) {
        log_error("Failed to create HTTP server");
        io_multiplexer_destroy(g_mux);
        remove_pid_file(pid_file);
        logger_shutdown();
        config_free(g_config);
        return 1;
    }
    
    
    /* Set static file directory for web interface */
    fprintf(stderr, "DEBUG: About to get static_dir config\n");
    fflush(stderr);
    const char *static_dir = config_get_string(g_config, "http", "static_dir", "web");
    fprintf(stderr, "DEBUG: static_dir=%s, about to call http_server_set_static_dir\n", static_dir);
    fflush(stderr);
    http_server_set_static_dir(g_http_server, static_dir);
    fprintf(stderr, "DEBUG: http_server_set_static_dir completed\n");
    fflush(stderr);
    log_info("Static files directory: %s", static_dir);
    fprintf(stderr, "DEBUG: Logged static dir\n");
    fflush(stderr);

    // Initialize session manager first
    fprintf(stderr, "DEBUG: About to get max_sessions config\n");
    fflush(stderr);
    int max_sessions = config_get_int(g_config, "sessions", "max_sessions", 50);
    fprintf(stderr, "DEBUG: max_sessions=%d, creating session manager\n", max_sessions);
    fflush(stderr);
    g_session_mgr = session_manager_create(max_sessions, g_mux);
    fprintf(stderr, "DEBUG: Session manager created: %p\n", (void*)g_session_mgr);
    fflush(stderr);
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
    fprintf(stderr, "DEBUG: About to create REST API\n");
    fflush(stderr);
    g_rest_api = rest_api_create(g_http_server, g_session_mgr);
    fprintf(stderr, "DEBUG: REST API created: %p\n", (void*)g_rest_api);
    fflush(stderr);
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
    fprintf(stderr, "DEBUG: About to get API key config\n");
    fflush(stderr);
    const char *api_key = config_get_string(g_config, "http", "api_key", NULL);
    fprintf(stderr, "DEBUG: api_key=%s\n", api_key ? "SET" : "NULL");
    fflush(stderr);
    if (api_key) {
        fprintf(stderr, "DEBUG: Setting API key\n");
        fflush(stderr);
        rest_api_set_key(g_rest_api, api_key);
        log_info("API key authentication enabled");
        fprintf(stderr, "DEBUG: API key set\n");
        fflush(stderr);
    }
    
    // Register REST API routes
    fprintf(stderr, "DEBUG: About to register REST API routes\n");
    fflush(stderr);
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
    
    fprintf(stderr, "DEBUG: REST API routes registered successfully\n");
    fflush(stderr);
    
    // Start HTTP server
    fprintf(stderr, "DEBUG: About to start HTTP server\n");
    fflush(stderr);
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
    
    fprintf(stderr, "DEBUG: HTTP server started, getting fd\n");
    fflush(stderr);
    
    // Add HTTP server to multiplexer
    int http_fd = http_server_get_fd(g_http_server);
    fprintf(stderr, "DEBUG: Got http_fd=%d, adding to multiplexer\n", http_fd);
    fflush(stderr);
    
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
    
    fprintf(stderr, "DEBUG: Added to multiplexer, about to log_info\n");
    fflush(stderr);
    log_info("HTTP server listening on %s:%d", bind_addr, http_port);
    fprintf(stderr, "DEBUG: log_info completed\n");
    fflush(stderr);
    
    // Initialize Unix socket server
    fprintf(stderr, "DEBUG: About to check socket_enabled config\n");
    fflush(stderr);
    bool socket_enabled = config_get_bool(g_config, "socket", "enabled", true);
    fprintf(stderr, "DEBUG: socket_enabled=%d\n", socket_enabled);
    fflush(stderr);
    if (socket_enabled) {
        fprintf(stderr, "DEBUG: About to get socket_path config\n");
        fflush(stderr);
        const char *socket_path = config_get_string(g_config, "socket", "socket_path", "/tmp/pshell.sock");
        fprintf(stderr, "DEBUG: socket_path=%s\n", socket_path);
        fflush(stderr);
        int socket_perms = config_get_int(g_config, "socket", "socket_permissions", 0600);
        fprintf(stderr, "DEBUG: socket_perms=%o, creating socket server\n", socket_perms);
        fflush(stderr);
        
        g_socket_server = socket_server_create(socket_path);
        fprintf(stderr, "DEBUG: socket_server_create returned: %p\n", (void*)g_socket_server);
        fflush(stderr);
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
        
        fprintf(stderr, "DEBUG: About to set socket permissions\n");
        fflush(stderr);
        socket_server_set_permissions(g_socket_server, socket_perms);
        fprintf(stderr, "DEBUG: Permissions set, about to set handler\n");
        fflush(stderr);
        socket_server_set_handler(g_socket_server, socket_protocol_handler, g_session_mgr);
        fprintf(stderr, "DEBUG: Handler set, about to start socket server\n");
        fflush(stderr);
        
        fprintf(stderr, "DEBUG: Calling socket_server_start at %p\n", (void*)socket_server_start);
        fflush(stderr);
        bool start_result = socket_server_start(g_socket_server);
        fprintf(stderr, "DEBUG: socket_server_start returned: %d\n", start_result);
        fflush(stderr);
        
        if (!start_result) {
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
        fprintf(stderr, "DEBUG: About to get socket_fd\n");
        fflush(stderr);
        int socket_fd = socket_server_get_fd(g_socket_server);
        fprintf(stderr, "DEBUG: socket_fd=%d, adding to multiplexer\n", socket_fd);
        fflush(stderr);
        
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
        
        fprintf(stderr, "DEBUG: Added to multiplexer, about to log socket listening\n");
        fflush(stderr);
        log_info("Socket server listening on %s", socket_path);
        fprintf(stderr, "DEBUG: Logged socket listening\n");
        fflush(stderr);
    } else {
        fprintf(stderr, "DEBUG: Socket server disabled\n");
        fflush(stderr);
    }
    
    fprintf(stderr, "DEBUG: About to log 'Entering main event loop'\n");
    fflush(stderr);
    log_info("Entering main event loop...");
    fprintf(stderr, "DEBUG: Logged 'Entering main event loop', starting loop\n");
    fflush(stderr);
    
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
