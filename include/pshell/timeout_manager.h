#ifndef PSHELL_TIMEOUT_MANAGER_H
#define PSHELL_TIMEOUT_MANAGER_H

#include <stdbool.h>
#include <sys/time.h>

/**
 * Timeout Manager - Timeout tracking and calculation
 * 
 * Provides utilities for managing timeouts in blocking operations.
 */

/* Timeout context */
typedef struct {
    int timeout_ms;             /* Total timeout in milliseconds */
    struct timeval start_time;  /* Start time */
    bool has_timeout;           /* Whether timeout is set */
} timeout_ctx_t;

/**
 * Initialize timeout context
 * @param ctx Timeout context
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 */
void timeout_init(timeout_ctx_t *ctx, int timeout_ms);

/**
 * Check if timeout has expired
 * @param ctx Timeout context
 * @return true if timeout expired
 */
bool timeout_expired(const timeout_ctx_t *ctx);

/**
 * Get remaining timeout
 * @param ctx Timeout context
 * @return Remaining milliseconds (0 if expired, -1 if no timeout)
 */
int timeout_remaining(const timeout_ctx_t *ctx);

/**
 * Get remaining timeout as timeval
 * @param ctx Timeout context
 * @param tv Output timeval
 * @return true if timeout is set, false if no timeout
 */
bool timeout_to_timeval(const timeout_ctx_t *ctx, struct timeval *tv);

/**
 * Get elapsed time
 * @param ctx Timeout context
 * @return Elapsed milliseconds
 */
long timeout_elapsed(const timeout_ctx_t *ctx);

/**
 * Reset timeout (restart timer)
 * @param ctx Timeout context
 */
void timeout_reset(timeout_ctx_t *ctx);

#endif /* PSHELL_TIMEOUT_MANAGER_H */
