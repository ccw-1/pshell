#ifndef PSHELL_PATTERN_MATCHER_H
#define PSHELL_PATTERN_MATCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <regex.h>

/**
 * Pattern Matcher - POSIX regex-based pattern matching
 * 
 * Provides regex pattern matching for terminal output,
 * used by the expect engine for automation.
 */

/* Match result */
typedef struct {
    bool matched;           /* Whether pattern matched */
    size_t start;           /* Start position of match */
    size_t end;             /* End position of match */
    char *matched_text;     /* Matched text (allocated) */
    int nmatch;             /* Number of submatches */
    regmatch_t *matches;    /* Submatch positions (allocated) */
} match_result_t;

/* Pattern matcher */
typedef struct {
    regex_t regex;          /* Compiled regex */
    char *pattern;          /* Original pattern string */
    int cflags;             /* Compilation flags */
    bool compiled;          /* Whether regex is compiled */
} pattern_matcher_t;

/**
 * Create pattern matcher
 * @param pattern Regex pattern string
 * @param case_sensitive Whether matching is case-sensitive
 * @return Matcher or NULL on error
 */
pattern_matcher_t *pattern_matcher_create(const char *pattern, bool case_sensitive);

/**
 * Destroy pattern matcher
 * @param matcher Matcher to destroy
 */
void pattern_matcher_destroy(pattern_matcher_t *matcher);

/**
 * Match pattern against text
 * @param matcher Pattern matcher
 * @param text Text to search
 * @param result Output match result (caller must free with match_result_free)
 * @return true if pattern matched
 */
bool pattern_matcher_match(pattern_matcher_t *matcher, const char *text, match_result_t *result);

/**
 * Match pattern against text with offset
 * @param matcher Pattern matcher
 * @param text Text to search
 * @param offset Offset to start searching from
 * @param result Output match result (caller must free with match_result_free)
 * @return true if pattern matched
 */
bool pattern_matcher_match_offset(pattern_matcher_t *matcher, const char *text, 
                                  size_t offset, match_result_t *result);

/**
 * Free match result resources
 * @param result Match result to free
 */
void match_result_free(match_result_t *result);

/**
 * Get submatch text
 * @param text Original text
 * @param result Match result
 * @param index Submatch index (0 = full match, 1+ = capture groups)
 * @return Submatch text (allocated, caller must free) or NULL
 */
char *match_result_get_submatch(const char *text, const match_result_t *result, int index);

/**
 * Validate regex pattern
 * @param pattern Pattern to validate
 * @param error_buf Buffer for error message (optional)
 * @param error_size Size of error buffer
 * @return true if pattern is valid
 */
bool pattern_validate(const char *pattern, char *error_buf, size_t error_size);

#endif /* PSHELL_PATTERN_MATCHER_H */
