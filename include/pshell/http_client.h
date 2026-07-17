#ifndef PSHELL_HTTP_CLIENT_H
#define PSHELL_HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

/**
 * HTTP Client - Simple HTTP client for REST API calls
 * 
 * Provides basic HTTP GET/POST/DELETE operations for
 * communicating with the pshell REST API.
 */

/* HTTP response */
typedef struct {
    int status_code;        /* HTTP status code */
    char *body;             /* Response body (allocated) */
    size_t body_size;       /* Body size */
    char *error;            /* Error message (allocated) */
} http_response_t;

/**
 * Make HTTP GET request
 * @param host Hostname or IP address
 * @param port Port number
 * @param path Request path
 * @param api_key Optional API key (NULL if not needed)
 * @return Response (caller must free with http_response_free)
 */
http_response_t *http_client_get(const char *host, int port, const char *path, const char *api_key);

/**
 * Make HTTP POST request
 * @param host Hostname or IP address
 * @param port Port number
 * @param path Request path
 * @param body Request body (JSON)
 * @param api_key Optional API key (NULL if not needed)
 * @return Response (caller must free with http_response_free)
 */
http_response_t *http_client_post(const char *host, int port, const char *path, 
                                  const char *body, const char *api_key);

/**
 * Make HTTP DELETE request
 * @param host Hostname or IP address
 * @param port Port number
 * @param path Request path
 * @param api_key Optional API key (NULL if not needed)
 * @return Response (caller must free with http_response_free)
 */
http_response_t *http_client_delete(const char *host, int port, const char *path, const char *api_key);

/**
 * Free HTTP response
 * @param response Response to free
 */
void http_response_free(http_response_t *response);

/**
 * Check if response is successful (2xx status code)
 * @param response HTTP response
 * @return true if successful
 */
bool http_response_is_success(const http_response_t *response);

#endif /* PSHELL_HTTP_CLIENT_H */
