#include "pshell/pattern_matcher.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

pattern_matcher_t *pattern_matcher_create(const char *pattern, bool case_sensitive) {
    if (!pattern) {
        log_error("Invalid pattern");
        return NULL;
    }
    
    pattern_matcher_t *matcher = calloc(1, sizeof(pattern_matcher_t));
    if (!matcher) {
        log_error("Failed to allocate pattern matcher");
        return NULL;
    }
    
    matcher->pattern = strdup(pattern);
    if (!matcher->pattern) {
        free(matcher);
        return NULL;
    }
    
    /* Set compilation flags */
    matcher->cflags = REG_EXTENDED;
    if (!case_sensitive) {
        matcher->cflags |= REG_ICASE;
    }
    
    /* Compile regex */
    int ret = regcomp(&matcher->regex, pattern, matcher->cflags);
    if (ret != 0) {
        char error_buf[256];
        regerror(ret, &matcher->regex, error_buf, sizeof(error_buf));
        log_error("Failed to compile regex '%s': %s", pattern, error_buf);
        free(matcher->pattern);
        free(matcher);
        return NULL;
    }
    
    matcher->compiled = true;
    
    log_debug("Created pattern matcher: pattern='%s', case_sensitive=%d", 
              pattern, case_sensitive);
    
    return matcher;
}

void pattern_matcher_destroy(pattern_matcher_t *matcher) {
    if (!matcher) return;
    
    if (matcher->compiled) {
        regfree(&matcher->regex);
    }
    
    free(matcher->pattern);
    free(matcher);
}

bool pattern_matcher_match(pattern_matcher_t *matcher, const char *text, match_result_t *result) {
    return pattern_matcher_match_offset(matcher, text, 0, result);
}

bool pattern_matcher_match_offset(pattern_matcher_t *matcher, const char *text,
                                  size_t offset, match_result_t *result) {
    if (!matcher || !text || !result) {
        return false;
    }
    
    if (!matcher->compiled) {
        log_error("Pattern not compiled");
        return false;
    }
    
    /* Initialize result */
    memset(result, 0, sizeof(*result));
    
    /* Allocate match array (1 for full match + up to 9 submatches) */
    const int max_matches = 10;
    regmatch_t *matches = calloc(max_matches, sizeof(regmatch_t));
    if (!matches) {
        log_error("Failed to allocate match array");
        return false;
    }
    
    /* Execute regex match */
    const char *search_text = text + offset;
    int ret = regexec(&matcher->regex, search_text, max_matches, matches, 0);
    
    if (ret == REG_NOMATCH) {
        free(matches);
        return false;
    }
    
    if (ret != 0) {
        char error_buf[256];
        regerror(ret, &matcher->regex, error_buf, sizeof(error_buf));
        log_error("Regex execution failed: %s", error_buf);
        free(matches);
        return false;
    }
    
    /* Match found */
    result->matched = true;
    result->start = offset + matches[0].rm_so;
    result->end = offset + matches[0].rm_eo;
    
    /* Extract matched text */
    size_t match_len = matches[0].rm_eo - matches[0].rm_so;
    result->matched_text = malloc(match_len + 1);
    if (result->matched_text) {
        memcpy(result->matched_text, search_text + matches[0].rm_so, match_len);
        result->matched_text[match_len] = '\0';
    }
    
    /* Count submatches */
    result->nmatch = 0;
    for (int i = 0; i < max_matches; i++) {
        if (matches[i].rm_so == -1) break;
        result->nmatch++;
    }
    
    /* Store match positions */
    result->matches = matches;
    
    log_debug("Pattern matched: start=%zu, end=%zu, text='%s'",
              result->start, result->end, result->matched_text ? result->matched_text : "");
    
    return true;
}

void match_result_free(match_result_t *result) {
    if (!result) return;
    
    free(result->matched_text);
    free(result->matches);
    
    memset(result, 0, sizeof(*result));
}

char *match_result_get_submatch(const char *text, const match_result_t *result, int index) {
    if (!text || !result || !result->matches) {
        return NULL;
    }
    
    if (index < 0 || index >= result->nmatch) {
        return NULL;
    }
    
    regmatch_t *match = &result->matches[index];
    if (match->rm_so == -1) {
        return NULL;
    }
    
    size_t len = match->rm_eo - match->rm_so;
    char *submatch = malloc(len + 1);
    if (!submatch) {
        return NULL;
    }
    
    memcpy(submatch, text + match->rm_so, len);
    submatch[len] = '\0';
    
    return submatch;
}

bool pattern_validate(const char *pattern, char *error_buf, size_t error_size) {
    if (!pattern) {
        if (error_buf && error_size > 0) {
            snprintf(error_buf, error_size, "Pattern is NULL");
        }
        return false;
    }
    
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    
    if (ret != 0) {
        if (error_buf && error_size > 0) {
            regerror(ret, &regex, error_buf, error_size);
        }
        return false;
    }
    
    regfree(&regex);
    return true;
}