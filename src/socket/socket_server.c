#include "pshell/socket_server.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 8192

/* Socket connection structure */
struct socket_connection {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
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
    
    /* Remove existing socket file */
    unlink(server->socket_path);
    
    /* Create socket */
    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        log_error("Failed to create socket: %s", strerror(errno));
        return false;
    }
    
    /* Bind */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server->socket_path, sizeof(addr.sun_path) - 1);
    
    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind socket %s: %s",
                  server->socket_path, strerror(errno));
        close(server->listen_fd);
        server->listen_fd = -1;
        return false;
    }
    
    /* Set permissions */
    if (chmod(server->socket_path, server->permissions) < 0) {
        log_error("Failed to set socket permissions: %s", strerror(errno));
        close(server->listen_fd);
        unlink(server->socket_path);
        server->listen_fd = -1;
        return false;
    }
    
    /* Listen */
    if (listen(server->listen_fd, MAX_CONNECTIONS) < 0) {
        log_error("Failed to listen on socket: %s", strerror(errno));
        close(server->listen_fd);
        unlink(server->socket_path);
        server->listen_fd = -1;
        return false;
    }
    
    /* Set non-blocking */
    int flags = fcntl(server->listen_fd, F_GETFL, 0);
    fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    server->running = true;
    log_info("Socket server listening on %s", server->socket_path);
    
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
    
    /* Set non-blocking */
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    /* Read message */
    char buffer[BUFFER_SIZE];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (n <= 0) {
        close(client_fd);
        return false;
    }
    
    buffer[n] = '\0';
    
    /* Create connection object */
    socket_connection_t conn;
    conn.fd = client_fd;
    memcpy(conn.buffer, buffer, n + 1);
    conn.buffer_len = n;
    
    /* Call handler */
    if (server->handler) {
        server->handler(&conn, buffer, server->user_data);
    }
    
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
