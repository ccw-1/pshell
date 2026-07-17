#ifndef PSHELL_EXPECT_ENGINE_H
#define PSHELL_EXPECT_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "pshell/pattern_matcher.h"

/**
 * Expect Engine - Pattern-based output matching with timeout
 * 
 * Monitors session output and matches against patterns,
 * with configurable timeout handling.
 */

/* Forward declarations */
typedef struct session session_t;

/* Expect result codes */
typedef enum {
    EXPECT_MATCHED = 0,     /* Pattern matched */
    EXPECT_TIMEOUT = 1,     /* Timeout expired */
    EXPECT_EOF = 2,         /* End of file/session closed */
    EXPECT_ERROR = 3        /* Error occurred */
} expect_result_t;

/* Expect match information */
typedef struct {
    expect_result_t result;     /* Result code */
    int pattern_index;          /* Index of matched pattern (-1 if none) */
    char *matched_text;         /* Matched text (allocated) */
    size_t match_start;         /* Start position in buffer */
    size_t match_end;           /* End position in buffer */
    char *before;               /* Text before match (allocated) */
    char *after;                /* Text after match (allocated) */
    int nmatch;                 /* Number of submatches */
    char **submatches;          /* Submatch strings (allocated array) */
} expect_match_t;

/* Expect engine */
typedef struct {
    session_t *session;         /* Associated session */
    pattern_matcher_t **patterns; /* Array of patterns */
    int npatterns;              /* Number of patterns */
    int timeout_ms;             /* Timeout in milliseconds */
    char *buffer;               /* Accumulated output buffer */
    size_t buffer_size;         /* Current buffer size */
    size_t buffer_capacity;     /* Buffer capacity */
    bool case_sensitive;        /* Case-sensitive matching */
} expect_engine_t;

/**
 * Create expect engine
 * @param session Session to monitor
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param case_sensitive Whether matching is case-sensitive
 * @return Engine or NULL on error
 */
expect_engine_t *expect_engine_create(session_t *session, int timeout_ms, bool case_sensitive);

/**
 * Destroy expect engine
 * @param engine Engine to destroy
 */
void expect_engine_destroy(expect_engine_t *engine);

/**
 * Add pattern to expect engine
 * @param engine Expect engine
 * @param pattern Regex pattern string
 * @return Pattern index or -1 on error
 */
int expect_engine_add_pattern(expect_engine_t *engine, const char *pattern);

/**
 * Clear all patterns
 * @param engine Expect engine
 */
void expect_engine_clear_patterns(expect_engine_t *engine);

/**
 * Wait for pattern match
 * @param engine Expect engine
 * @param match Output match information (caller must free with expect_match_free)
 * @return Expect result code
 */
expect_result_t expect_engine_wait(expect_engine_t *engine, expect_match_t *match);

/**
 * Wait for any of multiple patterns
 * @param engine Expect engine
 * @param patterns Array of pattern strings
 * @param npatterns Number of patterns
 * @param match Output match information (caller must free with expect_match_free)
 * @return Expect result code
 */
expect_result_t expect_engine_wait_patterns(expect_engine_t *engine, 
                                           const char **patterns, int npatterns,
                                           expect_match_t *match);

/**
 * Set timeout
 * @param engine Expect engine
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 */
void expect_engine_set_timeout(expect_engine_t *engine, int timeout_ms);

/**
 * Get accumulated buffer
 * @param engine Expect engine
 * @return Buffer contents (do not free)
 */
const char *expect_engine_get_buffer(expect_engine_t *engine);

/**
 * Clear accumulated buffer
 * @param engine Expect engine
 */
void expect_engine_clear_buffer(expect_engine_t *engine);

/**
 * Free expect match resources
 * @param match Match to free
 */
void expect_match_free(expect_match_t *match);

#endif /* PSHELL_EXPECT_ENGINE_H */
