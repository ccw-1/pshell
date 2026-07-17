#include "pshell/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 65536

static int connect_to_server(const char *host, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        return -1;
    }

    struct hostent *server = gethostbyname(host);
    if (!server) {
        close(sock_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(port);

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

static http_response_t *make_request(const char *host, int port, const char *method,
                                     const char *path, const char *body, const char *api_key) {
    http_response_t *response = calloc(1, sizeof(http_response_t));
    if (!response) {
        return NULL;
    }

    /* Connect to server */
    int sock_fd = connect_to_server(host, port);
    if (sock_fd < 0) {
        response->error = strdup("Failed to connect to server");
        return response;
    }

    /* Build request */
    char request[BUFFER_SIZE];
    int len = snprintf(request, sizeof(request),
                      "%s %s HTTP/1.1\r\n"
                      "Host: %s:%d\r\n"
                      "Connection: close\r\n",
                      method, path, host, port);

    if (api_key) {
        len += snprintf(request + len, sizeof(request) - len,
                       "Authorization: Bearer %s\r\n", api_key);
    }

    if (body) {
        len += snprintf(request + len, sizeof(request) - len,
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n",
                       strlen(body));
    }

    len += snprintf(request + len, sizeof(request) - len, "\r\n");

    if (body) {
        len += snprintf(request + len, sizeof(request) - len, "%s", body);
    }

    /* Send request */
    if (write(sock_fd, request, len) < 0) {
        response->error = strdup("Failed to send request");
        close(sock_fd);
        return response;
    }

    /* Set socket timeout */
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    /* Read response - simple approach: read until EOF */
    char buffer[BUFFER_SIZE];
    ssize_t total = 0;

    while (total < (ssize_t)sizeof(buffer) - 1) {
        ssize_t n = read(sock_fd, buffer + total, sizeof(buffer) - total - 1);
        
        if (n > 0) {
            total += n;
        } else if (n == 0) {
            /* EOF - server closed connection, we're done */
            break;
        } else {
            /* Error or timeout */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout - if we have data, that's OK */
                if (total > 0) {
                    break;
                }
            }
            /* Real error */
            response->error = strdup("Read error");
            close(sock_fd);
            return response;
        }
    }

    close(sock_fd);

    if (total <= 0) {
        response->error = strdup("No data received");
        return response;
    }

    buffer[total] = '\0';

    /* Parse response */
    char *status_line = buffer;
    char *header_end = strstr(buffer, "\r\n\r\n");
    
    if (!header_end) {
        response->error = strdup("Invalid HTTP response");
        return response;
    }

    /* Parse status code */
    char *space = strchr(status_line, ' ');
    if (space) {
        response->status_code = atoi(space + 1);
    }

    /* Check for Content-Length header */
    *header_end = '\0';  /* Temporarily null-terminate headers */
    char *content_length_header = strstr(buffer, "Content-Length:");
    size_t expected_body_len = 0;
    if (content_length_header) {
        expected_body_len = atoi(content_length_header + 15);
    }
    *header_end = '\r';  /* Restore */

    /* Extract body */
    char *body_start = header_end + 4;
    size_t body_len = total - (body_start - buffer);
    
    /* If we have Content-Length, only use that many bytes */
    if (expected_body_len > 0 && body_len > expected_body_len) {
        body_len = expected_body_len;
    }
    
    if (body_len > 0) {
        response->body = malloc(body_len + 1);
        if (response->body) {
            memcpy(response->body, body_start, body_len);
            response->body[body_len] = '\0';
            response->body_size = body_len;
        }
    }

    return response;
}

http_response_t *http_client_get(const char *host, int port, const char *path, const char *api_key) {
    return make_request(host, port, "GET", path, NULL, api_key);
}

http_response_t *http_client_post(const char *host, int port, const char *path,
                                  const char *body, const char *api_key) {
    return make_request(host, port, "POST", path, body, api_key);
}

http_response_t *http_client_delete(const char *host, int port, const char *path, const char *api_key) {
    return make_request(host, port, "DELETE", path, NULL, api_key);
}

void http_response_free(http_response_t *response) {
    if (!response) {
        return;
    }

    free(response->body);
    free(response->error);
    free(response);
}

bool http_response_is_success(const http_response_t *response) {
    return response && response->status_code >= 200 && response->status_code < 300;
}
