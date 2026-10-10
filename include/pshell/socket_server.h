#ifndef PSHELL_SOCKET_SERVER_H
#define PSHELL_SOCKET_SERVER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Unix domain socket server for pshell
 * Provides local control interface via socket
 */

typedef struct socket_server socket_server_t;
typedef struct socket_connection socket_connection_t;

/* Message handler callback */
typedef void (*socket_handler_t)(socket_connection_t *conn, const char *message, void *user_data);

/**
 * Create socket server
 * @param socket_path Path to Unix socket (e.g., "/var/run/pshell.sock")
 * @return Server instance or NULL on error
 */
socket_server_t* socket_server_create(const char *socket_path);

/**
 * Destroy socket server
 * @param server Server to destroy
 */
void socket_server_destroy(socket_server_t *server);

/**
 * Start socket server
 * @param server Server instance
 * @return true on success
 */
bool socket_server_start(socket_server_t *server);

/**
 * Stop socket server
 * @param server Server instance
 */
void socket_server_stop(socket_server_t *server);

/**
 * Set message handler
 * @param server Server instance
 * @param handler Handler function
 * @param user_data User data passed to handler
 * @return true on success
 */
bool socket_server_set_handler(socket_server_t *server, socket_handler_t handler, void *user_data);

/**
 * Set socket permissions
 * @param server Server instance
 * @param mode Permission mode (e.g., 0600)
 * @return true on success
 */
bool socket_server_set_permissions(socket_server_t *server, int mode);

/**
 * Get server socket FD (for multiplexer)
 * @param server Server instance
 * @return Socket FD or -1
 */
int socket_server_get_fd(const socket_server_t *server);

/**
 * Handle incoming connection
 * @param server Server instance
 * @return true if connection handled
 */
bool socket_server_handle_connection(socket_server_t *server);

/**
 * Send response to connection
 * @param conn Connection
 * @param message Message to send
 * @return true on success
 */
bool socket_connection_send(socket_connection_t *conn, const char *message);

/**
 * Get connection file descriptor
 * @param conn Connection
 * @return FD or -1
 */
int socket_connection_get_fd(const socket_connection_t *conn);

/**
 * Set expect context for connection (for cancellation on disconnect)
 * @param conn Connection
 * @param ctx Expect context pointer (or NULL to clear)
 */
void socket_connection_set_expect_ctx(socket_connection_t *conn, void *ctx);

/**
 * Detach a connection: the handler transfers ownership of the fd to an
 * asynchronous worker, which writes the response and closes the fd.
 * After this the server must not touch the fd.
 * @param conn Connection
 */
void socket_connection_detach(socket_connection_t *conn);



#endif /* PSHELL_SOCKET_SERVER_H */
