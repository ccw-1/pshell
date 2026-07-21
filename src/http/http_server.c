#include "pshell/http_server.h"
#include "pshell/logger.h"
#include "pshell/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_ROUTES 64
#define MAX_HEADERS 32
#define MAX_QUERY_PARAMS 32
#define BUFFER_SIZE 8192

/* Route entry */
typedef struct {
    http_method_t method;
    char *path;
    http_handler_t handler;
    void *user_data;
    bool active;
} route_entry_t;

/* HTTP request structure */
struct http_request {
    http_method_t method;
    char *path;
    char *query_string;
    char *headers[MAX_HEADERS * 2];  /* name, value pairs */
    int header_count;
    char *query_params[MAX_QUERY_PARAMS * 2];  /* name, value pairs */
    int query_count;
    char *body;
    size_t body_size;
};

/* HTTP response structure */
struct http_response {
    http_status_t status;
    char *headers[MAX_HEADERS * 2];  /* name, value pairs */
    int header_count;
    char *body;
    size_t body_size;
    int client_fd;
};

/* HTTP server structure */
struct http_server {
    int listen_fd;
    char *bind_addr;
    int port;
    route_entry_t routes[MAX_ROUTES];
    int route_count;
    char *static_dir;
    bool running;
};

/* Helper: Parse query string */
static void parse_query_string(http_request_t *req, const char *query) {
    if (!query || !*query) return;
    
    char *q = strdup(query);
    if (!q) return;
    
    char *saveptr;
    char *pair = strtok_r(q, "&", &saveptr);
    
    while (pair && req->query_count < MAX_QUERY_PARAMS) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            req->query_params[req->query_count * 2] = strdup(pair);
            req->query_params[req->query_count * 2 + 1] = strdup(eq + 1);
            req->query_count++;
        }
        pair = strtok_r(NULL, "&", &saveptr);
    }
    
    free(q);
}

/* Helper: Parse HTTP request */
static http_request_t* parse_request(const char *data, size_t len) {
    http_request_t *req = calloc(1, sizeof(http_request_t));
    if (!req) return NULL;
    
    char *buf = strndup(data, len);
    if (!buf) {
        free(req);
        return NULL;
    }
    
    /* Parse request line */
    char *line = strtok(buf, "\r\n");
    if (!line) {
        free(buf);
        free(req);
        return NULL;
    }
    
    char method[16], path[1024], version[16];
    if (sscanf(line, "%15s %1023s %15s", method, path, version) != 3) {
        free(buf);
        free(req);
        return NULL;
    }
    
    req->method = http_method_from_string(method);
    
    /* Split path and query string */
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
        req->query_string = strdup(query + 1);
        parse_query_string(req, query + 1);
    }
    
    req->path = strdup(path);
    
    /* Parse headers */
    while ((line = strtok(NULL, "\r\n")) && *line) {
        char *colon = strchr(line, ':');
        if (colon && req->header_count < MAX_HEADERS) {
            *colon = '\0';
            req->headers[req->header_count * 2] = strdup(line);
            req->headers[req->header_count * 2 + 1] = strdup(str_trim(colon + 1));
            req->header_count++;
        }
    }
    
    /* Body is after headers */
    const char *body_start = strstr(data, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t body_len = len - (body_start - data);
        if (body_len > 0) {
            req->body = malloc(body_len + 1);
            if (req->body) {
                memcpy(req->body, body_start, body_len);
                req->body[body_len] = '\0';
                req->body_size = body_len;
            }
        }
    }
    
    free(buf);
    return req;
}

/* Helper: Free request */
static void free_request(http_request_t *req) {
    if (!req) return;
    
    free(req->path);
    free(req->query_string);
    free(req->body);
    
    for (int i = 0; i < req->header_count; i++) {
        free(req->headers[i * 2]);
        free(req->headers[i * 2 + 1]);
    }
    
    for (int i = 0; i < req->query_count; i++) {
        free(req->query_params[i * 2]);
        free(req->query_params[i * 2 + 1]);
    }
    
    free(req);
}

/* Helper: Send response */
static void send_response(http_response_t *resp) {
    if (resp->client_fd < 0) return;
    
    char buffer[BUFFER_SIZE];
    int len = 0;
    
    /* Status line */
    len += snprintf(buffer + len, sizeof(buffer) - len,
                   "HTTP/1.1 %d %s\r\n",
                   resp->status, http_status_message(resp->status));
    
    /* Headers */
    for (int i = 0; i < resp->header_count; i++) {
        len += snprintf(buffer + len, sizeof(buffer) - len,
                       "%s: %s\r\n",
                       resp->headers[i * 2],
                       resp->headers[i * 2 + 1]);
    }
    
    /* Connection: close (we don't support keep-alive) */
    len += snprintf(buffer + len, sizeof(buffer) - len,
                   "Connection: close\r\n");
    
    /* Content-Length if body present */
    if (resp->body && resp->body_size > 0) {
        len += snprintf(buffer + len, sizeof(buffer) - len,
                       "Content-Length: %zu\r\n", resp->body_size);
    }
    
    /* End of headers */
    len += snprintf(buffer + len, sizeof(buffer) - len, "\r\n");
    
    /* Send headers */
    ssize_t written = write(resp->client_fd, buffer, len);
    (void)written;  /* Suppress warning */
    
    /* Send body */
    if (resp->body && resp->body_size > 0) {
        written = write(resp->client_fd, resp->body, resp->body_size);
        (void)written;  /* Suppress warning */
    }
    
    /* Flush socket - ensure all data is sent */
    fsync(resp->client_fd);
}

/* Helper: Free response */
static void free_response(http_response_t *resp) {
    if (!resp) return;
    
    for (int i = 0; i < resp->header_count; i++) {
        free(resp->headers[i * 2]);
        free(resp->headers[i * 2 + 1]);
    }
    
    free(resp->body);
    free(resp);
}

http_server_t* http_server_create(const char *bind_addr, int port) {
    http_server_t *server = calloc(1, sizeof(http_server_t));
    if (!server) {
        log_error("Failed to allocate HTTP server");
        return NULL;
    }
    
    server->bind_addr = strdup(bind_addr);
    server->port = port;
    server->listen_fd = -1;
    server->running = false;
    
    log_info("Created HTTP server: %s:%d", bind_addr, port);
    return server;
}

void http_server_destroy(http_server_t *server) {
    if (!server) return;
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }
    
    free(server->bind_addr);
    free(server->static_dir);
    
    for (int i = 0; i < server->route_count; i++) {
        if (server->routes[i].active) {
            free(server->routes[i].path);
        }
    }
    
    free(server);
    log_info("Destroyed HTTP server");
}

bool http_server_start(http_server_t *server) {
    if (!server) return false;
    
    fprintf(stderr, "DEBUG: http_server_start: creating socket\n");
    fflush(stderr);
    
    /* Create socket */
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        fprintf(stderr, "DEBUG: socket() failed: %s\n", strerror(errno));
        fflush(stderr);
        return false;
    }
    
    fprintf(stderr, "DEBUG: socket created: fd=%d\n", server->listen_fd);
    fflush(stderr);
    
    /* Set socket options */
    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    fprintf(stderr, "DEBUG: socket options set, about to bind\n");
    fflush(stderr);
    
    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server->port);
    inet_pton(AF_INET, server->bind_addr, &addr.sin_addr);
    
    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "DEBUG: bind() failed: %s\n", strerror(errno));
        fflush(stderr);
        close(server->listen_fd);
        server->listen_fd = -1;
        return false;
    }
    
    fprintf(stderr, "DEBUG: bind successful, about to listen\n");
    fflush(stderr);
    
    /* Listen */
    if (listen(server->listen_fd, 10) < 0) {
        fprintf(stderr, "DEBUG: listen() failed: %s\n", strerror(errno));
        fflush(stderr);
        close(server->listen_fd);
        server->listen_fd = -1;
        return false;
    }
    
    fprintf(stderr, "DEBUG: listen successful, setting non-blocking\n");
    fflush(stderr);
    
    /* Set non-blocking */
    int flags = fcntl(server->listen_fd, F_GETFL, 0);
    fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    fprintf(stderr, "DEBUG: non-blocking set, about to log_info\n");
    fflush(stderr);
    
    server->running = true;
    log_info("HTTP server listening on %s:%d", server->bind_addr, server->port);
    
    fprintf(stderr, "DEBUG: http_server_start completed successfully\n");
    fflush(stderr);
    
    return true;
}

void http_server_stop(http_server_t *server) {
    if (!server) return;
    
    server->running = false;
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    
    log_info("HTTP server stopped");
}

bool http_server_register_route(http_server_t *server, http_method_t method,
                                const char *path, http_handler_t handler,
                                void *user_data) {
    if (!server || !path || !handler) return false;
    
    if (server->route_count >= MAX_ROUTES) {
        log_error("Route table full");
        return false;
    }
    
    server->routes[server->route_count].method = method;
    server->routes[server->route_count].path = strdup(path);
    server->routes[server->route_count].handler = handler;
    server->routes[server->route_count].user_data = user_data;
    server->routes[server->route_count].active = true;
    server->route_count++;
    
    fprintf(stderr, "DEBUG: Registered route: %s %s\n",
              http_method_to_string(method), path);
    fflush(stderr);
    
    return true;
}

bool http_server_set_static_dir(http_server_t *server, const char *dir_path) {
    if (!server || !dir_path) return false;
    
    free(server->static_dir);
    server->static_dir = strdup(dir_path);
    
    log_info("Set static directory: %s", dir_path);
    return true;
}

int http_server_get_fd(const http_server_t *server) {
    return server ? server->listen_fd : -1;
}

bool http_server_handle_connection(http_server_t *server) {
    if (!server || server->listen_fd < 0) return false;
    
    /* Accept connection */
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Accept failed: %s", strerror(errno));
        }
        return false;
    }
    
    /* Read request */
    char buffer[BUFFER_SIZE];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (n <= 0) {
        close(client_fd);
        return false;
    }
    
    buffer[n] = '\0';
    
    /* Parse request */
    http_request_t *req = parse_request(buffer, n);
    if (!req) {
        close(client_fd);
        return false;
    }
    
    /* Create response */
    http_response_t *resp = calloc(1, sizeof(http_response_t));
    if (!resp) {
        free_request(req);
        close(client_fd);
        return false;
    }
    
    resp->client_fd = client_fd;
    resp->status = HTTP_STATUS_NOT_FOUND;
    
    /* Find matching route */
    bool handled = false;
    for (int i = 0; i < server->route_count; i++) {
        if (!server->routes[i].active) continue;
        
        if (server->routes[i].method != req->method) continue;
        
        /* Check for exact match first */
        if (strcmp(server->routes[i].path, req->path) == 0) {
            server->routes[i].handler(req, resp, server->routes[i].user_data);
            handled = true;
            break;
        }
        
        /* Check for pattern match (e.g., /api/sessions/:id) */
        const char *pattern = server->routes[i].path;
        const char *path = req->path;
        bool match = true;
        
        while (*pattern && *path) {
            if (*pattern == ':') {
                /* Skip parameter in pattern */
                while (*pattern && *pattern != '/') pattern++;
                /* Skip parameter value in path */
                while (*path && *path != '/') path++;
            } else if (*pattern == *path) {
                pattern++;
                path++;
            } else {
                match = false;
                break;
            }
        }
        
        /* Both must end at the same time */
        if (match && *pattern == *path) {
            server->routes[i].handler(req, resp, server->routes[i].user_data);
            handled = true;
            break;
        }
    }
    
    /* Try static file serving if no route matched */
    if (!handled && server->static_dir && req->method == HTTP_METHOD_GET) {
        char file_path[512];
        const char *path = req->path;
        
        /* Default to index.html for root */
        if (strcmp(path, "/") == 0) {
            path = "/index.html";
        }
        
        snprintf(file_path, sizeof(file_path), "%s%s", server->static_dir, path);
        
        /* Check if file exists and is readable */
        if (access(file_path, R_OK) == 0) {
            /* Determine content type from extension */
            const char *ext = strrchr(file_path, '.');
            const char *content_type = "application/octet-stream";
            
            if (ext) {
                if (strcmp(ext, ".html") == 0) content_type = "text/html";
                else if (strcmp(ext, ".css") == 0) content_type = "text/css";
                else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
                else if (strcmp(ext, ".json") == 0) content_type = "application/json";
                else if (strcmp(ext, ".png") == 0) content_type = "image/png";
                else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
                else if (strcmp(ext, ".gif") == 0) content_type = "image/gif";
                else if (strcmp(ext, ".svg") == 0) content_type = "image/svg+xml";
            }
            
            http_response_set_header(resp, "Content-Type", content_type);
            /* Prevent browser caching for JS/CSS files to ensure updates are loaded */
            if (ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".css") == 0)) {
                http_response_set_header(resp, "Cache-Control", "no-cache, no-store, must-revalidate");
                http_response_set_header(resp, "Pragma", "no-cache");
                http_response_set_header(resp, "Expires", "0");
            }
            if (http_response_send_file(resp, file_path)) {
                handled = true;
            }
        }
    }
    
    if (!handled) {
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "Not Found");
    }
    
    /* Send response */
    send_response(resp);
    
    /* Shutdown write side to signal EOF to client */
    shutdown(client_fd, SHUT_WR);
    
    /* Cleanup */
    close(client_fd);
    free_request(req);
    free_response(resp);
    
    return true;
}

/* Request functions */

http_method_t http_request_get_method(const http_request_t *req) {
    return req ? req->method : HTTP_METHOD_UNKNOWN;
}

const char* http_request_get_path(const http_request_t *req) {
    return req ? req->path : NULL;
}

const char* http_request_get_header(const http_request_t *req, const char *name) {
    if (!req || !name) return NULL;
    
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i * 2], name) == 0) {
            return req->headers[i * 2 + 1];
        }
    }
    
    return NULL;
}

const char* http_request_get_body(const http_request_t *req, size_t *size) {
    if (!req) return NULL;
    if (size) *size = req->body_size;
    return req->body;
}

const char* http_request_get_query(const http_request_t *req, const char *name) {
    if (!req || !name) return NULL;
    
    for (int i = 0; i < req->query_count; i++) {
        if (strcmp(req->query_params[i * 2], name) == 0) {
            return req->query_params[i * 2 + 1];
        }
    }
    
    return NULL;
}

/* Response functions */

void http_response_set_status(http_response_t *resp, http_status_t status) {
    if (resp) resp->status = status;
}

void http_response_set_header(http_response_t *resp, const char *name, const char *value) {
    if (!resp || !name || !value || resp->header_count >= MAX_HEADERS) return;
    
    resp->headers[resp->header_count * 2] = strdup(name);
    resp->headers[resp->header_count * 2 + 1] = strdup(value);
    resp->header_count++;
}

void http_response_set_body(http_response_t *resp, const char *body, size_t size) {
    if (!resp || !body) return;
    
    free(resp->body);
    resp->body = malloc(size);
    if (resp->body) {
        memcpy(resp->body, body, size);
        resp->body_size = size;
    }
}

void http_response_send_json(http_response_t *resp, http_status_t status, const char *json) {
    if (!resp || !json) return;
    
    http_response_set_status(resp, status);
    http_response_set_header(resp, "Content-Type", "application/json");
    http_response_set_body(resp, json, strlen(json));
}

void http_response_send_error(http_response_t *resp, http_status_t status, const char *message) {
    if (!resp) return;
    
    char json[512];
    snprintf(json, sizeof(json),
             "{\"error\":\"%s\",\"status\":%d}",
             message ? message : "Error", status);
    
    http_response_send_json(resp, status, json);
}

bool http_response_send_file(http_response_t *resp, const char *file_path) {
    if (!resp || !file_path) return false;
    
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        http_response_send_error(resp, HTTP_STATUS_NOT_FOUND, "File not found");
        return false;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *data = malloc(size);
    if (!data) {
        fclose(fp);
        http_response_send_error(resp, HTTP_STATUS_INTERNAL_ERROR, "Memory error");
        return false;
    }
    
    fread(data, 1, size, fp);
    fclose(fp);
    
    http_response_set_status(resp, HTTP_STATUS_OK);
    http_response_set_body(resp, data, size);
    
    free(data);
    return true;
}

/* Utility functions */

const char* http_method_to_string(http_method_t method) {
    switch (method) {
        case HTTP_METHOD_GET: return "GET";
        case HTTP_METHOD_POST: return "POST";
        case HTTP_METHOD_PUT: return "PUT";
        case HTTP_METHOD_DELETE: return "DELETE";
        case HTTP_METHOD_HEAD: return "HEAD";
        case HTTP_METHOD_OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

http_method_t http_method_from_string(const char *str) {
    if (!str) return HTTP_METHOD_UNKNOWN;
    
    if (strcmp(str, "GET") == 0) return HTTP_METHOD_GET;
    if (strcmp(str, "POST") == 0) return HTTP_METHOD_POST;
    if (strcmp(str, "PUT") == 0) return HTTP_METHOD_PUT;
    if (strcmp(str, "DELETE") == 0) return HTTP_METHOD_DELETE;
    if (strcmp(str, "HEAD") == 0) return HTTP_METHOD_HEAD;
    if (strcmp(str, "OPTIONS") == 0) return HTTP_METHOD_OPTIONS;
    
    return HTTP_METHOD_UNKNOWN;
}

const char* http_status_message(http_status_t status) {
    switch (status) {
        case HTTP_STATUS_OK: return "OK";
        case HTTP_STATUS_CREATED: return "Created";
        case HTTP_STATUS_NO_CONTENT: return "No Content";
        case HTTP_STATUS_BAD_REQUEST: return "Bad Request";
        case HTTP_STATUS_UNAUTHORIZED: return "Unauthorized";
        case HTTP_STATUS_FORBIDDEN: return "Forbidden";
        case HTTP_STATUS_NOT_FOUND: return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_STATUS_INTERNAL_ERROR: return "Internal Server Error";
        case HTTP_STATUS_NOT_IMPLEMENTED: return "Not Implemented";
        default: return "Unknown";
    }
}
