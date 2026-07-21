#include "pshell/pattern_matcher.h"
#include "pshell/expect_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("Running test: %s...", name); \
        fflush(stdout);

#define TEST_END \
        tests_passed++; \
        printf(" PASSED\n"); \
    } while(0)

/* Test: Pattern validation */
void test_pattern_validation(void) {
    TEST("pattern_validation");
    
    char error[256];
    
    /* Valid patterns */
    assert(pattern_validate("hello", error, sizeof(error)) == true);
    assert(pattern_validate("^start", error, sizeof(error)) == true);
    assert(pattern_validate("end$", error, sizeof(error)) == true);
    assert(pattern_validate("[0-9]+", error, sizeof(error)) == true);
    assert(pattern_validate("(group)", error, sizeof(error)) == true);
    
    /* Invalid patterns */
    assert(pattern_validate("[", error, sizeof(error)) == false);
    assert(pattern_validate("(", error, sizeof(error)) == false);
    assert(pattern_validate("*", error, sizeof(error)) == false);
    
    TEST_END;
}

/* Test: Basic pattern matching */
void test_basic_matching(void) {
    TEST("basic_matching");
    
    pattern_matcher_t *matcher = pattern_matcher_create("hello", true);
    assert(matcher != NULL);
    
    match_result_t result;
    
    /* Should match */
    assert(pattern_matcher_match(matcher, "hello world", &result) == true);
    assert(result.matched == true);
    assert(result.start == 0);
    assert(result.end == 5);
    assert(strcmp(result.matched_text, "hello") == 0);
    match_result_free(&result);
    
    /* Should not match (case sensitive) */
    assert(pattern_matcher_match(matcher, "HELLO world", &result) == false);
    
    pattern_matcher_destroy(matcher);
    
    TEST_END;
}

/* Test: Case insensitive matching */
void test_case_insensitive(void) {
    TEST("case_insensitive");
    
    pattern_matcher_t *matcher = pattern_matcher_create("hello", false);
    assert(matcher != NULL);
    
    match_result_t result;
    
    /* Should match both cases */
    assert(pattern_matcher_match(matcher, "hello world", &result) == true);
    match_result_free(&result);
    
    assert(pattern_matcher_match(matcher, "HELLO world", &result) == true);
    match_result_free(&result);
    
    assert(pattern_matcher_match(matcher, "HeLLo world", &result) == true);
    match_result_free(&result);
    
    pattern_matcher_destroy(matcher);
    
    TEST_END;
}

/* Test: Regex patterns */
void test_regex_patterns(void) {
    TEST("regex_patterns");
    
    match_result_t result;
    
    /* Test digit pattern */
    pattern_matcher_t *digit = pattern_matcher_create("[0-9]+", true);
    assert(digit != NULL);
    assert(pattern_matcher_match(digit, "Port 8080 is open", &result) == true);
    assert(strcmp(result.matched_text, "8080") == 0);
    match_result_free(&result);
    pattern_matcher_destroy(digit);
    
    /* Test word boundary */
    pattern_matcher_t *word = pattern_matcher_create("\\btest\\b", true);
    assert(word != NULL);
    assert(pattern_matcher_match(word, "this is a test case", &result) == true);
    assert(strcmp(result.matched_text, "test") == 0);
    match_result_free(&result);
    pattern_matcher_destroy(word);
    
    /* Test anchors */
    pattern_matcher_t *start = pattern_matcher_create("^Error:", true);
    assert(start != NULL);
    assert(pattern_matcher_match(start, "Error: something failed", &result) == true);
    match_result_free(&result);
    assert(pattern_matcher_match(start, "No Error: here", &result) == false);
    pattern_matcher_destroy(start);
    
    TEST_END;
}

/* Test: Capture groups */
void test_capture_groups(void) {
    TEST("capture_groups");
    
    pattern_matcher_t *matcher = pattern_matcher_create("([0-9]+)\\.([0-9]+)", true);
    assert(matcher != NULL);
    
    match_result_t result;
    const char *text = "Version 1.23 released";
    
    assert(pattern_matcher_match(matcher, text, &result) == true);
    assert(result.nmatch == 3);  /* Full match + 2 groups */
    
    /* Get submatches */
    char *full = match_result_get_submatch(text, &result, 0);
    char *major = match_result_get_submatch(text, &result, 1);
    char *minor = match_result_get_submatch(text, &result, 2);
    
    assert(strcmp(full, "1.23") == 0);
    assert(strcmp(major, "1") == 0);
    assert(strcmp(minor, "23") == 0);
    
    free(full);
    free(major);
    free(minor);
    match_result_free(&result);
    pattern_matcher_destroy(matcher);
    
    TEST_END;
}

/* Test: Match offset */
void test_match_offset(void) {
    TEST("match_offset");
    
    pattern_matcher_t *matcher = pattern_matcher_create("test", true);
    assert(matcher != NULL);
    
    match_result_t result;
    const char *text = "test test test";
    
    /* First match */
    assert(pattern_matcher_match_offset(matcher, text, 0, &result) == true);
    assert(result.start == 0);
    match_result_free(&result);
    
    /* Second match */
    assert(pattern_matcher_match_offset(matcher, text, 5, &result) == true);
    assert(result.start == 5);
    match_result_free(&result);
    
    /* Third match */
    assert(pattern_matcher_match_offset(matcher, text, 10, &result) == true);
    assert(result.start == 10);
    match_result_free(&result);
    
    pattern_matcher_destroy(matcher);
    
    TEST_END;
}

/* Test: Expect result names */
void test_expect_result_names(void) {
    TEST("expect_result_names");
    
    assert(strcmp(expect_result_name(EXPECT_MATCHED), "MATCHED") == 0);
    assert(strcmp(expect_result_name(EXPECT_TIMEOUT), "TIMEOUT") == 0);
    assert(strcmp(expect_result_name(EXPECT_EOF), "EOF") == 0);
    assert(strcmp(expect_result_name(EXPECT_ERROR), "ERROR") == 0);
    
    TEST_END;
}

/* Test: Complex patterns */
void test_complex_patterns(void) {
    TEST("complex_patterns");
    
    match_result_t result;
    
    /* Email pattern */
    pattern_matcher_t *email = pattern_matcher_create(
        "[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}", true);
    assert(email != NULL);
    assert(pattern_matcher_match(email, "Contact: user@example.com", &result) == true);
    assert(strstr(result.matched_text, "@") != NULL);
    match_result_free(&result);
    pattern_matcher_destroy(email);
    
    /* IP address pattern */
    pattern_matcher_t *ip = pattern_matcher_create(
        "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}", true);
    assert(ip != NULL);
    assert(pattern_matcher_match(ip, "Server: 192.168.1.1", &result) == true);
    assert(strcmp(result.matched_text, "192.168.1.1") == 0);
    match_result_free(&result);
    pattern_matcher_destroy(ip);
    
    /* Shell prompt pattern */
    pattern_matcher_t *prompt = pattern_matcher_create("[$#>]\\s*$", true);
    assert(prompt != NULL);
    assert(pattern_matcher_match(prompt, "user@host:~$ ", &result) == true);
    match_result_free(&result);
    pattern_matcher_destroy(prompt);
    
    TEST_END;
}

int main(void) {
    printf("=== Pattern Matching Tests ===\n\n");
    
    test_pattern_validation();
    test_basic_matching();
    test_case_insensitive();
    test_regex_patterns();
    test_capture_groups();
    test_match_offset();
    test_expect_result_names();
    test_complex_patterns();
    
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("\nAll tests PASSED!\n");
        return 0;
    } else {
        printf("\nSome tests FAILED!\n");
        return 1;
    }
}
