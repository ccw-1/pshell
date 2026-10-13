#ifndef PSHELL_HTTP_SERVER_H
#define PSHELL_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Simple HTTP server for pshell
 * No external dependencies, custom implementation
 */

typedef struct http_server http_server_t;
typedef struct http_request http_request_t;
typedef struct http_response http_response_t;

/* HTTP methods */
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_UNKNOWN
} http_method_t;

/* HTTP status codes */
typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_CONFLICT = 409,
    HTTP_STATUS_INTERNAL_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501
} http_status_t;

/* Request handler callback */
typedef void (*http_handler_t)(http_request_t *req, http_response_t *resp, void *user_data);

/**
 * Create HTTP server
 * @param bind_addr Address to bind to (e.g., "127.0.0.1")
 * @param port Port to listen on
 * @return Server instance or NULL on error
 */
http_server_t* http_server_create(const char *bind_addr, int port);

/**
 * Destroy HTTP server
 * @param server Server to destroy
 */
void http_server_destroy(http_server_t *server);

/**
 * Start HTTP server
 * @param server Server instance
 * @return true on success
 */
bool http_server_start(http_server_t *server);

/**
 * Stop HTTP server
 * @param server Server instance
 */
void http_server_stop(http_server_t *server);

/**
 * Register route handler
 * @param server Server instance
 * @param method HTTP method
 * @param path URL path (supports wildcards)
 * @param handler Handler function
 * @param user_data User data passed to handler
 * @return true on success
 */
bool http_server_register_route(http_server_t *server, http_method_t method,
                                const char *path, http_handler_t handler,
                                void *user_data);

/**
 * Set static file directory
 * @param server Server instance
 * @param dir_path Directory path
 * @return true on success
 */
bool http_server_set_static_dir(http_server_t *server, const char *dir_path);

/**
 * Get server socket FD (for multiplexer)
 * @param server Server instance
 * @return Socket FD or -1
 */
int http_server_get_fd(const http_server_t *server);

/**
 * Handle incoming connection
 * @param server Server instance
 * @return true if connection handled
 */
bool http_server_handle_connection(http_server_t *server);

/* Request functions */

/**
 * Get request method
 * @param req Request
 * @return HTTP method
 */
http_method_t http_request_get_method(const http_request_t *req);

/**
 * Get request path
 * @param req Request
 * @return URL path
 */
const char* http_request_get_path(const http_request_t *req);

/**
 * Get request header
 * @param req Request
 * @param name Header name
 * @return Header value or NULL
 */
const char* http_request_get_header(const http_request_t *req, const char *name);

/**
 * Get request body
 * @param req Request
 * @param size Output body size
 * @return Body data or NULL
 */
const char* http_request_get_body(const http_request_t *req, size_t *size);

/**
 * Get query parameter
 * @param req Request
 * @param name Parameter name
 * @return Parameter value or NULL
 */
const char* http_request_get_query(const http_request_t *req, const char *name);

/* Response functions */

/**
 * Set response status
 * @param resp Response
 * @param status HTTP status code
 */
void http_response_set_status(http_response_t *resp, http_status_t status);

/**
 * Set response header
 * @param resp Response
 * @param name Header name
 * @param value Header value
 */
void http_response_set_header(http_response_t *resp, const char *name, const char *value);

/**
 * Set response body
 * @param resp Response
 * @param body Body data
 * @param size Body size
 */
void http_response_set_body(http_response_t *resp, const char *body, size_t size);

/**
 * Send JSON response
 * @param resp Response
 * @param status HTTP status code
 * @param json JSON string
 */
void http_response_send_json(http_response_t *resp, http_status_t status, const char *json);

/**
 * Send error response
 * @param resp Response
 * @param status HTTP status code
 * @param message Error message
 */
void http_response_send_error(http_response_t *resp, http_status_t status, const char *message);

/**
 * Send file response
 * @param resp Response
 * @param file_path File path
 * @return true on success
 */
bool http_response_send_file(http_response_t *resp, const char *file_path);

/**
 * Convert HTTP method to string
 * @param method HTTP method
 * @return Method string
 */
const char* http_method_to_string(http_method_t method);

/**
 * Parse HTTP method from string
 * @param str Method string
 * @return HTTP method
 */
http_method_t http_method_from_string(const char *str);

/**
 * Get HTTP status message
 * @param status HTTP status code
 * @return Status message
 */
const char* http_status_message(http_status_t status);

#endif /* PSHELL_HTTP_SERVER_H */
