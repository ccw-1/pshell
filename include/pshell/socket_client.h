/*
 * socket_client.h - Unix domain socket client library
 * 
 * Provides client-side API for communicating with pshell daemon
 * via Unix domain socket.
 */

#ifndef PSHELL_SOCKET_CLIENT_H
#define PSHELL_SOCKET_CLIENT_H

#include <stddef.h>

/*
 * Socket client structure
 */
typedef struct {
    int fd;                     /* Socket file descriptor */
    char socket_path[256];      /* Socket path */
} socket_client_t;

/*
 * Connect to Unix domain socket
 * 
 * Returns: 0 on success, -1 on error
 */
int socket_client_connect(socket_client_t *client, const char *socket_path);

/*
 * Disconnect from socket
 */
void socket_client_disconnect(socket_client_t *client);

/*
 * Send request and receive response
 * 
 * Request and response are JSON strings.
 * 
 * Returns: Number of bytes received on success, -1 on error
 */
int socket_client_send_request(socket_client_t *client,
                               const char *request,
                               char *response,
                               size_t response_size);

#endif /* PSHELL_SOCKET_CLIENT_H */
