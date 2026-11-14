#ifndef PSHELL_EXPECT_ENGINE_H
#define PSHELL_EXPECT_ENGINE_H

#include "pshell/pattern_matcher.h"
#include "pshell/session.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * Expect Engine - Pattern-based automation for terminal sessions
 * 
 * Waits for specific patterns in terminal output and can send
 * responses automatically (like expect/tcl).
 */

/* Expect flags */
typedef enum {
    EXPECT_FLAG_NONE = 0,
    EXPECT_FLAG_SKIP_SEEDING = 1 << 0,  /* Don't seed from screen (incoming-only mode) */
} expect_flags_t;

/* Expect result */
typedef enum {
    EXPECT_MATCHED,         /* Pattern matched */
    EXPECT_TIMEOUT,         /* Timeout reached */
    EXPECT_EOF,             /* End of file (session closed) */
    EXPECT_ERROR            /* Error occurred */
} expect_result_t;

/* Expect context */
typedef struct {
    session_t *session;             /* Session to monitor */
    char *buffer;                   /* Accumulated output buffer */
    size_t buffer_size;             /* Buffer size */
    size_t buffer_used;             /* Bytes used in buffer */
    time_t start_time;              /* Start time for timeout */
    int timeout_ms;                 /* Timeout in milliseconds */
    expect_flags_t flags;           /* Expect flags (seeding, etc.) */
    uint64_t read_pos;              /* Position in the session's expect window of the next unread byte */
    bool attached;                  /* Attached to the session's expect window */
    volatile bool cancelled;        /* Cancellation flag (set when client disconnects) */
    bool (*cancel_check)(void *user_data); /* Optional extra cancellation probe */
    void *cancel_user_data;         /* User data for cancel_check */
} expect_context_t;

/**
 * Create expect context
 * @param session Session to monitor
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param flags Expect flags (EXPECT_FLAG_NONE for default behavior)
 * @return Context or NULL on error
 */
expect_context_t *expect_context_create(session_t *session, int timeout_ms, expect_flags_t flags);

/**
 * Destroy expect context
 * @param ctx Context to destroy
 */
void expect_context_destroy(expect_context_t *ctx);

/**
 * Wait for pattern in session output
 * @param ctx Expect context
 * @param pattern Regex pattern to match
 * @param case_sensitive Whether matching is case-sensitive
 * @param result Output match result (optional, caller must free)
 * @return Expect result
 */
expect_result_t expect_pattern(expect_context_t *ctx, const char *pattern,
                               bool case_sensitive, match_result_t *result);

/**
 * Wait for any of multiple patterns
 * @param ctx Expect context
 * @param patterns Array of patterns
 * @param num_patterns Number of patterns
 * @param case_sensitive Whether matching is case-sensitive
 * @param matched_index Output index of matched pattern
 * @param result Output match result (optional, caller must free)
 * @return Expect result
 */
expect_result_t expect_any(expect_context_t *ctx, const char **patterns,
                           int num_patterns, bool case_sensitive,
                           int *matched_index, match_result_t *result);

/**
 * Wait for exact string in output
 * @param ctx Expect context
 * @param str String to wait for
 * @param case_sensitive Whether matching is case-sensitive
 * @return Expect result
 */
expect_result_t expect_string(expect_context_t *ctx, const char *str,
                              bool case_sensitive);

/**
 * Get accumulated output buffer
 * @param ctx Expect context
 * @return Buffer contents (do not free)
 */
const char *expect_get_buffer(const expect_context_t *ctx);

/**
 * Clear accumulated output buffer
 * @param ctx Expect context
 */
void expect_clear_buffer(expect_context_t *ctx);

/**
 * Check if timeout has been reached
 * @param ctx Expect context
 * @return true if timeout reached
 */
bool expect_is_timeout(const expect_context_t *ctx);

/**
 * Get remaining timeout in milliseconds
 * @param ctx Expect context
 * @return Remaining timeout or -1 if no timeout
 */
int expect_get_remaining_timeout(const expect_context_t *ctx);

/**
 * Send text and wait for pattern (convenience function)
 * @param session Session
 * @param send_text Text to send (can be NULL)
 * @param expect_pattern Pattern to wait for
 * @param timeout_ms Timeout in milliseconds
 * @param result Output match result (optional, caller must free)
 * @return Expect result
 */
expect_result_t expect_send_and_wait(session_t *session, const char *send_text,
                                     const char *expect_pattern, int timeout_ms,
                                     match_result_t *result);

/**
 * Get result name (for debugging)
 * @param result Expect result
 * @return Result name string
 */
const char *expect_result_name(expect_result_t result);


/**
 * Cancel an ongoing expect operation
 * Thread-safe: Can be called from any thread to interrupt expect_pattern()
 * @param ctx Expect context to cancel
 */
void expect_cancel(expect_context_t *ctx);

/**
 * Install an extra cancellation probe, polled on every loop iteration of
 * expect_pattern(). Used to detect client disconnects while an expect runs on
 * a worker thread.
 * @param ctx Expect context
 * @param check Callback returning true when the wait should be aborted
 * @param user_data Opaque pointer passed to check
 */
void expect_set_cancel_check(expect_context_t *ctx, bool (*check)(void *user_data),
                             void *user_data);


#endif /* PSHELL_EXPECT_ENGINE_H */