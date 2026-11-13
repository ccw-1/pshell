#include "pshell/vt100_parser.h"
#include "pshell/screen_buffer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("Testing %s... ", name); \
    fflush(stdout);

#define PASS() \
    printf("PASS\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
    return;

void test_vt100_parser_init(void) {
    TEST("vt100_parser_init");
    
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    assert(parser.state == VT100_STATE_NORMAL);
    assert(parser.param_count == 0);
    
    PASS();
}

void test_vt100_parse_printable(void) {
    TEST("vt100_parse_printable");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    bool result = vt100_parser_feed(&parser, 'A', &seq);
    
    if (!result) {
        FAIL("Expected sequence");
        return;
    }
    
    if (seq.type != VT100_SEQ_PRINT) {
        FAIL("Expected PRINT sequence");
        return;
    }
    
    if (seq.text[0] != 'A') {
        FAIL("Expected 'A'");
        return;
    }
    
    PASS();
}

void test_vt100_parse_cursor_up(void) {
    TEST("vt100_parse_cursor_up");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    const char *input = "\x1B[5A";  /* ESC [ 5 A */
    bool got_seq = false;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            got_seq = true;
            break;
        }
    }
    
    if (!got_seq) {
        FAIL("No sequence parsed");
        return;
    }
    
    if (seq.type != VT100_SEQ_CURSOR_UP) {
        FAIL("Expected CURSOR_UP");
        return;
    }
    
    if (seq.params[0] != 5) {
        FAIL("Expected param 5");
        return;
    }
    
    PASS();
}

void test_vt100_parse_sgr(void) {
    TEST("vt100_parse_sgr");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    const char *input = "\x1B[1;31m";  /* ESC [ 1 ; 31 m (bold red) */
    bool got_seq = false;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            got_seq = true;
            break;
        }
    }
    
    if (!got_seq) {
        FAIL("No sequence parsed");
        return;
    }
    
    if (seq.type != VT100_SEQ_SGR) {
        FAIL("Expected SGR");
        return;
    }
    
    if (seq.param_count != 2) {
        FAIL("Expected 2 params");
        return;
    }
    
    if (seq.params[0] != 1 || seq.params[1] != 31) {
        FAIL("Expected params 1, 31");
        return;
    }
    
    PASS();
}

void test_screen_buffer_create(void) {
    TEST("screen_buffer_create");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    if (screen->rows != 24 || screen->cols != 80) {
        FAIL("Wrong dimensions");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_put_char(void) {
    TEST("screen_buffer_put_char");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Put some characters */
    screen_buffer_put_char(screen, 'H');
    screen_buffer_put_char(screen, 'e');
    screen_buffer_put_char(screen, 'l');
    screen_buffer_put_char(screen, 'l');
    screen_buffer_put_char(screen, 'o');
    
    /* Check cursor position */
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 0 || col != 5) {
        FAIL("Wrong cursor position");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check content */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "Hello", 5) != 0) {
        FAIL("Wrong content");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_cursor_movement(void) {
    TEST("screen_buffer_cursor_movement");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Move cursor */
    screen_buffer_set_cursor(screen, 5, 10);
    
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 5 || col != 10) {
        FAIL("Wrong cursor position after set");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Move up */
    screen_buffer_cursor_up(screen, 2);
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 3 || col != 10) {
        FAIL("Wrong cursor position after up");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Move forward */
    screen_buffer_cursor_forward(screen, 5);
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 3 || col != 15) {
        FAIL("Wrong cursor position after forward");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_erase(void) {
    TEST("screen_buffer_erase");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Fill first line */
    for (int i = 0; i < 10; i++) {
        screen_buffer_put_char(screen, 'X');
    }
    
    /* Erase from cursor to end of line */
    screen_buffer_set_cursor(screen, 0, 5);
    screen_buffer_erase_line(screen, 0);
    
    /* Check that first 5 chars are still there */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "XXXXX", 5) != 0) {
        FAIL("First 5 chars should remain");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check that rest is cleared */
    for (int i = 5; i < 10; i++) {
        if (line[i] != ' ') {
            FAIL("Rest should be cleared");
            screen_buffer_destroy(screen);
            return;
        }
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_process_sequence(void) {
    TEST("screen_buffer_process_sequence");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    /* Parse and process "Hello\n" */
    const char *input = "Hello\n";
    vt100_sequence_t seq;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            screen_buffer_process_sequence(screen, &seq);
        }
    }
    
    /* Check content */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "Hello", 5) != 0) {
        FAIL("Wrong content");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check cursor moved to next line */
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 1) {
        FAIL("Cursor should be on line 1");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

/* Feed text through the parser into the screen */
static void feed_text(screen_buffer_t *screen, vt100_parser_t *parser, const char *text) {
    vt100_sequence_t seq;
    for (int i = 0; text[i]; i++) {
        if (vt100_parser_feed(parser, text[i], &seq)) {
            screen_buffer_process_sequence(screen, &seq);
        }
    }
}

static int scrollback_line_starts(const screen_buffer_t *screen, int idx, const char *prefix) {
    const char_cell_t *cells = screen_buffer_get_scrollback_line(screen, idx);
    if (!cells) return 0;
    for (size_t i = 0; prefix[i]; i++) {
        if (cells[i].ch != prefix[i]) return 0;
    }
    return 1;
}

/* Regression: widening the screen used to leave the scrollback allocated for
 * the old width, so later scrolled-off lines overflowed the heap (daemon
 * segfault after `pshellctl attach` resized the session). Run under ASAN. */
void test_screen_buffer_resize_scrollback(void) {
    TEST("screen_buffer_resize_scrollback");
    
    screen_buffer_t *screen = screen_buffer_create(5, 10, 20);
    if (!screen) {
        FAIL("Failed to create screen buffer");
    }
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    char buf[32];
    for (int i = 0; i < 12; i++) {
        snprintf(buf, sizeof(buf), "L%02d\r\n", i);
        feed_text(screen, &parser, buf);
    }
    int used_before = screen->scrollback_used;
    if (used_before <= 0 || used_before >= 20) {
        screen_buffer_destroy(screen);
        FAIL("Unexpected scrollback_used before resize");
    }
    
    if (screen_buffer_resize(screen, 8, 40) != 0) {
        screen_buffer_destroy(screen);
        FAIL("Resize failed");
    }
    if (screen->scrollback_used != used_before ||
        !scrollback_line_starts(screen, 0, "L00")) {
        screen_buffer_destroy(screen);
        FAIL("Scrollback content lost or reordered by resize");
    }
    
    /* Wrap the (now wider) scrollback several times */
    for (int i = 12; i < 100; i++) {
        snprintf(buf, sizeof(buf), "L%02d\r\n", i);
        feed_text(screen, &parser, buf);
    }
    if (screen->scrollback_used != 20) {
        screen_buffer_destroy(screen);
        FAIL("Scrollback should be full");
    }
    /* Newest scrolled-off line: 100 lines fed, 8 rows on screen (last row is the empty cursor row) */
    if (!scrollback_line_starts(screen, 19, "L92")) {
        screen_buffer_destroy(screen);
        FAIL("Newest scrollback line wrong");
    }
    
    /* Shrinking must keep order and not overflow either */
    if (screen_buffer_resize(screen, 4, 6) != 0 ||
        !scrollback_line_starts(screen, 19, "L92") ||
        !scrollback_line_starts(screen, 0, "L73")) {
        screen_buffer_destroy(screen);
        FAIL("Shrink broke scrollback");
    }
    for (int i = 0; i < 50; i++) {
        feed_text(screen, &parser, "xy\r\n");
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

int main(void) {
    printf("=== Terminal Emulation Tests ===\n\n");
    
    /* VT100 Parser Tests */
    test_vt100_parser_init();
    test_vt100_parse_printable();
    test_vt100_parse_cursor_up();
    test_vt100_parse_sgr();
    
    /* Screen Buffer Tests */
    test_screen_buffer_create();
    test_screen_buffer_put_char();
    test_screen_buffer_cursor_movement();
    test_screen_buffer_erase();
    test_screen_buffer_process_sequence();
    test_screen_buffer_resize_scrollback();
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
