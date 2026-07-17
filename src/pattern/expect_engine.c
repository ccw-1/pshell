#include "pshell/expect_engine.h"
#include "pshell/pattern_matcher.h"
#include "pshell/session.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

#define INITIAL_BUFFER_CAPACITY 4096
#define READ_CHUNK_SIZE 1024

expect_engine_t *expect_engine_create(session_t *session, int timeout_ms, bool case_sensitive)
{
    if (!session) {
        return NULL;
    }

    expect_engine_t *engine = calloc(1, sizeof(expect_engine_t));
    if (!engine) {
        return NULL;
    }

    engine->session = session;
    engine->timeout_ms = timeout_ms;
    engine->case_sensitive = case_sensitive;
    
    /* Allocate initial buffer */
    engine->buffer_capacity = INITIAL_BUFFER_CAPACITY;
    engine->buffer = malloc(engine->buffer_capacity);
    if (!engine->buffer) {
        free(engine);
        return NULL;
    }
    engine->buffer[0] = '\0';
    engine->buffer_size = 0;

    return engine;
}

void expect_engine_destroy(expect_engine_t *engine)
{
    if (!engine) {
        return;
    }

    /* Free patterns */
    for (int i = 0; i < engine->npatterns; i++) {
        pattern_matcher_destroy(engine->patterns[i]);
    }
    free(engine->patterns);

    free(engine->buffer);
    free(engine);
}

int expect_engine_add_pattern(expect_engine_t *engine, const char *pattern)
{
    if (!engine || !pattern) {
        return -1;
    }

    /* Create pattern matcher */
    pattern_matcher_t *matcher = pattern_matcher_create(pattern, engine->case_sensitive);
    if (!matcher) {
        return -1;
    }

    /* Expand patterns array */
    pattern_matcher_t **new_patterns = realloc(engine->patterns, 
                                               (engine->npatterns + 1) * sizeof(pattern_matcher_t *));
    if (!new_patterns) {
        pattern_matcher_destroy(matcher);
        return -1;
    }

    engine->patterns = new_patterns;
    engine->patterns[engine->npatterns] = matcher;
    return engine->npatterns++;
}

void expect_engine_clear_patterns(expect_engine_t *engine)
{
    if (!engine) {
        return;
    }

    for (int i = 0; i < engine->npatterns; i++) {
        pattern_matcher_destroy(engine->patterns[i]);
    }
    free(engine->patterns);
    
    engine->patterns = NULL;
    engine->npatterns = 0;
}

static bool expand_buffer(expect_engine_t *engine, size_t needed)
{
    if (engine->buffer_size + needed <= engine->buffer_capacity) {
        return true;
    }

    size_t new_capacity = engine->buffer_capacity * 2;
    while (new_capacity < engine->buffer_size + needed) {
        new_capacity *= 2;
    }

    char *new_buffer = realloc(engine->buffer, new_capacity);
    if (!new_buffer) {
        return false;
    }

    engine->buffer = new_buffer;
    engine->buffer_capacity = new_capacity;
    return true;
}

static expect_result_t read_and_match(expect_engine_t *engine, expect_match_t *match)
{
    int fd = session_get_fd(engine->session);
    if (fd < 0) {
        return EXPECT_ERROR;
    }

    /* Calculate timeout */
    struct timeval start_time, current_time, timeout_tv;
    gettimeofday(&start_time, NULL);
    
    while (1) {
        /* Check for pattern match in current buffer */
        for (int i = 0; i < engine->npatterns; i++) {
            match_result_t result;
            if (pattern_matcher_match(engine->patterns[i], engine->buffer, &result)) {
                /* Pattern matched! */
                match->result = EXPECT_MATCHED;
                match->pattern_index = i;
                match->match_start = result.start;
                match->match_end = result.end;
                match->matched_text = result.matched_text;
                result.matched_text = NULL; /* Transfer ownership */

                /* Extract before/after text */
                match->before = strndup(engine->buffer, result.start);
                match->after = strdup(engine->buffer + result.end);

                /* Extract submatches */
                match->nmatch = result.nmatch;
                if (result.nmatch > 0) {
                    match->submatches = calloc(result.nmatch, sizeof(char *));
                    for (int j = 0; j < result.nmatch; j++) {
                        match->submatches[j] = match_result_get_submatch(engine->buffer, &result, j);
                    }
                }

                match_result_free(&result);
                return EXPECT_MATCHED;
            }
            match_result_free(&result);
        }

        /* Calculate remaining timeout */
        if (engine->timeout_ms > 0) {
            gettimeofday(&current_time, NULL);
            long elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                            (current_time.tv_usec - start_time.tv_usec) / 1000;
            
            if (elapsed_ms >= engine->timeout_ms) {
                match->result = EXPECT_TIMEOUT;
                return EXPECT_TIMEOUT;
            }

            long remaining_ms = engine->timeout_ms - elapsed_ms;
            timeout_tv.tv_sec = remaining_ms / 1000;
            timeout_tv.tv_usec = (remaining_ms % 1000) * 1000;
        }

        /* Wait for data */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        int ret = select(fd + 1, &readfds, NULL, NULL, 
                        engine->timeout_ms > 0 ? &timeout_tv : NULL);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted, retry */
            }
            match->result = EXPECT_ERROR;
            return EXPECT_ERROR;
        }

        if (ret == 0) {
            /* Timeout */
            match->result = EXPECT_TIMEOUT;
            return EXPECT_TIMEOUT;
        }

        /* Read available data */
        char chunk[READ_CHUNK_SIZE];
        ssize_t nread = read(fd, chunk, sizeof(chunk) - 1);
        
        if (nread < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            match->result = EXPECT_ERROR;
            return EXPECT_ERROR;
        }

        if (nread == 0) {
            /* EOF */
            match->result = EXPECT_EOF;
            return EXPECT_EOF;
        }

        /* Append to buffer */
        if (!expand_buffer(engine, nread + 1)) {
            match->result = EXPECT_ERROR;
            return EXPECT_ERROR;
        }

        memcpy(engine->buffer + engine->buffer_size, chunk, nread);
        engine->buffer_size += nread;
        engine->buffer[engine->buffer_size] = '\0';
    }
}

expect_result_t expect_engine_wait(expect_engine_t *engine, expect_match_t *match)
{
    if (!engine || !match) {
        return EXPECT_ERROR;
    }

    /* Initialize match */
    memset(match, 0, sizeof(expect_match_t));
    match->pattern_index = -1;

    if (engine->npatterns == 0) {
        match->result = EXPECT_ERROR;
        return EXPECT_ERROR;
    }

    return read_and_match(engine, match);
}

expect_result_t expect_engine_wait_patterns(expect_engine_t *engine, 
                                           const char **patterns, int npatterns,
                                           expect_match_t *match)
{
    if (!engine || !patterns || npatterns <= 0 || !match) {
        return EXPECT_ERROR;
    }

    /* Save current patterns */
    pattern_matcher_t **saved_patterns = engine->patterns;
    int saved_npatterns = engine->npatterns;

    /* Set temporary patterns */
    engine->patterns = NULL;
    engine->npatterns = 0;

    for (int i = 0; i < npatterns; i++) {
        if (expect_engine_add_pattern(engine, patterns[i]) < 0) {
            /* Restore and fail */
            expect_engine_clear_patterns(engine);
            engine->patterns = saved_patterns;
            engine->npatterns = saved_npatterns;
            return EXPECT_ERROR;
        }
    }

    /* Wait for match */
    expect_result_t result = expect_engine_wait(engine, match);

    /* Restore original patterns */
    expect_engine_clear_patterns(engine);
    engine->patterns = saved_patterns;
    engine->npatterns = saved_npatterns;

    return result;
}

void expect_engine_set_timeout(expect_engine_t *engine, int timeout_ms)
{
    if (engine) {
        engine->timeout_ms = timeout_ms;
    }
}

const char *expect_engine_get_buffer(expect_engine_t *engine)
{
    return engine ? engine->buffer : NULL;
}

void expect_engine_clear_buffer(expect_engine_t *engine)
{
    if (engine) {
        engine->buffer_size = 0;
        if (engine->buffer) {
            engine->buffer[0] = '\0';
        }
    }
}

void expect_match_free(expect_match_t *match)
{
    if (!match) {
        return;
    }

    free(match->matched_text);
    free(match->before);
    free(match->after);

    if (match->submatches) {
        for (int i = 0; i < match->nmatch; i++) {
            free(match->submatches[i]);
        }
        free(match->submatches);
    }

    memset(match, 0, sizeof(expect_match_t));
}
