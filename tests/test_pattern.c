#include "pshell/pattern_matcher.h"
#include "pshell/timeout_manager.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test pattern validation */
static void test_pattern_validation(void) {
    char error[256];
    
    /* Valid patterns */
    assert(pattern_validate("hello", error, sizeof(error)));
    assert(pattern_validate("^[0-9]+$", error, sizeof(error)));
    assert(pattern_validate("(foo|bar)", error, sizeof(error)));
    
    /* Invalid patterns */
    assert(!pattern_validate("[", error, sizeof(error)));
    assert(!pattern_validate("(unclosed", error, sizeof(error)));
    
    printf("✓ Pattern validation tests passed\n");
}

/* Test basic pattern matching */
static void test_basic_matching(void) {
    pattern_matcher_t *matcher;
    match_result_t result;
    
    /* Test 1: Simple string match */
    matcher = pattern_matcher_create("hello", true);
    assert(matcher != NULL);
    
    assert(pattern_matcher_match(matcher, "hello world", &result));
    assert(result.matched);
    assert(result.start == 0);
    assert(result.end == 5);
    assert(strcmp(result.matched_text, "hello") == 0);
    match_result_free(&result);
    
    pattern_matcher_destroy(matcher);
    
    /* Test 2: Regex pattern */
    matcher = pattern_matcher_create("[0-9]+", true);
    assert(matcher != NULL);
    
    assert(pattern_matcher_match(matcher, "Port: 8080", &result));
    assert(result.matched);
    assert(strcmp(result.matched_text, "8080") == 0);
    match_result_free(&result);
    
    pattern_matcher_destroy(matcher);
    
    /* Test 3: Case-insensitive match */
    matcher = pattern_matcher_create("hello", false);
    assert(matcher != NULL);
    
    assert(pattern_matcher_match(matcher, "HELLO WORLD", &result));
    assert(result.matched);
    match_result_free(&result);
    
    pattern_matcher_destroy(matcher);
    
    printf("✓ Basic matching tests passed\n");
}

/* Test capture groups */
static void test_capture_groups(void) {
    pattern_matcher_t *matcher;
    match_result_t result;
    
    /* Pattern with capture groups */
    matcher = pattern_matcher_create("([a-z]+):([0-9]+)", true);
    assert(matcher != NULL);
    
    assert(pattern_matcher_match(matcher, "host:8080", &result));
    assert(result.matched);
    assert(result.nmatch == 3); /* Full match + 2 groups */
    
    /* Get submatches */
    char *full = match_result_get_submatch("host:8080", &result, 0);
    char *group1 = match_result_get_submatch("host:8080", &result, 1);
    char *group2 = match_result_get_submatch("host:8080", &result, 2);
    
    assert(strcmp(full, "host:8080") == 0);
    assert(strcmp(group1, "host") == 0);
    assert(strcmp(group2, "8080") == 0);
    
    free(full);
    free(group1);
    free(group2);
    match_result_free(&result);
    pattern_matcher_destroy(matcher);
    
    printf("✓ Capture group tests passed\n");
}

/* Test match offset */
static void test_match_offset(void) {
    pattern_matcher_t *matcher;
    match_result_t result;
    
    matcher = pattern_matcher_create("world", true);
    assert(matcher != NULL);
    
    /* Match from offset 0 - should find "world" */
    assert(pattern_matcher_match_offset(matcher, "hello world", 0, &result));
    assert(result.start == 6);
    match_result_free(&result);
    
    /* Match from offset 7 - should not find "world" */
    assert(!pattern_matcher_match_offset(matcher, "hello world", 7, &result));
    
    pattern_matcher_destroy(matcher);
    
    printf("✓ Match offset tests passed\n");
}

/* Test timeout manager */
static void test_timeout_manager(void) {
    timeout_ctx_t ctx;
    struct timeval tv;
    
    /* Test with timeout */
    timeout_init(&ctx, 1000); /* 1 second */
    assert(ctx.has_timeout);
    assert(!timeout_expired(&ctx));
    assert(timeout_remaining(&ctx) > 0);
    assert(timeout_to_timeval(&ctx, &tv));
    
    /* Test without timeout */
    timeout_init(&ctx, 0);
    assert(!ctx.has_timeout);
    assert(!timeout_expired(&ctx));
    assert(timeout_remaining(&ctx) == -1);
    assert(!timeout_to_timeval(&ctx, &tv));
    
    /* Test elapsed time */
    timeout_init(&ctx, 5000);
    usleep(100000); /* Sleep 100ms */
    long elapsed = timeout_elapsed(&ctx);
    assert(elapsed >= 100 && elapsed < 200);
    
    /* Test reset */
    timeout_reset(&ctx);
    elapsed = timeout_elapsed(&ctx);
    assert(elapsed < 50);
    
    printf("✓ Timeout manager tests passed\n");
}

/* Test no match */
static void test_no_match(void) {
    pattern_matcher_t *matcher;
    match_result_t result;
    
    matcher = pattern_matcher_create("xyz", true);
    assert(matcher != NULL);
    
    assert(!pattern_matcher_match(matcher, "hello world", &result));
    assert(!result.matched);
    
    pattern_matcher_destroy(matcher);
    
    printf("✓ No match tests passed\n");
}

int main(void) {
    printf("Running pattern matching tests...\n\n");
    
    test_pattern_validation();
    test_basic_matching();
    test_capture_groups();
    test_match_offset();
    test_timeout_manager();
    test_no_match();
    
    printf("\n✓ All pattern matching tests passed!\n");
    return 0;
}
