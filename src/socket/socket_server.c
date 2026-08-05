#include "pshell/socket_server.h"
#include "pshell/expect_engine.h"
#include "pshell/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <arpa/inet.h>

#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 8192

/* Socket connection structure */
struct socket_connection {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
    void *expect_ctx;  /* Active expect_context_t pointer (for cancellation on disconnect) */
};

/* Socket server structure */
struct socket_server {
    int listen_fd;
    char *socket_path;
    socket_handler_t handler;
    void *user_data;
    int permissions;
    bool running;
    socket_connection_t connections[MAX_CONNECTIONS];
    int connection_count;
};

socket_server_t* socket_server_create(const char *socket_path) {
    if (!socket_path) {
        log_error("Invalid socket path");
        return NULL;
    }
    
    socket_server_t *server = calloc(1, sizeof(socket_server_t));
    if (!server) {
        log_error("Failed to allocate socket server");
        return NULL;
    }
    
    server->socket_path = strdup(socket_path);
    server->listen_fd = -1;
    server->permissions = 0600;  /* Default: owner only */
    server->running = false;
    
    log_info("Created socket server: %s", socket_path);
    return server;
}

void socket_server_destroy(socket_server_t *server) {
    if (!server) return;
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        unlink(server->socket_path);
    }
    
    /* Close all connections */
    for (int i = 0; i < server->connection_count; i++) {
        if (server->connections[i].fd >= 0) {
            close(server->connections[i].fd);
        }
    }
    
    free(server->socket_path);
    free(server);
    
    log_info("Destroyed socket server");
}

bool socket_server_start(socket_server_t *server) {
    if (!server) return false;
    
    fprintf(stderr, "DEBUG: socket_server_start: entry\n");
    fflush(stderr);
    
    /* Remove existing socket file */
    unlink(server->socket_path);
    
    fprintf(stderr, "DEBUG: socket_server_start: creating socket\n");
    fflush(stderr);
    
    /* Create socket */
    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        fprintf(stderr, "DEBUG: socket() failed: %s\n", strerror(errno));
        fflush(stderr);
        return false;
    }
    
    fprintf(stderr, "DEBUG: socket_server_start: binding\n");
    fflush(stderr);
    
    /* Bind */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server->socket_path, sizeof(addr.sun_path) - 1);
    
    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "DEBUG: bind() failed: %s\n", strerror(errno));
        fflush(stderr);
        close(server->listen_fd);
        server->listen_fd = -1;
        return false;
    }
    
    fprintf(stderr, "DEBUG: socket_server_start: setting permissions\n");
    fflush(stderr);
    
    /* Set permissions */
    if (chmod(server->socket_path, server->permissions) < 0) {
        fprintf(stderr, "DEBUG: chmod() failed: %s\n", strerror(errno));
        fflush(stderr);
        close(server->listen_fd);
        unlink(server->socket_path);
        server->listen_fd = -1;
        return false;
    }
    
    fprintf(stderr, "DEBUG: socket_server_start: listening\n");
    fflush(stderr);
    
    /* Listen */
    if (listen(server->listen_fd, MAX_CONNECTIONS) < 0) {
        fprintf(stderr, "DEBUG: listen() failed: %s\n", strerror(errno));
        fflush(stderr);
        close(server->listen_fd);
        unlink(server->socket_path);
        server->listen_fd = -1;
        return false;
    }
    
    fprintf(stderr, "DEBUG: socket_server_start: setting non-blocking\n");
    fflush(stderr);
    
    /* Set non-blocking */
    int flags = fcntl(server->listen_fd, F_GETFL, 0);
    fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    fprintf(stderr, "DEBUG: socket_server_start: about to call log_info\n");
    fflush(stderr);
    
    server->running = true;
    log_info("Socket server listening on %s", server->socket_path);
    
    fprintf(stderr, "DEBUG: socket_server_start: completed successfully\n");
    fflush(stderr);
    
    return true;
}

void socket_server_stop(socket_server_t *server) {
    if (!server) return;
    
    server->running = false;
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        unlink(server->socket_path);
        server->listen_fd = -1;
    }
    
    /* Close all connections */
    for (int i = 0; i < server->connection_count; i++) {
        if (server->connections[i].fd >= 0) {
            close(server->connections[i].fd);
            server->connections[i].fd = -1;
        }
    }
    server->connection_count = 0;
    
    log_info("Socket server stopped");
}

bool socket_server_set_handler(socket_server_t *server, socket_handler_t handler, void *user_data) {
    if (!server) return false;
    
    server->handler = handler;
    server->user_data = user_data;
    
    return true;
}

bool socket_server_set_permissions(socket_server_t *server, int mode) {
    if (!server) return false;
    
    server->permissions = mode;
    
    /* If already running, update permissions */
    if (server->listen_fd >= 0) {
        if (chmod(server->socket_path, mode) < 0) {
            log_error("Failed to set socket permissions: %s", strerror(errno));
            return false;
        }
    }
    
    return true;
}

int socket_server_get_fd(const socket_server_t *server) {
    return server ? server->listen_fd : -1;
}

bool socket_server_handle_connection(socket_server_t *server) {
    if (!server || server->listen_fd < 0) return false;
    
    /* Accept connection */
    int client_fd = accept(server->listen_fd, NULL, NULL);
    
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Accept failed: %s", strerror(errno));
        }
        return false;
    }
    
    /* Set socket timeout (5 seconds) */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    /* Read message length (4 bytes, network byte order) */
    uint32_t msg_len_net;
    ssize_t n = read(client_fd, &msg_len_net, sizeof(msg_len_net));
    
    if (n != sizeof(msg_len_net)) {
        if (n < 0) {
            log_error("Failed to read message length: %s", strerror(errno));
        }
        close(client_fd);
        return false;
    }
    
    size_t msg_len = ntohl(msg_len_net);
    
    /* Validate message length */
    if (msg_len == 0 || msg_len >= BUFFER_SIZE) {
        log_error("Invalid message length: %zu", msg_len);
        close(client_fd);
        return false;
    }
    
    /* Read message */
    char buffer[BUFFER_SIZE];
    n = read(client_fd, buffer, msg_len);
    
    if (n != (ssize_t)msg_len) {
        log_error("Failed to read complete message: got %zd, expected %zu", n, msg_len);
        close(client_fd);
        return false;
    }
    
    buffer[n] = '\0';
    
    /* Create connection object */
    socket_connection_t conn;
    conn.fd = client_fd;
    memcpy(conn.buffer, buffer, n + 1);
    conn.buffer_len = n;
    conn.expect_ctx = NULL;
    
    /* Call handler */
    if (server->handler) {
        server->handler(&conn, buffer, server->user_data);
    }
    
    /* Cancel any active expect operation before closing */
    if (conn.expect_ctx) {
        expect_cancel((expect_context_t*)conn.expect_ctx);
    }
    
    /* Ensure data is sent before closing */
    fsync(client_fd);
    shutdown(client_fd, SHUT_WR);
    
    /* Close connection */
    close(client_fd);
    
    return true;
}

bool socket_connection_send(socket_connection_t *conn, const char *message) {
    if (!conn || conn->fd < 0 || !message) return false;
    
    size_t len = strlen(message);
    ssize_t written = write(conn->fd, message, len);
    
    if (written < 0) {
        log_error("Failed to send response: %s", strerror(errno));
        return false;
    }
    
    if ((size_t)written != len) {
        log_warn("Partial write: %zd of %zu bytes", written, len);
        return false;
    }
    
    return true;
}

int socket_connection_get_fd(const socket_connection_t *conn) {
    return conn ? conn->fd : -1;
}

void socket_connection_set_expect_ctx(socket_connection_t *conn, void *ctx) {
    if (!conn) return;
    conn->expect_ctx = ctx;
}
