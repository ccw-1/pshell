#include "pshell/expect_engine.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#define EXPECT_BUFFER_SIZE 65536
#define EXPECT_READ_SIZE 4096

/* Get current time in milliseconds */
static long long get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

expect_context_t *expect_context_create(session_t *session, int timeout_ms, expect_flags_t flags) {
    if (!session) {
        log_error("Invalid session");
        return NULL;
    }
    
    expect_context_t *ctx = calloc(1, sizeof(expect_context_t));
    if (!ctx) {
        log_error("Failed to allocate expect context");
        return NULL;
    }
    
    ctx->session = session;
    ctx->timeout_ms = timeout_ms;
    ctx->start_time = time(NULL);
    ctx->flags = flags;
    ctx->snapshot_line = session_get_current_line_no(session);
    ctx->snapshot_offset = 0;
    
    /* Allocate buffer */
    ctx->buffer_size = EXPECT_BUFFER_SIZE;
    ctx->buffer = malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        log_error("Failed to allocate expect buffer");
        free(ctx);
        return NULL;
    }
    
    ctx->buffer[0] = '\0';
    ctx->buffer_used = 0;
    ctx->cancelled = false;
    
    /* Activate expect buffer in session */
    bool skip_seeding = (flags & EXPECT_FLAG_SKIP_SEEDING) != 0;
    if (session_start_expect(session, skip_seeding) < 0) {
        log_error("Failed to start expect buffer");
        free(ctx->buffer);
        free(ctx);
        return NULL;
    }
    
    /* Conditionally seed buffer based on flags */
    if (!(flags & EXPECT_FLAG_SKIP_SEEDING)) {
        /* Read seeded data from session's expect buffer into context buffer */
        ssize_t seeded = session_read_expect_output(session, ctx->buffer, ctx->buffer_size - 1,
                                                    1, 0, NULL, NULL);
        if (seeded > 0) {
            ctx->buffer[seeded] = '\0';
            ctx->buffer_used = seeded;
            log_debug("Loaded %zd bytes of seeded data into expect context", seeded);
        }
    } else {
        session_line_no_t current_line = 1;
        size_t current_offset = 0;
        (void)session_read_expect_output(session, ctx->buffer, 0,
                                         ctx->snapshot_line, ctx->snapshot_offset,
                                         &current_line, &current_offset);
        ctx->snapshot_line = current_line;
        ctx->snapshot_offset = current_offset;
        log_debug("Incoming-only mode: snapshot line=%lu offset=%zu",
                  (unsigned long)ctx->snapshot_line, ctx->snapshot_offset);
    }
    
    log_debug("Created expect context: timeout=%dms, flags=0x%x", timeout_ms, flags);
    
    return ctx;
}

void expect_context_destroy(expect_context_t *ctx) {
    if (!ctx) return;
    
    /* Deactivate expect buffer in session */
    if (ctx->session) {
        session_stop_expect(ctx->session);
    }
    
    free(ctx->buffer);
    free(ctx);
}

void expect_cancel(expect_context_t *ctx) {
    if (!ctx) return;
    ctx->cancelled = true;
    log_debug("Expect context cancelled");
}



bool expect_is_timeout(const expect_context_t *ctx) {
    if (!ctx || ctx->timeout_ms <= 0) {
        return false;
    }
    
    long long elapsed = get_time_ms() - (ctx->start_time * 1000);
    return elapsed >= ctx->timeout_ms;
}

int expect_get_remaining_timeout(const expect_context_t *ctx) {
    if (!ctx || ctx->timeout_ms <= 0) {
        return -1;
    }
    
    long long elapsed = get_time_ms() - (ctx->start_time * 1000);
    long long remaining = ctx->timeout_ms - elapsed;
    
    return remaining > 0 ? (int)remaining : 0;
}

const char *expect_get_buffer(const expect_context_t *ctx) {
    return ctx ? ctx->buffer : NULL;
}

void expect_clear_buffer(expect_context_t *ctx) {
    if (!ctx) return;
    
    ctx->buffer[0] = '\0';
    ctx->buffer_used = 0;
}

expect_result_t expect_pattern(expect_context_t *ctx, const char *pattern,
                               bool case_sensitive, match_result_t *result) {
    if (!ctx || !pattern) {
        return EXPECT_ERROR;
    }
    
    /* Create pattern matcher */
    pattern_matcher_t *matcher = pattern_matcher_create(pattern, case_sensitive);
    if (!matcher) {
        log_error("Failed to create pattern matcher");
        return EXPECT_ERROR;
    }
    
    /* Read and match loop with exponential backoff */
    int sleep_us = 10000;  /* Start with 10ms */
    const int max_sleep_us = 100000;  /* Cap at 100ms */
    
    while (1) {
        /* Check cancellation */
        if (ctx->cancelled) {
            pattern_matcher_destroy(matcher);
            log_debug("Expect operation cancelled");
            return EXPECT_ERROR;
        }
        
        /* Check timeout */
        if (expect_is_timeout(ctx)) {
            pattern_matcher_destroy(matcher);
            log_debug("Expect timeout reached");
            return EXPECT_TIMEOUT;
        }
        
        /* Try to match current buffer */
        match_result_t match;
        log_debug("Trying to match pattern '%s' in buffer (size=%zu): '%.100s%s'", 
                  pattern, ctx->buffer_used, ctx->buffer, 
                  ctx->buffer_used > 100 ? "..." : "");
        
        if (pattern_matcher_match(matcher, ctx->buffer, &match)) {
            pattern_matcher_destroy(matcher);
            
            if (result) {
                *result = match;
            } else {
                match_result_free(&match);
            }
            
            log_debug("Pattern matched: '%s'", pattern);
            return EXPECT_MATCHED;
        }
        
        /* Read accumulated expect output */
        char read_buf[EXPECT_READ_SIZE];
        session_line_no_t next_line = ctx->snapshot_line;
        size_t next_offset = ctx->snapshot_offset;
        ssize_t n = session_read_expect_output(ctx->session, read_buf, sizeof(read_buf) - 1,
                                               (ctx->flags & EXPECT_FLAG_SKIP_SEEDING) ? ctx->snapshot_line : 1,
                                               (ctx->flags & EXPECT_FLAG_SKIP_SEEDING) ? ctx->snapshot_offset : 0,
                                               &next_line, &next_offset);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* No data available, sleep with exponential backoff */
                log_debug("No data available, sleeping %dus", sleep_us);
                usleep(sleep_us);
                sleep_us = (sleep_us * 2 < max_sleep_us) ? sleep_us * 2 : max_sleep_us;
                continue;
            }
            
            /* Error reading */
            pattern_matcher_destroy(matcher);
            log_error("Error reading session output: %s", strerror(errno));
            return EXPECT_ERROR;
        }
        
        if (n == 0) {
            if (!session_is_alive(ctx->session)) {
                pattern_matcher_destroy(matcher);
                log_debug("Session closed (EOF)");
                return EXPECT_EOF;
            }
            
            log_debug("No new data yet, sleeping %dus", sleep_us);
            usleep(sleep_us);
            sleep_us = (sleep_us * 2 < max_sleep_us) ? sleep_us * 2 : max_sleep_us;
            continue;
        }
        
        log_debug("Read %zd bytes from expect buffer: '%.50s%s'", 
                  n, read_buf, n > 50 ? "..." : "");
        
        if (ctx->flags & EXPECT_FLAG_SKIP_SEEDING) {
            ctx->snapshot_line = next_line;
            ctx->snapshot_offset = next_offset;
        }
        
        /* Got data, reset backoff */
        sleep_us = 10000;
        
        /* Append to buffer */
        read_buf[n] = '\0';
        
        if (ctx->buffer_used + n >= ctx->buffer_size) {
            /* Buffer full, shift or expand */
            if (ctx->buffer_used > ctx->buffer_size / 2) {
                /* Shift buffer to make room */
                size_t shift = ctx->buffer_size / 4;
                memmove(ctx->buffer, ctx->buffer + shift, ctx->buffer_used - shift);
                ctx->buffer_used -= shift;
            } else {
                /* Expand buffer */
                size_t new_size = ctx->buffer_size * 2;
                char *new_buffer = realloc(ctx->buffer, new_size);
                if (!new_buffer) {
                    pattern_matcher_destroy(matcher);
                    log_error("Failed to expand expect buffer");
                    return EXPECT_ERROR;
                }
                ctx->buffer = new_buffer;
                ctx->buffer_size = new_size;
            }
        }
        
        memcpy(ctx->buffer + ctx->buffer_used, read_buf, n);
        ctx->buffer_used += n;
        ctx->buffer[ctx->buffer_used] = '\0';
    }
}

expect_result_t expect_any(expect_context_t *ctx, const char **patterns,
                           int num_patterns, bool case_sensitive,
                           int *matched_index, match_result_t *result) {
    if (!ctx || !patterns || num_patterns <= 0) {
        return EXPECT_ERROR;
    }
    
    /* Create pattern matchers */
    pattern_matcher_t **matchers = calloc(num_patterns, sizeof(pattern_matcher_t*));
    if (!matchers) {
        log_error("Failed to allocate matchers array");
        return EXPECT_ERROR;
    }
    
    for (int i = 0; i < num_patterns; i++) {
        matchers[i] = pattern_matcher_create(patterns[i], case_sensitive);
        if (!matchers[i]) {
            /* Cleanup on error */
            for (int j = 0; j < i; j++) {
                pattern_matcher_destroy(matchers[j]);
            }
            free(matchers);
            return EXPECT_ERROR;
        }
    }
    
    /* Read and match loop */
    expect_result_t res = EXPECT_ERROR;
    
    while (1) {
        /* Check timeout */
        if (expect_is_timeout(ctx)) {
            res = EXPECT_TIMEOUT;
            break;
        }
        
        /* Try to match current buffer with any pattern */
        for (int i = 0; i < num_patterns; i++) {
            match_result_t match;
            if (pattern_matcher_match(matchers[i], ctx->buffer, &match)) {
                if (matched_index) {
                    *matched_index = i;
                }
                
                if (result) {
                    *result = match;
                } else {
                    match_result_free(&match);
                }
                
                log_debug("Pattern %d matched: '%s'", i, patterns[i]);
                res = EXPECT_MATCHED;
                goto cleanup;
            }
        }
        
        /* Read more output */
        char read_buf[EXPECT_READ_SIZE];
        session_line_no_t next_line = ctx->snapshot_line;
        size_t next_offset = ctx->snapshot_offset;
        ssize_t n = session_read_expect_output(ctx->session, read_buf, sizeof(read_buf) - 1,
                                               1, 0, &next_line, &next_offset);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            
            res = EXPECT_ERROR;
            break;
        }
        
        if (n == 0) {
            if (!session_is_alive(ctx->session)) {
                res = EXPECT_EOF;
                break;
            }
            
            usleep(10000);
            continue;
        }
        
        /* Append to buffer */
        read_buf[n] = '\0';
        
        if (ctx->buffer_used + n >= ctx->buffer_size) {
            if (ctx->buffer_used > ctx->buffer_size / 2) {
                size_t shift = ctx->buffer_size / 4;
                memmove(ctx->buffer, ctx->buffer + shift, ctx->buffer_used - shift);
                ctx->buffer_used -= shift;
            } else {
                size_t new_size = ctx->buffer_size * 2;
                char *new_buffer = realloc(ctx->buffer, new_size);
                if (!new_buffer) {
                    res = EXPECT_ERROR;
                    break;
                }
                ctx->buffer = new_buffer;
                ctx->buffer_size = new_size;
            }
        }
        
        memcpy(ctx->buffer + ctx->buffer_used, read_buf, n);
        ctx->buffer_used += n;
        ctx->buffer[ctx->buffer_used] = '\0';
    }
    
cleanup:
    /* Cleanup matchers */
    for (int i = 0; i < num_patterns; i++) {
        pattern_matcher_destroy(matchers[i]);
    }
    free(matchers);
    
    return res;
}

expect_result_t expect_string(expect_context_t *ctx, const char *str,
                              bool case_sensitive) {
    if (!ctx || !str) {
        return EXPECT_ERROR;
    }
    
    /* Escape special regex characters and create pattern */
    size_t len = strlen(str);
    char *pattern = malloc(len * 2 + 1);
    if (!pattern) {
        return EXPECT_ERROR;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = str[i];
        /* Escape regex special characters */
        if (strchr(".*+?[]{}()^$|\\", ch)) {
            pattern[j++] = '\\';
        }
        pattern[j++] = ch;
    }
    pattern[j] = '\0';
    
    expect_result_t result = expect_pattern(ctx, pattern, case_sensitive, NULL);
    
    free(pattern);
    return result;
}

expect_result_t expect_send_and_wait(session_t *session, const char *send_text,
                                     const char *pattern, int timeout_ms,
                                     match_result_t *result) {
    if (!session || !pattern) {
        return EXPECT_ERROR;
    }
    
    /* Send text if provided */
    if (send_text) {
        ssize_t written = session_send_input(session, send_text, strlen(send_text));
        if (written < 0) {
            log_error("Failed to send input");
            return EXPECT_ERROR;
        }
    }
    
    /* Create expect context */
    expect_context_t *ctx = expect_context_create(session, timeout_ms, EXPECT_FLAG_NONE);
    if (!ctx) {
        return EXPECT_ERROR;
    }
    
    /* Wait for pattern */
    expect_result_t res = expect_pattern(ctx, pattern, true, result);
    
    expect_context_destroy(ctx);
    
    return res;
}

const char *expect_result_name(expect_result_t result) {
    switch (result) {
        case EXPECT_MATCHED: return "MATCHED";
        case EXPECT_TIMEOUT: return "TIMEOUT";
        case EXPECT_EOF: return "EOF";
        case EXPECT_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}