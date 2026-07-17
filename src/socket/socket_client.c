/*
 * socket_client.c - Unix domain socket client implementation
 */

#include "pshell/socket_client.h"
#include "pshell/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>

int socket_client_connect(socket_client_t *client, const char *socket_path) {
    if (!client || !socket_path) {
        return -1;
    }
    
    /* Create socket */
    client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->fd < 0) {
        log_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    /* Setup address */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    /* Connect */
    if (connect(client->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("Failed to connect to %s: %s", socket_path, strerror(errno));
        close(client->fd);
        client->fd = -1;
        return -1;
    }
    
    /* Save socket path */
    strncpy(client->socket_path, socket_path, sizeof(client->socket_path) - 1);
    
    log_debug("Connected to socket: %s", socket_path);
    return 0;
}

void socket_client_disconnect(socket_client_t *client) {
    if (!client || client->fd < 0) {
        return;
    }
    
    close(client->fd);
    client->fd = -1;
    log_debug("Disconnected from socket: %s", client->socket_path);
}

int socket_client_send_request(socket_client_t *client,
                               const char *request,
                               char *response,
                               size_t response_size) {
    if (!client || client->fd < 0 || !request || !response) {
        return -1;
    }
    
    /* Send request length (4 bytes, network byte order) */
    size_t request_len = strlen(request);
    uint32_t len_net = htonl((uint32_t)request_len);
    
    if (write(client->fd, &len_net, sizeof(len_net)) != sizeof(len_net)) {
        log_error("Failed to send request length: %s", strerror(errno));
        return -1;
    }
    
    /* Send request */
    if (write(client->fd, request, request_len) != (ssize_t)request_len) {
        log_error("Failed to send request: %s", strerror(errno));
        return -1;
    }
    
    log_debug("Sent request: %s", request);
    
    /* Read response length */
    uint32_t response_len_net;
    if (read(client->fd, &response_len_net, sizeof(response_len_net)) != sizeof(response_len_net)) {
        log_error("Failed to read response length: %s", strerror(errno));
        return -1;
    }
    
    size_t response_len = ntohl(response_len_net);
    
    /* Check buffer size */
    if (response_len >= response_size) {
        log_error("Response too large: %zu bytes (buffer: %zu)", response_len, response_size);
        return -1;
    }
    
    /* Read response */
    size_t total_read = 0;
    while (total_read < response_len) {
        ssize_t n = read(client->fd, response + total_read, response_len - total_read);
        if (n <= 0) {
            log_error("Failed to read response: %s", strerror(errno));
            return -1;
        }
        total_read += n;
    }
    
    /* Null-terminate */
    response[total_read] = '\0';
    
    log_debug("Received response: %s", response);
    
    return (int)total_read;
}